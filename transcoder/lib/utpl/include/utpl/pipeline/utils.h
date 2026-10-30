#ifndef __UTPL_PIPELINE_UTILS_H__
#define __UTPL_PIPELINE_UTILS_H__

#include "utpl/pipeline/element.h"
#include "utpl/pipeline/bin.h"

#include "utpl/transcoder/stream.h"

namespace utpl {
    namespace pipeline {
        namespace utils {
            ElementPtr createQueue(BinPtr bin = nullptr, const std::string &name = std::string(), guint sizeBuffers = 2, guint sizeBytes = 0, guint64 sizeTime = 0);
            ElementPtr createIdentity(BinPtr bin = nullptr, const std::string &name = std::string(), bool callHandoffs = false, bool silent = true);

            void addDebugTsWatcher(pipeline::PadPtr pad, const std::string &name);

            bool isDolbyPatCompatible(const utpl::transcoder::StreamParameters &parameters, bool encoded);
        }
    }
}

#endif /* __UTPL_PIPELINE_UTILS_H__ */