#ifndef __UTPL_TRANSCODER_ENGINE_H__
#define __UTPL_TRANSCODER_ENGINE_H__

#include "utpl/common/utils/class.h"

#include "utpl/pipeline/pipe.h"

#include "utpl/transcoder/types.h"
#include "utpl/transcoder/input.h"
#include "utpl/transcoder/output.h"
#include "utpl/transcoder/blanker.h"
#include "utpl/transcoder/adBreak.h"
#include "utpl/transcoder/channel.h"
#include "utpl/transcoder/event/scheduler.h"

namespace utpl {
    namespace transcoder {
        class EngineParameters {
            public:
                EngineParameters() {
                    // A channel always has a programme input. Keeping one default-constructed
                    // entry preserves the single-input accessors below as plain references.
                    this->_inputs.resize(1);
                }

                // Single-input accessors. They address the programme input (index 0) so that
                // existing callers, configurations and tests are unaffected by multi-input support.
                void setInputParameters(const InputParameters &params) {
                    this->_inputs[0] = params;
                }

                InputParameters &getInputParameters() {
                    return this->_inputs[0];
                }

                const InputParameters &getInputParameters() const {
                    return this->_inputs[0];
                }

                // Multi-input accessors. The first entry is always the programme input; further
                // entries are additional sources the channel can switch to, such as an ad break
                // playlist.
                void addInputParameters(const InputParameters &params) {
                    this->_inputs.push_back(params);
                }

                std::vector<InputParameters> &getAllInputParameters() {
                    return this->_inputs;
                }

                const std::vector<InputParameters> &getAllInputParameters() const {
                    return this->_inputs;
                }

                void addOutputParameters(const OutputParameters &params) {
                    this->_outputsParameters.push_back(params);
                }

                std::vector<OutputParameters> &getOutputParameters() {
                    return this->_outputsParameters;
                }

                const std::vector<OutputParameters> &getOutputParameters() const {
                    return this->_outputsParameters;
                }

                void setPredefinedSqueezes(const PredefinedSqueezes &params) {
                    this->_predefinedSqueezes = params;
                }

                PredefinedSqueezes &getPredefinedSqueezes() {
                    return this->_predefinedSqueezes;
                }

                const PredefinedSqueezes &getPredefinedSqueezes() const {
                    return this->_predefinedSqueezes;
                }

                void setScteParameters(const ScteParameters &params) {
                    this->_scte = params;
                }

                const ScteParameters &getScteParameters() const {
                    return this->_scte;
                }

                void setBlackoutParameters(const BlackoutParameters &params) {
                    this->_blackout = params;
                }

                const BlackoutParameters &getBlackoutParameters() const {
                    return this->_blackout;
                }

                void setBlankingParameters(const BlackoutParameters &params) {
                    this->_blanking = params;
                }

                const BlackoutParameters &getBlankingParameters() const {
                    return this->_blanking;
                }

                void setAdInsertionParameters(const AdInsertionParameters &params) {
                    this->_adInsertion = params;
                }

                const AdInsertionParameters &getAdInsertionParameters() const {
                    return this->_adInsertion;
                }

            private:
                std::vector<InputParameters> _inputs;
                PredefinedSqueezes _predefinedSqueezes;
                ScteParameters     _scte;
                BlackoutParameters    _blackout;
                BlackoutParameters    _blanking;
                AdInsertionParameters _adInsertion;

                std::vector<OutputParameters> _outputsParameters;
        };

        class Engine {
            UTPL_CLASS_NOCOPY(Engine);

            public:
                Engine();
                ~Engine();

                void prepare(const EngineParameters &parameters);
                void start(std::function<void()> callback = {});
                utpl::Error loop();
                void stop(std::function<void()> callback = {});
                void cleanup();

                void interrupt();

                void addEventProvider(EventProvider *eventProvider);
                void removeEventProvider(EventProvider *eventProvider);

                utpl::transcoder::Channel &getChannel();

                // True while a locally inserted ad break is on air. The control plane reports it
                // so an operator UI can follow what is actually playing out rather than assuming
                // its own last click still holds - a break also ends on maxBreakDuration, which
                // no client can predict.
                bool isAdBreakOnAir() const;

            private:
                class Pimpl;

                std::unique_ptr<Pimpl> _self;
        };
    }
}

#endif /* __UTPL_TRANSCODER_ENGINE_H__ */