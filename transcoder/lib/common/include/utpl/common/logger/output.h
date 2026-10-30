#ifndef __UTPL_COMMON_LOGGER_OUTPUT_H__
#define __UTPL_COMMON_LOGGER_OUTPUT_H__

#include <string>

namespace utpl {
    enum class LoggerOutputType {
        NONE,

        STDOUT,
        STDERR,
        FILE
    };

    class LoggerOutput {
        public:
            LoggerOutput() {
                this->_type    = LoggerOutputType::NONE;
                this->_enabled = false;
                this->_color   = false;
            }

            LoggerOutputType getType() const {
                return this->_type;
            }

            LoggerOutput &setType(LoggerOutputType type) {
                this->_type = type;

                return *this;
            }

            const std::string &getPath() const {
                return this->_path;
            }

            LoggerOutput &setPath(const std::string &path) {
                this->_path = path;

                return *this;
            }

            bool isEnabled() const {
                return this->_enabled;
            }

            LoggerOutput &setEnabled(bool enabled) {
                this->_enabled = enabled;

                return *this;
            }

            bool isColor() const {
                return this->_color;
            }

            LoggerOutput setColor(bool color) {
                this->_color = color;

                return *this;
            }

        private:
            LoggerOutputType  _type;
            std::string       _path;
            bool              _enabled;
            bool              _color;
    };
}

#endif /* __UTPL_COMMON_LOGGER_OUTPUT_H__ */