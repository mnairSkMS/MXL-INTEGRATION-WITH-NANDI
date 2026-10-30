#ifndef __APPLICATION_CONFIG_H__
#define __APPLICATION_CONFIG_H__

#include "application/config/transcoder.h"
#include "application/config/application.h"
#include "application/config/debug.h"

namespace app {
    class ApplicationConfig {
        public:
            const TranscoderParameters &getTranscoderParameters() const {
                return this->_transcoderParams;
            }

            void setTranscoderParameters(const TranscoderParameters &params)  {
                this->_transcoderParams = params;
            }

            const ApplicationParameters &getApplicationParameters() const {
                return this->_applicationParams;
            }

            void setApplicationParameters(const ApplicationParameters &params) {
                this->_applicationParams = params;
            }

        private:
            TranscoderParameters  _transcoderParams;
            ApplicationParameters _applicationParams;
    };


    class DebugConfig {
        public:
            const DebugParameters &getDebugParameters() const {
                return this->_debugParams;
            }

            void setDebugParameters(const DebugParameters &params) {
                this->_debugParams = params;
            }

        private:
            DebugParameters  _debugParams;
    };
}

#endif /* __APPLICATION_CONFIG_H__ */