#include <chrono>
#include <iomanip>

#include "utpl/common/time.h"
#include "utpl/common/debug.h"
#include "utpl/common/types/exception.h"

using namespace utpl;

Timestamp Time::getUTCTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

Timestamp Time::getMonotonicTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string Time::toSqliteDateTime(Timestamp timestamp) {
    std::stringstream stream;

    stream << toUTCString(timestamp) << ".";
    stream << std::setfill('0') << std::setw(3) << (timestamp % 1000);

    return stream.str();
}

utpl::Timestamp Time::fromSqliteDateTime(const std::string &dateTime) {
    utpl::Timestamp ret = 0;

    if (! dateTime.empty()) {
        std::tm parsed;

        auto *remaining = strptime(dateTime.c_str(), "%Y-%m-%d %T", &parsed);
        if (remaining == nullptr) {
            throw utpl::InvalidArgumentException("Unable to parse date and time from string '" + dateTime + "', which should be formatted with SQLite datetime() format");
        }

        ret  = timegm(&parsed);
        ret *= 1000;

        if (*remaining == '.') {
            std::string r(remaining + 1);

            // From sqlite documentation:
            // "Exactly three digits are shown in the examples because only the first three digits are 
            // significant to the result, but the input string can have fewer or more than three 
            // digits and the date/time functions will still operate correctly"
            if (r.length() > 3) {
                r = r.substr(0, 3);
            }

            ret += std::stol(r);
        }  
    }

    return ret;
}

std::string Time::toUTCString(Timestamp timestamp) {
    std::stringstream stream;
    
    {
        auto tp = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::time_point(std::chrono::milliseconds(timestamp))
        );

        stream << std::put_time(std::gmtime(&tp), "%Y-%m-%d %T");
    }

    return stream.str();
}

std::string Time::toEsamDateTime(Timestamp timestamp) {
    std::stringstream stream;
    
    {
        auto tp = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::time_point(std::chrono::milliseconds(timestamp))
        );

        stream << std::put_time(std::gmtime(&tp), "%Y-%m-%dT%H:%M:%S");
        stream << "." << std::setfill('0') << std::setw(3) << (timestamp % 1000);
        stream << "Z";
    }

    return stream.str();
}

std::string Time::toLocalString(Timestamp timestamp) {
    std::stringstream stream;
    
    {
        auto tp = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::time_point(std::chrono::milliseconds(timestamp))
        );

        stream << std::put_time(std::localtime(&tp), "%Y-%m-%d %T");
    }

    return stream.str();
}

utpl::Timestamp Time::fromIso8601(const std::string &dateTime) {
    utpl::Timestamp ret = 0;

    try {
        std::tm tm = {};

        std::istringstream ss(dateTime);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        if (ss.fail()) {
            throw std::runtime_error("Failed to parse ISO 8601 date string");
        }

        // handle optional fractional seconds
        if (ss.peek() == '.') {
            double fractional_seconds;
            ss >> fractional_seconds;
            ret += static_cast<utpl::Timestamp>(fractional_seconds * 1000);
        }

        // to miliseconds UTC
        auto time_point = std::chrono::system_clock::from_time_t(timegm(&tm));
        ret += std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch()).count();

    } catch (const std::exception& e) {
        throw utpl::Exception("Failed to parse ISO 8601 date string");
    }

    return ret;
}