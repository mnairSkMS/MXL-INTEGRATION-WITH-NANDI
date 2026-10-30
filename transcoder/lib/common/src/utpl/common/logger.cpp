#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/stdout_sinks.h"
#include "spdlog/sinks/rotating_file_sink.h"

#include "utpl/common/logger.h"

using namespace utpl;

constexpr int logMaxSize = 1024 * 1024 * 5;
constexpr int logMaxFiles = 3;

constexpr const char *LOG_PATTERN_BASIC = "[ %D %T.%e ] [%-8n] [%^%=7l%$] %v";
constexpr const char *LOG_PATTERN_DEBUG = "[ %D %T.%e ] [%-8n] [%^%=7l%$] [ %-8!s, %-12!!: %-4!#] %v";

static const std::string DEFAULT_LOGGER_NAME = "root";


std::set<std::string> Logger::_loggersConnectedToRootLogger;
std::map<std::string, std::unique_ptr<Logger>> Logger::_loggers = Logger::initializer();

std::map<std::string, std::unique_ptr<Logger>> Logger::initializer() {
    std::map<std::string, std::unique_ptr<Logger>> ret;

    {
        ret[DEFAULT_LOGGER_NAME].reset(new Logger(DEFAULT_LOGGER_NAME));
        
        {
            auto &logger = ret[DEFAULT_LOGGER_NAME];

            logger->applyParameters(LoggerParameters()
                .setBasicPattern(true)
                .setLevel(LogLevel::WARNING)
                .addOutput(utpl::LoggerOutput()
                    .setColor(true)
                    .setEnabled(true)
                    .setType(LoggerOutputType::STDERR)
                )
            );

            spdlog::set_default_logger(logger->_logger);
        }
    }

    return ret;
}

spdlog::level::level_enum Logger::loggerLevelToSpdlogLevel(LogLevel level) {
    spdlog::level::level_enum mappedLevel;

    switch (level) {
        case ALWAYS:  
        case NONE:
        case UNKNOWN:
            mappedLevel = spdlog::level::off;
            break;

        case ERROR:   mappedLevel = spdlog::level::err;   break;
        case WARNING: mappedLevel = spdlog::level::warn;  break;
        case LOG:     mappedLevel = spdlog::level::info;  break;
        case DEBUG:   mappedLevel = spdlog::level::debug; break;
        case TRACE:   mappedLevel = spdlog::level::trace; break;
    }

    return mappedLevel;
}

Logger::Logger(const std::string &name) : _logger(std::make_shared<spdlog::logger>(name)) {
    if (name != DEFAULT_LOGGER_NAME) {
        Logger &rootLogger = getLogger(DEFAULT_LOGGER_NAME);

        this->applyParameters(rootLogger.getParameters());

        this->_loggersConnectedToRootLogger.insert(name);
    }

    spdlog::register_logger(this->_logger);
}

Logger::~Logger() {

}

LoggerParameters Logger::getParameters() const {
    return this->_params;
}

void Logger::applyParameters(const LoggerParameters &parameters) {
    this->_params = parameters;

    if (this->_params.getOutputs().empty()) {
        Logger &rootLogger = getLogger();

        this->_logger->sinks() = rootLogger._logger->sinks();

    } else {
        this->_logger->sinks().clear();

        for (const auto &sink : parameters.getOutputs()) {
            if (sink.isEnabled()) {
                auto color = sink.isColor(); 
                
                switch(sink.getType()) {
                    case utpl::LoggerOutputType::STDOUT:
                        if (color) {
                            this->_logger->sinks().push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
                        } else {
                            this->_logger->sinks().push_back(std::make_shared<spdlog::sinks::stdout_sink_mt>());
                        }
                        break;

                    case utpl::LoggerOutputType::STDERR:
                        if (color) {
                            this->_logger->sinks().push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
                        } else {
                            this->_logger->sinks().push_back(std::make_shared<spdlog::sinks::stderr_sink_mt>());
                        }
                        break;

                    case utpl::LoggerOutputType::FILE:
                        this->_logger->sinks().push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(sink.getPath(), logMaxSize, logMaxFiles));
                        break;
                }
            }
        }
    }

    this->_logger->set_pattern(
        parameters.isBasicPattern() ? 
            LOG_PATTERN_BASIC : LOG_PATTERN_DEBUG
    );

    this->_logger->set_level(loggerLevelToSpdlogLevel(parameters.getLevel()));
}

Logger &Logger::setParameters(const LoggerParameters &parameters) {
    const auto &name = this->_logger->name();

    if (name != DEFAULT_LOGGER_NAME) {
        this->_loggersConnectedToRootLogger.erase(name);

    } else {
        for (auto &l : this->_loggersConnectedToRootLogger) {
            getLogger(l).applyParameters(parameters);
        }
    }

    this->applyParameters(parameters);

    return *this;
}

Logger &Logger::getLogger(const std::string &name) {
    if (name.empty()) {
        return getLogger(DEFAULT_LOGGER_NAME);
    }

    auto it = _loggers.find(name);
    if (it == _loggers.end()) {
        _loggers[name].reset(new Logger(name));
        
        return *_loggers[name].get();
    }

    return *it->second.get();
}
