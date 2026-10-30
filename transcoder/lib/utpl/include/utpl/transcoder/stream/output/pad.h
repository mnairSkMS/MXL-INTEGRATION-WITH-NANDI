#ifndef __UTPL_TRANSCODER_STREAM_OUTPUT_PAD_H__
#define __UTPL_TRANSCODER_STREAM_OUTPUT_PAD_H__

#include "utpl/transcoder/stream.h"
#include "utpl/transcoder/stream/pad.h"

namespace utpl {
    namespace transcoder {
        class OutputPadStream : public OutputStream {
            public:
                OutputPadStream(const StreamParameters &parameters, pipeline::PadPtr pad) : _base(parameters) {
                    this->_pad = pad;
                }

                OutputPadStream(pipeline::PadPtr pad) : _base(pad) {
                    this->_pad;
                }

                const StreamParameters &getStreamParameters() const {
                    return this->_base.getStreamParameters();
                }

                void link() override {
                    this->_base.link();
                }

                void unlink() override {
                    this->_base.unlink();
                }

                pipeline::PadPtr getPad() override {
                    return this->_pad;
                }

            private:
                PadStream _base;
                pipeline::PadPtr _pad;
        };
    }
}

#endif /* __UTPL_TRANSCODER_STREAM_PAD_H__ */