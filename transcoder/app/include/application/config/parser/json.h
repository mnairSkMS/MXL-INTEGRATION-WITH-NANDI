#ifndef __APPLICATION_CONFIG_PARSER_JSON_H__
#define __APPLICATION_CONFIG_PARSER_JSON_H__

#include "application/config/parser.h"

namespace app {
    class JsonConfigParserFactory {
        UTPL_CLASS_NOBUILD_NOCOPY(JsonConfigParserFactory);

        public:
            static std::unique_ptr<ConfigParser> createParser(const std::string &path);
    };

    class JsonDebugConfigParserFactory {
        UTPL_CLASS_NOBUILD_NOCOPY(JsonDebugConfigParserFactory);

        public:
            static std::unique_ptr<DebugConfigParser> createParser(const std::string &path);
    };
}

#endif /* __APPLICATION_CONFIG_PARSER_JSON_H__ */