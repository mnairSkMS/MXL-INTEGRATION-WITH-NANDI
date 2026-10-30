#ifndef __UTPL_TRANSCODER_STREAM_PAD_H__
#define __UTPL_TRANSCODER_STREAM_PAD_H__

#include "utpl/transcoder/stream.h"
namespace utpl {
    namespace transcoder {
        class PadStream : public Stream {
            public:
                PadStream(const StreamParameters &parameters);
                PadStream(pipeline::PadPtr pad);

                const StreamParameters &getStreamParameters() const override;

                void link() override;
                void unlink() override;

            private:
                StreamParameters _parameters;
        };
    }
}

#endif /* __UTPL_TRANSCODER_STREAM_PAD_H__ */