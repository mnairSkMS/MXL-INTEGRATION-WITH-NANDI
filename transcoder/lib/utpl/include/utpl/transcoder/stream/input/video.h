#ifndef __UTPL_TRANSCODER_STREAM_INPUT_VIDEO_H__
#define __UTPL_TRANSCODER_STREAM_INPUT_VIDEO_H__

#include "utpl/pipeline/bin.h"

#include "utpl/transcoder/stream/input/base.h"

namespace utpl {
    namespace transcoder {
        class DummyVideoStream : public BaseInputStream {
            public:
                DummyVideoStream(pipeline::BinPtr bin, const StreamParameters &streamParameters);

            protected:
                pipeline::PadPtr getSrcPad(bool passthrough) override;

            private:
                pipeline::PadPtr _srcPad;
        };
    }
}

#endif /* __UTPL_TRANSCODER_STREAM_INPUT_VIDEO_H__ */