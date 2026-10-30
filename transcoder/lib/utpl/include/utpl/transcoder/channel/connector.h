#ifndef __UTPL_TRANSCODER_CHANNEL_CONNECTOR_H__
#define __UTPL_TRANSCODER_CHANNEL_CONNECTOR_H__

#include "utpl/common/utils/class.h"

#include "utpl/transcoder/stream.h"
#include "utpl/pipeline/bin.h"

namespace utpl {
    namespace transcoder {
        class StreamConnector {
            UTPL_CLASS_INTERFACE(StreamConnector) {}

            public:
                /// Returns source stream
                virtual std::shared_ptr<InputStream> getStream() = 0;

                /// Returns stream parameters of connector
                virtual const StreamParameters &getStreamParameters() = 0;

                virtual pipeline::PadPtr getPad(const StreamParameters &parameters) = 0;
                virtual pipeline::PadPtr peekPad() = 0;

                virtual void prepare(pipeline::BinPtr &bin) = 0;
                virtual void link() = 0;
                virtual void unlink() = 0;
                virtual void cleanup() = 0;

                virtual pipeline::ElementPtr getNode() = 0;
        };
    }
}

#endif /* __UTPL_TRANSCODER_CHANNEL_CONNECTOR_H__ */