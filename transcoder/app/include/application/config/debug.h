#ifndef __APPLICATION_CONFIG_DEBUG_H__
#define __APPLICATION_CONFIG_DEBUG_H__

#include <string>

#include "utpl/common/types.h"
#include "utpl/common/logger/parameters.h"

namespace app {
    class DebugParameters {
        public:
            DebugParameters() {
            }

            const utpl::LoggerParameters &getTranscoderLogger() const {
                return this->_transcoderLogger;
            }

            void setTranscoderLogger(const utpl::LoggerParameters &logger) {
                this->_transcoderLogger = logger;
            }

            const utpl::LoggerParameters &getGstreamerLogger() const {
                return this->_gstreamerLogger;
            }

            void setGstreamerLogger(const utpl::LoggerParameters &logger) {
                this->_gstreamerLogger = logger;
            }

        private:
            std::string _dbPath;

            utpl::LoggerParameters _transcoderLogger;
            utpl::LoggerParameters _gstreamerLogger;
    };
}

#endif /* __APPLICATION_CONFIG_DEBUG_H__ */