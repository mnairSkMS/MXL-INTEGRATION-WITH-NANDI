#ifndef __UTPL_COMMON_LOGGER_LEVEL_H__
#define __UTPL_COMMON_LOGGER_LEVEL_H__

#include <string>

namespace utpl {
    enum LogLevel {
        UNKNOWN,

        NONE,
        TRACE,
        DEBUG,
        LOG,
        WARNING,
        ERROR,
        ALWAYS
    };
      
    static const std::string &toString(LogLevel level);

    static LogLevel fromString(const std::string &level);
}

#endif /* __UTPL_COMMON_LOGGER_LEVEL_H__ */