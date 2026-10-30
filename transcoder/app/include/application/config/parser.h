#ifndef __APPLICATION_CONFIG_PARSER_H__
#define __APPLICATION_CONFIG_PARSER_H__

#include "utpl/common/utils/class.h"
#include "application/config.h"


namespace app {
    class ConfigParser {
        UTPL_CLASS_INTERFACE(ConfigParser){}

        public:
            virtual ApplicationConfig parse(const std::string &path) = 0;

    };

    class DebugConfigParser {
        UTPL_CLASS_INTERFACE(DebugConfigParser){}

        public:
            virtual DebugConfig parse(const std::string &path) = 0;

    };
}

#endif /* __APPLICATION_CONFIG_PARSER_H__ */