#ifndef __UPTL_COMMON_DEBUG_H__
#define __UPTL_COMMON_DEBUG_H__

#include <gst/gst.h>

#include "utpl/common/utils/class.h"
#include "utpl/common/types.h"
#include "utpl/common/logger.h"

#define _err(...)   LOGGER_ERROR_BY(utpl::Debug::getLogger(), __VA_ARGS__)
#define _warn(...)  LOGGER_WARNING_BY(utpl::Debug::getLogger(), __VA_ARGS__)
#define _log(...)   LOGGER_LOG_BY(utpl::Debug::getLogger(), __VA_ARGS__)
#define _dbg(...)   LOGGER_DEBUG_BY(utpl::Debug::getLogger(), __VA_ARGS__)
#define _trace(...) LOGGER_TRACE_BY(utpl::Debug::getLogger(), __VA_ARGS__)
#define _print(...) LOGGER_ALWAYS_BY(utpl::Debug::getLogger(), __VA_ARGS__)

#define ERR(x)   _err x
#define WARN(x)  _warn x
#define LOG(x)   _log x
#define DBG(x)   _dbg x
#define TRACE(x) _trace x
#define PRINT(x) _print x

namespace utpl {
    class Debug {
        public:
            UTPL_CLASS_NOBUILD(Debug);

            static void initialize();
            static void configureTranscoderDebug(const utpl::LoggerParameters &params);
            static void configureGstreamerDebug(const utpl::LoggerParameters &params);

            static Logger &getLogger();

        private:
            static Logger &getGstLogger();
            static void _gstLogFunction(GstDebugCategory *category, GstDebugLevel level, const gchar *file, const gchar *function, gint line, GObject *object, GstDebugMessage *message, gpointer user_data);
            static spdlog::level::level_enum _toSpdlogLevel(utpl::LogLevel level);
            static GstDebugLevel _toGstLevel(utpl::LogLevel level);
    };
}

#endif /* __UPTL_COMMON_DEBUG_H__ */