#include "utpl/common/debug.h"

#include "utpl/pipeline/utils.h"

#include "utpl/transcoder/input/single.h"
#include "utpl/transcoder/input/standard.h"

#include "utpl/logger.h"

using namespace utpl;

static auto &logger = Logger::getLogger(UTPL_LOGGER_NAME_INPUT);

#define SWITCH_NAME(index) "stream-switch-passthrough-" + std::to_string(index)
#define DECODE_SWITCH_NAME(index) "stream-switch-decode-" + std::to_string(index)

class FallbackStream : public transcoder::BaseInputStream {
    public:
        using PreLinkCallback  = std::function<void(bool linkPasstrough, pipeline::PadPtr &passthroughPad, bool linkDecoded, pipeline::PadPtr &decodedPad)>;
        using PostLinkCallback = std::function<void()>;

    public:
        FallbackStream(
            pipeline::BinPtr bin, 
            const transcoder::StreamParameters &streamParameters, 
            PreLinkCallback preLinkCallback, 
            PostLinkCallback postLinkCallback
        ) : BaseInputStream(bin) {
            this->_preLinkCallback  = preLinkCallback;
            this->_postLinkCallback = postLinkCallback;

            this->setStreamParameters(streamParameters);
        }

        virtual pipeline::PadPtr getSrcPad(bool passthrough) override {
            if (passthrough) {
                this->_linkPassthrough = true;

            } else {
                this->_linkDecoded = true;
            }

            return nullptr;
        }

        void link() override {
            this->_linkDecoded     = false;
            this->_linkPassthrough = false;

            this->BaseInputStream::link();

            DBG(("Linking standard input pads, decoded: {}, passthrough: {} to current stream of type: {}", this->_linkDecoded, this->_linkPassthrough, this->getStreamParameters().toString()));

            {
                pipeline::PadPtr passthroughPad;
                pipeline::PadPtr decodedPad;

                this->_preLinkCallback(this->_linkPassthrough, passthroughPad, this->_linkDecoded, decodedPad);

                if (passthroughPad) {
                    this->linkSrcPad(passthroughPad, true);
                }

                if (decodedPad) {
                    this->linkSrcPad(decodedPad, false);
                }

                this->_postLinkCallback();
            }
        }

    private:
        pipeline::BinPtr _bin;

        bool _linkPassthrough;
        bool _linkDecoded;

        PreLinkCallback  _preLinkCallback;
        PostLinkCallback _postLinkCallback;
};

transcoder::StandardInput::StandardInput() {
    this->_currentInput = nullptr;
    this->_prepared     = false;
}

transcoder::StandardInput::~StandardInput() {
    this->cleanup();
}

void transcoder::StandardInput::prepare(const InputParameters &parameters) {
    this->cleanup();

    this->_bin = pipeline::Bin::create();

    InputParameters singleParams = parameters;

    for (size_t i = 0; i < parameters.getUris().size(); i++) {
        auto input = std::make_shared<SingleInput>(true, "standard-input-" + std::to_string(i));

        {
            singleParams.clearUris();
            singleParams.addUri(parameters.getUris().at(i));

            input->prepare(singleParams);
        }

        this->_inputs.push_back(input);
    }
}

void transcoder::StandardInput::probe(ProbeCallback callback) {
    this->_probeCallback = callback;

    this->_bin->pause([this](bool success) {
        for (size_t inputIndex = 0; inputIndex < this->_inputs.size(); inputIndex++) {
            auto &input = this->_inputs[inputIndex];

            input->onStateChanged([input, this](InputState state) {
                ProbedInput *probedInput = nullptr;

                std::lock_guard lock(this->_mutex);

                for (auto &i : this->_probedInputs) {
                    if (i->input.get() == input.get()) {
                        probedInput = i.get();
                        break;
                    }
                }

                if (probedInput) {
                    switch (state) {
                        case InputState::BUFFERED:
                            probedInput->buffered = true;
                            break;

                        case InputState::BUFFERING:
                            probedInput->buffered = false;
                            if (probedInput == this->_currentInput) {
                                this->_currentInput = nullptr;
                            }
                            break;

                        default:
                            break;
                    }
                }

                {
                    bool doSwitch = false;

                    if (this->_currentInput == nullptr) {
                        doSwitch = true;

                    } else if (! this->_currentInput->buffered) {
                        doSwitch = true;
                    }

                    if (doSwitch) {
                        int inputIndex = 0;

                        for (auto &i : this->_probedInputs) {
                            if (i->valid && i->buffered) {
                                logger.log("Switching Standard input source to redundant target location {} due to data underflow, URI: {}", 
                                    inputIndex, i->input->getInputParameters().getUris()[0]
                                );

                                _switchStream(i.get());
                                break;
                            }

                            inputIndex++;
                        }
                    }
                }
            });

            input->onError([input, this](const std::string &message) {
                if (this->_currentInput) {
                    if (this->_currentInput->input.get() == input.get()) {
                        if (this->_errorCallback) {
                            return this->_errorCallback(message);
                        }

                    } else {
                        DBG(("Ignoring error message from secondary input"));
                    }

                } else {
                    WARN(("No active input set - ignoring error message"));
                }

                return false;
            });

            input->onScteData([input, this](const Scte35Data &scteData) {
                if (this->_currentInput) {
                    if (this->_currentInput->input.get() == input.get()) {
                        if (this->_scteCallback) {
                            this->_scteCallback(scteData);
                        }
                    } else {
                        DBG(("Ignoring SCTE data from secondary input"));
                    }

                } else {
                    WARN(("No active input set - discarding SCTE data"));
                }
            });

            {
                auto inputNode = input->getNode();

                switch (input->getProtocol()) {
                    case Protocol::UDP:
                    case Protocol::SRT:
                    case Protocol::RTMP:
                    case Protocol::HTTP:
                    case Protocol::HLS:
                    case Protocol::DASH:
                        inputNode->setProperty<gboolean>("async-handling", true);
                        break;

                    default:
                        break;
                }

                inputNode->lockState(true);

                this->_bin->add(inputNode);
            }

            DBG(("Setting probe callback on input {}", (void *) input.get()));

            input->probe([this, input, inputIndex](bool success) {
                ProbeCallback targetCallback;
                bool          targetSuccess;

                bool isInputCorrect = false;

                {
                    std::lock_guard guard(this->_mutex);

                    DBG(("Probe callback wrapped executed on input: {}, success: {}", (void *) input.get(), success));

                    if (success) {
                        isInputCorrect = true;
                        
                        if (this->_probedInputs.empty()) {
                            size_t streamsCount = input->getStreams().size();

                            for (size_t streamIndex = 0; streamIndex < streamsCount; streamIndex++) {
                                auto &stream = input->getStreams()[streamIndex];

                                this->_streams.push_back(std::make_shared<FallbackStream>(
                                    this->_bin,
                                    stream->getStreamParameters(),
                                    // Pre
                                    [this, stream, streamIndex](
                                        bool linkPasstrough, pipeline::PadPtr &passthroughPad, bool linkDecoded, pipeline::PadPtr &decodedPad
                                    ) {
                                        std::lock_guard guard(this->_mutex);

                                        DBG(("Pre-link callback called from standard input stream of type: {}, linkPassthrough: {}, linkDecoded: {}", 
                                            stream->getStreamParameters().toString(), linkPasstrough, linkDecoded
                                        ));

                                        if (linkPasstrough) {
                                            passthroughPad = this->_createFlow(streamIndex, false);
                                        }

                                        if (linkDecoded) {
                                            decodedPad = this->_createFlow(streamIndex, true);
                                        }
                                    },
                                    // Post
                                    [this, streamIndex, stream]() {
                                        std::vector<std::pair<bool, std::string>> toLink = {
                                            { true,  { SWITCH_NAME(streamIndex) }},
                                            { false, { DECODE_SWITCH_NAME(streamIndex) }}
                                        };
                    
                                        DBG(("Post-link callback called from standard input stream of type: {}", stream->getStreamParameters().toString()));

                                        for (const auto &p : toLink) {
                                            auto fallback = this->_bin->get(p.second);
                                            if (fallback) {
                                                auto fallbackPad = fallback->requestPad("sink_%u");

                                                stream->requestPad(p.first)->link(
                                                    fallbackPad
                                                );
                                            }
                                        }
                    
                                        stream->link();
                                    }
                                ));
                            }

                        } else {
                            auto &firstInput = this->_inputs[0];

                            // Verify parameters with other stream
                            logger.log("Verifying other input streams match main input streams");

                            if (
                                firstInput->isLive()            != input->isLive() ||
                                firstInput->getStreams().size() != input->getStreams().size()
                            ) {
                                isInputCorrect = false;
                            }

                            if (isInputCorrect) {
                                const auto &streams = firstInput->getStreams();
                                
                                for (size_t i = 0; i < streams.size(); i++) {
                                    auto currentStreamParams = streams[i]->getStreamParameters();
                                    auto inputStreamParams   = input->getStreams()[i]->getStreamParameters();

                                    if (currentStreamParams != inputStreamParams) {
                                        isInputCorrect = false;
                                        break;
                                    }
                                }
                            }

                            if (! isInputCorrect) {
                                logger.warning("Redundant input has different parameters. Ignoring this input.");

                                this->_bin->remove(input->getNode());

                                g_idle_add(G_SOURCE_FUNC(_destroyInputAsync), input.get());

                            } else {
                                logger.log("New input has the same parameters as the current main input, attaching it to the fallback source.");

                                size_t streamIndex = 0;
                                for (auto &stream : input->getStreams()) {
                                    std::vector<std::pair<bool, std::string>> toLink = {
                                        { true,  { SWITCH_NAME(streamIndex) }},
                                        { false, { DECODE_SWITCH_NAME(streamIndex) }}
                                    };

                                    for (const auto &p : toLink) {
                                        auto fallback = this->_bin->get(p.second);

                                        if (fallback) {
                                            auto fallbackPad = fallback->requestPad("sink_%u");

                                            stream->requestPad(p.first)->link(
                                                fallbackPad
                                            );
                                        }
                                    }

                                    stream->link();

                                    streamIndex++;
                                }
                            }
                        }

                        this->_probedInputs.push_back(std::make_unique<ProbedInput>(input, isInputCorrect));

                    } else {
                        this->_probedInputs.push_back(std::make_unique<ProbedInput>(input, false));
                    }

                    // Start if any input was probed successfully
                    if (this->_probeCallback) {
                        size_t correctlyProbedCount = 0;
                        size_t totalProbedCount     = 0;

                        for (auto &input : this->_probedInputs) {
                            if (input->valid) {
                                correctlyProbedCount++;
                            }

                            totalProbedCount++;
                        }

                        targetSuccess = correctlyProbedCount > 0;

                        if (targetSuccess || (totalProbedCount == this->_inputs.size())) {
                            LOG(("Probed {}/{} inputs, calling callback with {}", this->_probedInputs.size(), this->_inputs.size(), targetSuccess ? "success" : "failure"));

                            targetCallback = this->_probeCallback;

                            this->_probeCallback = nullptr;
                        }
                    }

                    if (targetCallback) {
                        targetCallback(targetSuccess);
                    }
                }

                if (isInputCorrect) {
                    input->getNode()->lockState(false);
                    input->getNode()->syncWithParent();
                }
            });
        }
    });
}

void transcoder::StandardInput::_switchStream(ProbedInput *input) {
    if (input != this->_currentInput) {
        int inputIndex = -1;

        for (auto &i : this->_probedInputs) {
            inputIndex++;

            if (i.get() == input) {
                break;
            }
        }

        DBG(("Switching current input from '{}' to '{}', input index: {}", this->_currentInput ? this->_currentInput->input->getName() : "<null>", input ? input->input->getName() : "<null>", inputIndex));

        auto streamCount = input->input->getStreams().size();

        for (int streamIndex = 0; streamIndex < streamCount; streamIndex++) {
            std::vector<std::string> switchNames = {
                { SWITCH_NAME(streamIndex) },
                { DECODE_SWITCH_NAME(streamIndex) }
            };

            for (auto &switchName : switchNames) {
                auto selector = this->_bin->get(switchName);
                if (selector) {
                    auto pads = selector->getPads(false);

                    if (! pads.empty()) {
                        selector->setProperty<GstPad *>("active-pad", 
                            (*pads[inputIndex])->get()
                        );
                    }
                }
            }
        }

        this->_currentInput = input;
    }
}

void transcoder::StandardInput::cleanup() {
    for (auto &input : this->_inputs) {
        input->cleanup();
    }

    this->_probedInputs.clear();
    this->_inputs.clear();

    this->_bin          = nullptr;
    this->_prepared     = false;
    this->_currentInput = nullptr;
}

const std::vector<std::shared_ptr<transcoder::InputStream>> &transcoder::StandardInput::getStreams() const {
    return this->_streams;
}

const transcoder::InputParameters &transcoder::StandardInput::getInputParameters() const {
    return this->_currentInput->input->getInputParameters();
}

void transcoder::StandardInput::onScteData(ScteDataCallback callback) {
    this->_scteCallback = callback;
}

void transcoder::StandardInput::onStateChanged(StateChangedCallback callback) {
    this->_stateChangeCallback = callback;
}

void transcoder::StandardInput::onError(ErrorCallback callback) {
    this->_errorCallback = callback;
}

void utpl::transcoder::StandardInput::onStreamsChanged(StreamsChangedCallback callback) {

}

pipeline::ElementPtr transcoder::StandardInput::getNode()
{
    return this->_bin;
}

bool transcoder::StandardInput::isLive() const {
    for (const auto &input : this->_inputs) {
        if (input->isLive()) {
            return true;
        }
    }

    return false;
}

Protocol transcoder::StandardInput::getProtocol() const {
    if (! this->_inputs.empty()) {
        return this->_inputs.rbegin()->get()->getProtocol();
    }

    return Protocol::UNKNOWN;
}

std::string transcoder::StandardInput::getName() const {
    if (this->_bin) {
        return this->_bin->getName();
    }

    return std::string();
}

GstClockTime utpl::transcoder::StandardInput::getDuration() const {
    if (this->_currentInput) {
        return this->_currentInput->input->getDuration();
    }

    return GST_CLOCK_TIME_NONE;
}

const std::string &transcoder::StandardInput::getId() const {
    return this->_id.getString();
}

transcoder::InputStatus transcoder::StandardInput::getStatus() {
    transcoder::InputStatus ret;

    for (auto &input : this->_inputs) {
        ret.addTransport(
            input->getId(),
            input->getInputParameters().getUris()[0],
            input->getStatus().getTransportsStatus()[0].getState()
        );
    }
    
    {
        std::lock_guard lock(this->_mutex);

        if (this->_currentInput) {
            ret.setCurrentTransportId(this->_currentInput->input->getId());
        }
    }

    return ret;
}

Error transcoder::StandardInput::setCurrentStream(const std::string &streamId) {
    Error ret = Error::NO_ERROR;

    if (streamId.empty()) {
        logger.warning("Empty stream ID provided - ignoring the request");

        ret = Error::INVALID_ARGUMENT;

    } else {
        std::lock_guard lock(this->_mutex);

        {
            ProbedInput *newInput = nullptr;

            for (auto &i : this->_probedInputs) {
                if (i->input->getId() == streamId) {
                    newInput = i.get();
                    break;
                }
            }

            if (! newInput) {
                ret = Error::INVALID_ARGUMENT;

                for (auto &i : this->_inputs) {
                    if (i->getId() == streamId) {
                        logger.warning("Requested stream {} has not been probed yet and cannot be set as active - ignoring request", streamId);

                        ret = Error::INVALID_STATE;
                    }
                }

            } else {
                if (! newInput->valid) {
                    logger.warning("Requested stream {} is in an invalid state and cannot be set as active - ignoring request", streamId);

                    ret = Error::INVALID_STATE;

                } else {
                    if (newInput == this->_currentInput) {
                        logger.log("Requested stream {} is already active - ignoring request", streamId);

                    } else {
                        logger.log("Switching current stream to {} on input {}", streamId, this->getId());

                        _switchStream(newInput);
                    }
                }
            }
        }
    }

    return ret;
}

static pipeline::ElementPtr _createQueue(pipeline::BinPtr &bin, const std::string &name = std::string()) {
    return pipeline::utils::createQueue(bin, name, 16, 0, 0);
}

pipeline::PadPtr transcoder::StandardInput::_createFlow(int streamIndex, bool decoded) {
    auto switchName = decoded ? DECODE_SWITCH_NAME(streamIndex) : SWITCH_NAME(streamIndex);

    auto fallback = this->_bin->get(switchName);
    if (fallback) {
        WARN(("Already prepared! {}", (void*) fallback.get()));

    } else {
        DBG(("Creating queue and switch for stream {}, passthrough {}", streamIndex, ! decoded));

        {
            pipeline::ElementPtr splitter;
            pipeline::ElementPtr fallbackQueue;
            pipeline::ElementPtr clockSync;

            fallback      = this->_bin->add(pipeline::Element::make(switchName, "input-selector"));
            clockSync     = this->_bin->add(pipeline::Element::make("clocksync"));
            fallbackQueue = _createQueue(this->_bin);

            fallback->link(clockSync);
            clockSync->link(fallbackQueue);

            return fallbackQueue->getPad("src");
        }
    }

    // FIXME: return pad of existing element!
    return nullptr;
}

void transcoder::StandardInput::_destroyInputAsync(Input *input) {
    input->getNode()->null();
    input->cleanup();
}
