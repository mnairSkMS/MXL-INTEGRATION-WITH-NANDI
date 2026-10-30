#ifndef __UTPL_COMMON_LOGGER_H__
#define __UTPL_COMMON_LOGGER_H__

#include <string>
#include <memory>
#include <set>
#include <map>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/bin_to_hex.h>

#include "utpl/common/utils/class.h"
#include "utpl/common/types.h"

#include "utpl/common/logger/parameters.h"

namespace utpl {
    class Logger {
        UTPL_CLASS_NOBUILD_NOCOPY(Logger);

        public:
            virtual ~Logger();

        private:
            Logger(const std::string &name);

        public:
            static Logger &getLogger(const std::string &name = std::string());

        public:
            LoggerParameters getParameters() const;
            Logger &setParameters(const LoggerParameters &parameters);

            template <typename... Args>
            static auto toHex(Args&&... args) -> decltype(spdlog::to_hex(std::forward<Args>(args)...)) {
                return spdlog::to_hex(std::forward<Args>(args)...);
            }

            template<typename... Args>
            void report(const char *fileName, const char *funcName, int line, LogLevel level, spdlog::format_string_t<Args...> fmt, Args &&...args) {
                if (level != LogLevel::NONE) {
                    this->_logger->log(spdlog::source_loc{ fileName, line, funcName }, loggerLevelToSpdlogLevel(level), fmt, std::forward<Args>(args)...);
                }
            }

            #define GEN_REP(__name, __lvl) \
                template<typename... Args> \
                void __name(spdlog::format_string_t<Args...> fmt, Args &&...args) { \
                    this->__name(0, nullptr, nullptr, fmt, std::forward<Args>(args)...); \
                } \
                template<typename... Args> \
                void __name(int line, const char *fileName, const char *funcName, spdlog::format_string_t<Args...> fmt, Args &&...args) { \
                    this->report(fileName, funcName, line, __lvl, fmt, std::forward<Args>(args)...); \
                } \

            GEN_REP(always,  LogLevel::ALWAYS)
            GEN_REP(error,   LogLevel::ERROR)
            GEN_REP(warning, LogLevel::WARNING)
            GEN_REP(log,     LogLevel::LOG)
            GEN_REP(debug,   LogLevel::DEBUG)
            GEN_REP(trace,   LogLevel::TRACE)

        private:
            void applyParameters(const LoggerParameters &parameters);

            static std::map<std::string, std::unique_ptr<Logger>> initializer();

            static spdlog::level::level_enum loggerLevelToSpdlogLevel(LogLevel level);

        private:
            std::shared_ptr<spdlog::logger> _logger;
            LoggerParameters _params;

            static std::map<std::string, std::unique_ptr<Logger>> _loggers;
            static std::set<std::string> _loggersConnectedToRootLogger;
    };
}

#define LOGGER_ALWAYS_BY(logger, ...)  logger.always (__LINE__, __FILE__, __func__, __VA_ARGS__)
#define LOGGER_ERROR_BY(logger, ...)   logger.error  (__LINE__, __FILE__, __func__, __VA_ARGS__)
#define LOGGER_WARNING_BY(logger, ...) logger.warning(__LINE__, __FILE__, __func__, __VA_ARGS__)
#define LOGGER_LOG_BY(logger, ...)     logger.log    (__LINE__, __FILE__, __func__, __VA_ARGS__)
#define LOGGER_DEBUG_BY(logger, ...)   logger.debug  (__LINE__, __FILE__, __func__, __VA_ARGS__)
#define LOGGER_TRACE_BY(logger, ...)   logger.trace  (__LINE__, __FILE__, __func__, __VA_ARGS__)

#define LOGGER_ALWAYS(...)  LOGGER_ALWAYS_BY(utpl::Logger::getLogger(), __VA_ARGS__)
#define LOGGER_ERROR(...)   LOGGER_ERROR_BY(utpl::Logger::getLogger(), __VA_ARGS__)
#define LOGGER_WARNING(...) LOGGER_WARNING_BY(utpl::Logger::getLogger(), __VA_ARGS__)
#define LOGGER_LOG(...)     LOGGER_LOG_BY(utpl::Logger::getLogger(), __VA_ARGS__)
#define LOGGER_DEBUG(...)   LOGGER_DEBUG_BY(utpl::Logger::getLogger(), __VA_ARGS__)
#define LOGGER_TRACE(...)   LOGGER_TRACE_BY(utpl::Logger::getLogger(), __VA_ARGS__)

#endif /* __UTPL_COMMON_LOGGER_H__ */