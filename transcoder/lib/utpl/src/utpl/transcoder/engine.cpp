#include <map>

#include "utpl/common/types/exception.h"
#include "utpl/common/debug.h"

#include "utpl/pipeline/pipe.h"

#include "utpl/transcoder/engine.h"
#include "utpl/transcoder/event/scheduler.h"

#include "utpl/transcoder/input/single.h"
#include "utpl/transcoder/input/standard.h"
#include "utpl/transcoder/input/playlist.h"

#include "utpl/transcoder/output/single.h"
#include "utpl/transcoder/output/group.h"

#include "utpl/esam/client.h"

using namespace utpl;

//#define DOT_DUMP

struct transcoder::Engine::Pimpl {
    enum class StopReason {
        NONE,
        USER_ACTION,
        PIPE_ERROR
    };
    
    enum class State {
        STOPPED,

        INPUT_ADD,
        INPUT_PROBE,
        CHANNEL_ADD,
        OUTPUT_ADD,
        CHANNEL_PREPARE,

        PLAYING,

        PIPELINE_STOP,
        OUTPUT_DEL,
        CHANNEL_DEL,
        INPUT_DEL
    };

    static std::string state2string(State state) {
        switch(state) {
            case State::STOPPED: return "STOPPED";
            
            case State::INPUT_ADD:       return "INPUT_ADD";
            case State::INPUT_PROBE:     return "INPUT_PROBE";
            case State::CHANNEL_ADD:     return "CHANNEL_ADD";
            case State::OUTPUT_ADD:      return "OUTPUT_ADD";
            case State::CHANNEL_PREPARE: return "CHANNEL_PREPARE";
            case State::PLAYING:         return "PLAYING";

            case State::PIPELINE_STOP: return "PIPELINE_STOP";
            case State::OUTPUT_DEL:    return "OUTPUT_DEL";
            case State::CHANNEL_DEL:   return "CHANNEL_DEL";
            case State::INPUT_DEL:     return "INPUT_DEL";

            default:
                return "<UNKNOWN>";
        }
    };

    State _nextState(State current, State target) {
        State ret = current;

        if (ret != target) {
            if (target > ret) {
                auto it = this->_upMap.find(ret);
                if (it != this->_upMap.end()) {
                    ret = it->second;

                } else {
                    throw utpl::Exception("Transition from state " + state2string(ret) + " is unknown!");
                }

            } else {
                auto it = this->_downMap.find(ret);
                if (it != this->_downMap.end()) {
                    ret = it->second;

                } else {
                    throw utpl::Exception("Transition from state " + state2string(ret) + " is unknown!");
                }
            }
        }

        return ret;
    }

    bool _switchState(State current, State target) {
        bool ret = true;

        if (current == target) {
            ret = false;

        } else {
            this->targetState  = target;
            this->pendingState = _nextState(current, target);
        }

        if (ret) {
            DBG(("Switching state from {} to {}", state2string(this->currentState), state2string(this->pendingState)));

            auto it = this->_transitionHandlers.find({ this->currentState, this->pendingState });
            if (it == this->_transitionHandlers.end()) {
                WARN(("Transition handler from state " + state2string(this->currentState) + " to " + state2string(this->pendingState) + " does not exist!"));

                throw utpl::Exception("Transition handler from state " + state2string(this->currentState) + " to " + state2string(this->pendingState) + " does not exist!");

            } else {
                it->second(this);
            }
        }

        return ret;
    }

    bool _switchStateToStop(StopReason reason) {
        this->stopReason = reason;

        return this->_switchState(this->currentState, State::STOPPED);
    }

    void _commitPendingState() {
        if (this->currentState != this->pendingState) {
            DBG(("Commiting state {}", state2string(this->pendingState)));

            this->currentState = this->pendingState;

            if (! this->_switchState(this->currentState, this->targetState)) {
                if (this->currentState == State::PLAYING) {
                    DBG(("Current state is PLAYING - calling play() callback"));

                    this->startCallback();

                } else if (this->currentState == State::STOPPED) {
                    DBG(("Current state is STOPPED - calling stop() callback"));

                    if (this->stopCallback) {
                        this->stopCallback();
                    }

                    if (this->stopReason == StopReason::PIPE_ERROR) {
                        this->interrupt();
                    }

                    this->stopReason = StopReason::NONE;
                }
            }
        }
    }

    Pimpl() : _blanker(&_channel), _adBreakSwitcher(&_channel) {
        this->currentState = this->pendingState = this->targetState = State::STOPPED;
    }

    ~Pimpl() {
        this->cleanup();
    }

    void prepare(const EngineParameters &parameters) {
        this->cleanup();

        this->_pipe = pipeline::Pipe::create();

        this->_pipe->onEos([this](pipeline::Pipe &pipe) {
            DBG(("EOS - stopping engine"));

            this->stop([this]() {
                this->interrupt();
            });
        });

        this->_pipe->onError([this](pipeline::Pipe &pipe, const std::string &message) {
            DBG(("Received an error ({}) - stopping engine", message));

#if defined(DOT_DUMP)
            GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN((*this->_pipe)->get()), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline.error");
#endif

            this->error = utpl::Error::PIPELINE_ERROR;

            this->stop([this]() {
                this->interrupt(false);
            });
        });

        {
            for (const auto &inputParams : parameters.getAllInputParameters()) {
                if (inputParams.getUris().size() == 1) {
                    if (inputParams.getUris()[0].find("pls+") != std::string::npos) {
                        this->_inputs.push_back({ std::make_shared<PlaylistInput>(), inputParams });

                    } else {
                        this->_inputs.push_back({ std::make_shared<SingleInput>(true), inputParams });
                    }

                } else {
                    this->_inputs.push_back({ std::make_shared<StandardInput>(), inputParams });
                }
            }
        }

        {
            for (const auto &outputParams : parameters.getOutputParameters()) {
                if (outputParams.getType() == OutputType::SINGLE) {
                    this->_outputs.push_back({
                        std::make_shared<SingleOutput>(), outputParams
                    });
                }
            }

            for (const auto &outputParams : parameters.getOutputParameters()) {
                if (outputParams.getType() == OutputType::GROUP) {
                    std::vector<std::shared_ptr<SingleOutput>> children;

                    for (const auto &stream : outputParams.getStreams()) {
                        const auto &name = stream.getLinkParameters().getOutputParameters().getOutputName();

                        for (const auto &output : this->_outputs) {
                            if (output.second.getName() == name) {
                                children.push_back(std::reinterpret_pointer_cast<SingleOutput>(output.first));
                                break;
                            }
                        }
                    }

                    this->_outputs.push_back({
                        std::make_shared<GroupOutput>(children), outputParams
                    });
                }
            }
        }

        this->_scteParams    = parameters.getScteParameters();
        this->_squeezeParams = parameters.getPredefinedSqueezes();

        this->_channel.configure(this->_scteParams);

        this->_blanker.configure(
            parameters.getBlackoutParameters(),
            parameters.getBlankingParameters()
        );

        this->_adInsertionParams = parameters.getAdInsertionParameters();

        this->_adBreakSwitcher.configure(this->_adInsertionParams);

        if (this->_adInsertionParams.isEnabled()) {
            if (this->_adInsertionParams.getOutputScte() == AdInsertionOutputScte::SUPPRESS) {
                this->_channel.setScteSuppressionPredicate([this]() {
                    return this->_adBreakSwitcher.isBreakOnAir();
                });
            }

            _warnOnOverlappingTriggers(
                this->_adInsertionParams.getTrigger(), parameters.getBlankingParameters(), "blanking"
            );

            _warnOnOverlappingTriggers(
                this->_adInsertionParams.getTrigger(), parameters.getBlackoutParameters(), "blackout"
            );
        }
    }

    // Blanking and ad insertion both act on the same cue. Configured for the same segmentation
    // types they would blank the channel and switch to the ad source at once, putting black ads
    // on air, so say so at startup rather than leaving it to be discovered on the first break.
    static void _warnOnOverlappingTriggers(
        const BlackoutParameters &adTrigger,
        const BlackoutParameters &other,
        const char *otherName
    ) {
        if (! other.isEnabled()) {
            return;
        }

        auto overlaps = [](const BlackoutCondition &a, const BlackoutCondition &b) {
            for (const auto &left : a.getSegments()) {
                for (const auto &right : b.getSegments()) {
                    if (left.getTypeOfBegin() == right.getTypeOfBegin()) {
                        return true;
                    }
                }
            }

            return false;
        };

        const auto &adConditions    = adTrigger.getConditions();
        const auto &otherConditions = other.getConditions();

        if (
            overlaps(adConditions.getSpliceInsertCondition(), otherConditions.getSpliceInsertCondition()) ||
            overlaps(adConditions.getTimeSignalCondition(),   otherConditions.getTimeSignalCondition())
        ) {
            WARN(("Ad insertion and {} are configured for overlapping segmentation types - "
                  "the channel will blank while the ad source is on air", otherName
            ));
        }
    }

    void cleanup() {
        this->_inputs.clear();
        this->_outputs.clear();

        this->_pipe = nullptr;
    }

    void start(std::function<void()> callback) {
        this->error         = utpl::Error::NO_ERROR;
        this->startCallback = [callback, this]() {
            this->_scheduler.start(this->_squeezeParams, this->_scteParams, &this->_channel);

            if (callback) {
                callback();
            }
        };
        
        if (! this->_switchState(this->currentState, State::PLAYING)) {
            this->startCallback();
        }
    }

    utpl::Error loop() {
        if (this->currentState != State::STOPPED) {
            DBG(("Starting transcoder engine main loop"));
            this->_pipe->loop();
            DBG(("Transcoder engine main loop has exited"));
        }
        
        return this->error;
    }

    void stop(std::function<void()> callback) {
        this->_scheduler.stop();

        this->stopCallback = [callback, this]() {
            if (callback) {
                callback();
            }
        };

#if defined(DOT_DUMP)
        GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN((*this->_pipe)->get()), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");
#endif

        if (! _switchStateToStop(StopReason::USER_ACTION)) {
            this->stopCallback();
        }
    }

    void interrupt(bool setError = false) {
        DBG(("Interrupting main loop execution"));

        if (setError) {
            this->error = utpl::Error::INTERRUPTED;
        }

        this->_pipe->interrupt();
    }

    // Hands the switcher the programme source and the secondary source it cuts to. Called once the
    // graph is playing, so both inputs exist and are attached to the connectors.
    void _bindAdBreakInputs() {
        if (! this->_adInsertionParams.isEnabled()) {
            return;
        }

        std::shared_ptr<Input> programmeInput;
        std::shared_ptr<Input> adInput;

        for (auto &inputIt : this->_inputs) {
            if (inputIt.second.getRole() == InputRole::SECONDARY) {
                if (! adInput) {
                    adInput = inputIt.first;
                }

            } else if (! programmeInput) {
                programmeInput = inputIt.first;
            }
        }

        if (! adInput) {
            WARN(("Ad insertion is enabled but no input has the 'secondary' role"));
        }

        this->_adBreakSwitcher.bindInputs(programmeInput, adInput);
    }

    // An input that is meant to resume where it left off must not be running while it is off air.
    // Freezing it here also leaves it prerolled, so the first break switches without a gap.
    void _freezeOffAirInputs() {
        auto currentInput = this->_channel.getCurrentInput();

        for (auto &inputIt : this->_inputs) {
            auto &input = inputIt.first;

            if (input == currentInput) {
                continue;
            }

            if (inputIt.second.getOffAirBehavior() != InputOffAirBehavior::PAUSE) {
                continue;
            }

            if (input->setPlaybackActive(false) == Error::NOT_SUPPORTED) {
                WARN(("Input '{}' is configured to pause off air but does not support it", input->getName()));
            }
        }
    }

    void addEventProvider(EventProvider *eventProvider) {
        this->_scheduler.addProvider(eventProvider);
    }

    void removeEventProvider(EventProvider *eventProvider) {
        this->_scheduler.removeProvider(eventProvider);
    }

    bool isAdBreakOnAir() const {
        return this->_adBreakSwitcher.isBreakOnAir();
    }

    utpl::transcoder::Channel &getChannel() {
        return this->_channel;
    }

    std::vector<std::pair<std::shared_ptr<Input>, InputParameters>>  _inputs;
    std::vector<std::pair<std::shared_ptr<Output>, OutputParameters>> _outputs;

    // Number of inputs still probing during the INPUT_ADD -> INPUT_PROBE transition.
    size_t _pendingProbes = 0;

    Channel         _channel;
    Blanker         _blanker;
    AdBreakSwitcher _adBreakSwitcher;

    AdInsertionParameters _adInsertionParams;

    EventScheduler _scheduler;

    PredefinedSqueezes _squeezeParams;
    ScteParameters     _scteParams;

    State currentState;
    State pendingState;
    State targetState;

    StopReason stopReason;
    utpl::Error error;

    std::function<void()> startCallback;
    std::function<void()> stopCallback;

    pipeline::PipePtr _pipe;

    static const std::map<State, State> _upMap;
    static const std::map<State, State> _downMap;
    static const std::map<std::pair<State, State>, std::function<void(Pimpl *)>> _transitionHandlers;
};

#define PIMPL_NS(x) transcoder::Engine::Pimpl::x


const std::map<PIMPL_NS(State), PIMPL_NS(State)>  PIMPL_NS(_upMap) = {
    { PIMPL_NS(State::STOPPED),         PIMPL_NS(State::INPUT_ADD) },
    { PIMPL_NS(State::INPUT_ADD),       PIMPL_NS(State::INPUT_PROBE) },
    { PIMPL_NS(State::INPUT_PROBE),     PIMPL_NS(State::CHANNEL_ADD) },
    { PIMPL_NS(State::CHANNEL_ADD),     PIMPL_NS(State::OUTPUT_ADD) },
    { PIMPL_NS(State::OUTPUT_ADD),      PIMPL_NS(State::CHANNEL_PREPARE) },
    { PIMPL_NS(State::CHANNEL_PREPARE), PIMPL_NS(State::PLAYING) },
};


const std::map<PIMPL_NS(State), PIMPL_NS(State)> PIMPL_NS(_downMap) = {
    { PIMPL_NS(State::PLAYING),       PIMPL_NS(State::PIPELINE_STOP) },
    { PIMPL_NS(State::PIPELINE_STOP), PIMPL_NS(State::CHANNEL_DEL) },
    { PIMPL_NS(State::CHANNEL_DEL),   PIMPL_NS(State::OUTPUT_DEL) },
    { PIMPL_NS(State::OUTPUT_DEL),    PIMPL_NS(State::INPUT_DEL) },
    { PIMPL_NS(State::INPUT_DEL),     PIMPL_NS(State::STOPPED) },

    { PIMPL_NS(State::INPUT_ADD),       PIMPL_NS(State::INPUT_DEL) },
    { PIMPL_NS(State::INPUT_PROBE),     PIMPL_NS(State::PIPELINE_STOP) },
    { PIMPL_NS(State::CHANNEL_ADD),     PIMPL_NS(State::PIPELINE_STOP) },
    { PIMPL_NS(State::OUTPUT_ADD),      PIMPL_NS(State::PIPELINE_STOP) },
    { PIMPL_NS(State::CHANNEL_PREPARE), PIMPL_NS(State::PIPELINE_STOP) },
};


const std::map<std::pair<PIMPL_NS(State), PIMPL_NS(State)>, std::function<void(transcoder::Engine::Pimpl *)>> PIMPL_NS(_transitionHandlers) = {
    {{ PIMPL_NS(State::STOPPED), PIMPL_NS(State::INPUT_ADD) }, [](transcoder::Engine::Pimpl *self) {
        for (auto &inputIt : self->_inputs) {
            auto &input  = inputIt.first;
            auto &params = inputIt.second;

            input->prepare(params);
        }

        self->_commitPendingState();
    }},

    {{ PIMPL_NS(State::INPUT_ADD), PIMPL_NS(State::INPUT_PROBE) }, [](transcoder::Engine::Pimpl *self) {
        self->_pipe->play([self](bool success) {
            // Every input must finish probing before the state machine may advance. A channel
            // with an ad break source has more than one, so the transition is committed by the
            // last probe to complete, not by each of them.
            self->_pendingProbes = self->_inputs.size();

            for (auto &inputIt : self->_inputs) {
                auto &input  = inputIt.first;
                auto &params = inputIt.second;

                self->_pipe->add(input->getNode());

                // FIXME: Check state, synchronize all inputs
                input->probe([self, &input](bool success) {
                    if (success) {
                        if (self->_pendingProbes > 0 && --self->_pendingProbes == 0) {
                            // Add in configuration order. Probes complete in whatever order the
                            // sources become ready - a local playlist is ready long before an SRT
                            // caller connects - and the channel treats its first input as the
                            // programme source, so completion order must not decide which one
                            // that is.
                            for (auto &entry : self->_inputs) {
                                self->_channel.addInput(entry.first);
                            }

                            self->_commitPendingState();
                        }

                    } else {
                        // Abandon the remaining probes: the pipeline is going down anyway.
                        self->_pendingProbes = 0;

                        self->_switchStateToStop(StopReason::PIPE_ERROR);
                    }
                });
            }
        });
    }},
    
    {{ PIMPL_NS(State::INPUT_PROBE), PIMPL_NS(State::CHANNEL_ADD) }, [](transcoder::Engine::Pimpl *self) {
        // Add outputs to channel
        for (auto &output : self->_outputs) {
            self->_channel.addOutput(output.first, output.second);
        };

        // Add channel to pipeline
        self->_pipe->add(self->_channel.getNode());

        for (auto &output : self->_outputs) {
            auto node = output.first->getNode();
            if (node) {
                self->_pipe->add(node);
            }
        }

        // Prepare channel and outputs
        self->_channel.prepare(self->_pipe);

        self->_commitPendingState();
    }},

    {{ PIMPL_NS(State::CHANNEL_ADD), PIMPL_NS(State::OUTPUT_ADD) }, [](transcoder::Engine::Pimpl *self) {
        self->_commitPendingState();
    }},

    {{ PIMPL_NS(State::OUTPUT_ADD), PIMPL_NS(State::CHANNEL_PREPARE) }, [](transcoder::Engine::Pimpl *self) {
        self->_channel.makeReady([self]() {
            self->_commitPendingState();
        });
    }},

    {{ PIMPL_NS(State::CHANNEL_PREPARE), PIMPL_NS(State::PLAYING) }, [](transcoder::Engine::Pimpl *self) {
        self->_pipe->play([self](bool success) {
            if (success) {
                self->_bindAdBreakInputs();
                self->_freezeOffAirInputs();
                // Outputs are added to a pipeline that is already running. An output
                // whose sink changes state asynchronously - splitmuxsink, which prerolls
                // its internal sink - is left behind in PAUSED when its ASYNC_DONE lands
                // after the parent bin has already settled: the pending PLAYING is never
                // pushed back down and the sink blocks in preroll, so nothing is written.
                // The pipeline is genuinely PLAYING here, so synchronizing is a no-op for
                // every output that already followed the transition.
                for (auto &outputIt : self->_outputs) {
                    auto node = outputIt.first->getNode();

                    if (node && ! node->syncStateWithParent()) {
                        WARN(("Unable to synchronize output state with the pipeline"));
                    }
                }

                self->_commitPendingState();

            } else {
                self->_switchStateToStop(StopReason::PIPE_ERROR);
            }
        });
    }},

    // ----------------------------------- Reverse

    {{ PIMPL_NS(State::PLAYING), PIMPL_NS(State::PIPELINE_STOP) }, [](transcoder::Engine::Pimpl *self) {
        self->_pipe->null([self](bool success) {
            self->_commitPendingState();
        });
    }},

    {{ PIMPL_NS(State::CHANNEL_PREPARE), PIMPL_NS(State::PIPELINE_STOP) }, [](transcoder::Engine::Pimpl *self) {
        self->_pipe->null([self](bool success) {
            self->_commitPendingState();
        });
    }},

    {{ PIMPL_NS(State::INPUT_ADD), PIMPL_NS(State::INPUT_DEL) }, [](transcoder::Engine::Pimpl *self) {
        self->_pipe->null([self](bool success) {
            self->_commitPendingState();
        });
    }},

    {{ PIMPL_NS(State::PIPELINE_STOP), PIMPL_NS(State::CHANNEL_DEL) }, [](transcoder::Engine::Pimpl *self) {
        auto channelNode = self->_channel.getNode();
        if (channelNode) {
            self->_pipe->remove(channelNode);

            self->_channel.cleanup();
        }

        self->_commitPendingState();
    }},

    {{ PIMPL_NS(State::CHANNEL_DEL), PIMPL_NS(State::OUTPUT_DEL) }, [](transcoder::Engine::Pimpl *self) {
        for (auto &outputIt : self->_outputs) {
            auto &output = outputIt.first;

            self->_pipe->remove(output->getNode());
            
            output->cleanup();
        }

        self->_commitPendingState();
    }},

    {{ PIMPL_NS(State::OUTPUT_DEL), PIMPL_NS(State::INPUT_DEL) }, [](transcoder::Engine::Pimpl *self) {
        for (auto &inputIt : self->_inputs) {
            auto &input = inputIt.first;

            self->_pipe->remove(input->getNode());

            input->cleanup();
        }

        self->_commitPendingState();
    }},

    {{ PIMPL_NS(State::INPUT_DEL), PIMPL_NS(State::STOPPED) }, [](transcoder::Engine::Pimpl *self) {
        self->_commitPendingState();
    }}
};


transcoder::Engine::Engine() {
    this->_self = std::make_unique<Pimpl>();

}


transcoder::Engine::~Engine() {
    this->cleanup();

    this->_self.reset();
}


void transcoder::Engine::prepare(const EngineParameters &parameters) {
    this->_self->prepare(parameters);
}


void transcoder::Engine::start(std::function<void()> callback) {
    this->_self->start(callback);
}


utpl::Error transcoder::Engine::loop() {
    return this->_self->loop();
}


void transcoder::Engine::stop(std::function<void()> callback) {
    this->_self->stop(callback);
}


void transcoder::Engine::cleanup() {
    this->_self->cleanup();
}


void transcoder::Engine::interrupt() {
    this->_self->interrupt(true);
}

void transcoder::Engine::addEventProvider(EventProvider *eventProvider) {
    this->_self->addEventProvider(eventProvider);
}

void transcoder::Engine::removeEventProvider(EventProvider *eventProvider) {
    this->_self->removeEventProvider(eventProvider);
}

bool transcoder::Engine::isAdBreakOnAir() const {
    return this->_self->isAdBreakOnAir();
}

utpl::transcoder::Channel &transcoder::Engine::getChannel() {
    return this->_self->getChannel();
}
