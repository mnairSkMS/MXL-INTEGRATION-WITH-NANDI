#ifndef __APPLICATION_CONFIG_PARSER_JSON_PARSER_H__
#define __APPLICATION_CONFIG_PARSER_JSON_PARSER_H__

#include <nlohmann/json.hpp>

#include "utpl/common/utils/semver.hpp"

#include "application/config/parser.h"

namespace app {
    class BaseMultiversionJsonParser {
        public:
            virtual nlohmann::json begin(const std::string &path);

        protected:
            const semver::version &getVersion() const {
                return this->_version;
            }

        private:
            semver::version _version;
    };

    class MultiVersionConfigJsonParser : public BaseMultiversionJsonParser, public ConfigParser {
        public:
            virtual ApplicationConfig parse(const std::string &path) override;
    };

    class MultiVersionDebugConfigJsonParser : public BaseMultiversionJsonParser, public DebugConfigParser {
        public:
            DebugConfig parse(const std::string &path) override;
    };
}

#endif /* __APPLICATION_CONFIG_PARSER_JSON_PARSER_H__ */