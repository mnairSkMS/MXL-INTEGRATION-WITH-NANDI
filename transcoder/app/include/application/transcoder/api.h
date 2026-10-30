#ifndef __APP_TRANSCODER_GRPC_H__
#define __APP_TRANSCODER_GRPC_H__

#include "utpl/common/utils/class.h"

#include "application/transcoder.h"

namespace app {
    class TranscoderApi {
        UTPL_CLASS_INTERFACE(TranscoderApi) {}

        public:
            virtual void start(Transcoder *transcoder) = 0;
            virtual void stop() = 0;
    };
}

#endif /* __APP_TRANSCODER_GRPC_H__ */