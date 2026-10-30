#ifndef __UTPL_COMMON_LOGGER_PARAMETERS_H__
#define __UTPL_COMMON_LOGGER_PARAMETERS_H__

#include <vector>

#include "utpl/common/logger/level.h"
#include "utpl/common/logger/output.h"

namespace utpl {
    class LoggerParameters {
        public:
            LoggerParameters() {
                this->_level        = LogLevel::NONE;
                this->_basicPattern = true;
            }

            bool isBasicPattern() const {
                return this->_basicPattern;
            }

            LoggerParameters &setBasicPattern(bool basic) {
                this->_basicPattern = basic;

                return *this;
            }

            LogLevel getLevel() const {
                return this->_level;
            }

            LoggerParameters &setLevel(LogLevel level) {
                this->_level = level;

                return *this;
            }

            LoggerParameters &addOutput(const LoggerOutput &output) {
                this->_outputs.push_back(output);

                return *this;
            }

            const std::vector<LoggerOutput> &getOutputs() const {
                return this->_outputs;
            }

        private:
            bool                      _basicPattern;
            LogLevel                  _level;
            std::vector<LoggerOutput> _outputs;
    };
}

#endif /* __UTPL_COMMON_LOGGER_PARAMETERS_H__ */