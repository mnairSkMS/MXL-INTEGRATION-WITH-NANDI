#ifndef __APPLICATION_CONFIG_TRANSCODER_H__
#define __APPLICATION_CONFIG_TRANSCODER_H__

#include "utpl/transcoder/engine.h"

namespace app {
    class TranscoderParameters {
        public:
            const utpl::transcoder::EngineParameters &getEngineParameters() const {
                return this->_engineParameters;
            }

            void setEngineParameters(const utpl::transcoder::EngineParameters &parameters) {
                this->_engineParameters = parameters;
            }

        private:
            utpl::transcoder::EngineParameters _engineParameters;
    };
}

#endif /* __APPLICATION_CONFIG_TRANSCODER_H__ */