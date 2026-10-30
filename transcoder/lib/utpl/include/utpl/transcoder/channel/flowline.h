#ifndef __UTPL_TRANSCODER_CHANNEL_FLOWLINE_H__
#define __UTPL_TRANSCODER_CHANNEL_FLOWLINE_H__

#include <unordered_map>

#include "utpl/pipeline/pad.h"
#include "utpl/pipeline/element.h"
#include "utpl/pipeline/bin.h"

#include "utpl/transcoder/stream.h"

namespace utpl {
    namespace transcoder {
        class Flowline {
            UTPL_CLASS_INTERFACE(Flowline) {}

            public:
                virtual void addOutput(transcoder::OutputStream *stream) = 0;
                virtual bool compatible(const transcoder::StreamParameters &outputParameters) = 0;

                virtual void prepare(const transcoder::StreamParameters &inParameters, pipeline::BinPtr &bin) = 0;
                virtual pipeline::PadPtr getInputPad() = 0;

                virtual const StreamParameters &getParameters() const = 0;

                virtual void link() = 0;
        };
    }
}

#endif /* __UTPL_TRANSCODER_CHANNEL_FLOWLINE_H__ */