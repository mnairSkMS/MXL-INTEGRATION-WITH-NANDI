#ifndef __UTPL_COMMON_TIME_H__
#define __UTPL_COMMON_TIME_H__

#include <cstdint>
#include <string>

#include "utpl/common/utils/class.h"

namespace utpl {
    using Timestamp = int64_t;

    class Time {
        UTPL_CLASS_NOBUILD_NOCOPY(Time);
        
        public:
            static Timestamp getUTCTimestamp();
            static Timestamp getMonotonicTimestamp();

            static std::string toSqliteDateTime(Timestamp timestamp);
            static Timestamp fromSqliteDateTime(const std::string &dateTime);
            
            static std::string toUTCString(Timestamp timestamp);
            static std::string toLocalString(Timestamp timestamp);

            static std::string toEsamDateTime(Timestamp timestamp);
            
            static Timestamp fromIso8601(const std::string &dateTime);

        public:
            using days    = std::chrono::duration<Timestamp, std::ratio<24*60*60>>;
            using seconds = std::chrono::duration<Timestamp, std::ratio<1>>;
    };
}

#endif /* __UTPL_COMMON_TIME_H__ */