#ifndef __UTPL_TRANSCODER_STREAM_INPUT_AUDIO_H__
#define __UTPL_TRANSCODER_STREAM_INPUT_AUDIO_H__

#include <functional>

#include "utpl/pipeline/pipe.h"
#include "utpl/transcoder/stream/input/base.h"

#include "utpl/common/utils/class.h"

namespace utpl {
    namespace transcoder {
        class DummyAudioStream : public BaseInputStream{
            public:
                DummyAudioStream(pipeline::BinPtr bin, const StreamParameters &streamParameters);

            protected:
                pipeline::PadPtr getSrcPad(bool passthrough) override;

            private:
                pipeline::PadPtr _srcPad;
        };
    }
}

#endif /* __UTPL_TRANSCODER_STREAM_INPUT_AUDIO_H__ */