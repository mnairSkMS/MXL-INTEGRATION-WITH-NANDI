#include "utpl/common/debug.h"


void utpl::Debug::initialize() {
}

void utpl::Debug::configureTranscoderDebug(const utpl::LoggerParameters &params) {
    {
        auto p = params;

        p.setBasicPattern(true);

        // FIXME The 'root' logger is intentionally choosen, as the app does not support the root logger at the moment.
        utpl::Logger::getLogger("root").setParameters(p);
    }

    utpl::Logger::getLogger("debug").setParameters(params);
}

utpl::Logger &utpl::Debug::getLogger() {
    static auto &logger = utpl::Logger::getLogger("debug");

    return logger;
}

utpl::Logger &utpl::Debug::getGstLogger() {
    static auto &logger = utpl::Logger::getLogger("gstreamer");

    return logger;
}

void utpl::Debug::configureGstreamerDebug(const utpl::LoggerParameters &params) {
    auto *envVar = std::getenv("GST_DEBUG");

    utpl::Logger::getLogger("gstreamer").setParameters(params);

    // This was added for development purposes. If GST_DEBUG is manually set in environment, skip
    // setting custom logger
    if (envVar == nullptr) {
        bool enable = false;
    
        gst_debug_set_default_threshold(GST_LEVEL_NONE);

        for (const auto &output : params.getOutputs()) {
            if (output.isEnabled()) {
                enable = true;
                break;
            }
        }

        if (enable) {
            gst_debug_remove_log_function(gst_debug_log_default);
            gst_debug_set_default_threshold(_toGstLevel(params.getLevel()));
            gst_debug_add_log_function((GstLogFunction) _gstLogFunction, nullptr, nullptr);
        }
    }
}

void utpl::Debug::_gstLogFunction(GstDebugCategory *category, GstDebugLevel level, const gchar *file, const gchar *function, gint line, GObject *object, GstDebugMessage *message, gpointer user_data) {
    utpl::LogLevel logLevel = utpl::LogLevel::NONE;

    switch (level) {
        case GST_LEVEL_ERROR:   logLevel = utpl::LogLevel::ERROR;   break;
        case GST_LEVEL_WARNING: logLevel = utpl::LogLevel::WARNING; break;
        case GST_LEVEL_INFO:    logLevel = utpl::LogLevel::LOG;     break;
        case GST_LEVEL_DEBUG:   logLevel = utpl::LogLevel::DEBUG;   break;
        case GST_LEVEL_LOG:     logLevel = utpl::LogLevel::LOG;     break;
        case GST_LEVEL_FIXME:   logLevel = utpl::LogLevel::WARNING; break;
        case GST_LEVEL_TRACE:   logLevel = utpl::LogLevel::TRACE;   break;
        default: break;
    }

    if (object) {
        gchar* objectInfo = gst_info_strdup_printf("%" GST_PTR_FORMAT, static_cast<void*>(object));

        getGstLogger().report(file, function, line, logLevel, "{}: {} {}", gst_debug_category_get_name(category), objectInfo, gst_debug_message_get(message));

        g_free(objectInfo);

    } else {
        getGstLogger().report(file, function, line, logLevel, "{}: {}", gst_debug_category_get_name(category), gst_debug_message_get(message));
    }
}

spdlog::level::level_enum utpl::Debug::_toSpdlogLevel(utpl::LogLevel level) {
    switch (level) {
        case utpl::LogLevel::TRACE:   return spdlog::level::trace;
        case utpl::LogLevel::DEBUG:   return spdlog::level::debug;
        case utpl::LogLevel::LOG:     return spdlog::level::info;
        case utpl::LogLevel::WARNING: return spdlog::level::warn;
        case utpl::LogLevel::ERROR:   return spdlog::level::err;
        case utpl::LogLevel::NONE:    return spdlog::level::off;
    }

    return spdlog::level::off;
}

GstDebugLevel utpl::Debug::_toGstLevel(utpl::LogLevel level) {
    switch (level) {
        case utpl::LogLevel::TRACE:   return GST_LEVEL_TRACE;
        case utpl::LogLevel::DEBUG:   return GST_LEVEL_DEBUG;
        case utpl::LogLevel::LOG:     return GST_LEVEL_INFO;
        case utpl::LogLevel::WARNING: return GST_LEVEL_WARNING;
        case utpl::LogLevel::ERROR:   return GST_LEVEL_ERROR;
        case utpl::LogLevel::NONE:    return GST_LEVEL_NONE;
    }

    return GST_LEVEL_NONE;
}