#ifndef __UTPL_TRANSCODER_STREAM_BASE_H__
#define __UTPL_TRANSCODER_STREAM_BASE_H__

#include "utpl/transcoder/mxltap.h"
#include "utpl/transcoder/preview.h"
#include "utpl/pipeline/bin.h"

#include "utpl/transcoder/stream/pad.h"

namespace utpl {
    namespace transcoder {
        class BaseInputStream : public InputStream {
            public:
                BaseInputStream(pipeline::BinPtr bin);
                ~BaseInputStream();
            
                pipeline::PadPtr requestPad(bool passthrough) override;
                void             releasePad(pipeline::PadPtr pad) override;
                pipeline::PadPtr peekPad(bool passthrough) override;

                const StreamParameters &getStreamParameters() const override;

                // Copies this stream's decoded media to MXL once it is linked, if the input is
                // configured for it. Must be called before link().
                void setMxlTap(const MxlTapParameters &parameters);

                void link() override;
                void unlink() override;

            protected:
                virtual pipeline::PadPtr getSrcPad(bool passthrough) = 0;
            
            protected:
                void setStreamParameters(const StreamParameters &parameters);

                void linkSrcPad(pipeline::PadPtr pad, bool isPastrhrough);

                pipeline::BinPtr getBin();

            private:
                std::vector<pipeline::GhostPadPtr> _passthroughPads;
                std::vector<pipeline::GhostPadPtr> _decodedPads;
                pipeline::BinPtr _bin;
                StreamParameters _streamParameters;

                pipeline::PadPtr _passthroughPad;
                pipeline::PadPtr _decodedPad;

                pipeline::ElementPtr _splitter;

                // JPEG preview branch on a video stream's tee; see preview.h.
                std::unique_ptr<PreviewTap> _preview;

                // MXL branch on the tee of a decoded video or audio stream; see mxltap.h.
                MxlTapParameters       _mxlTapParameters;
                std::unique_ptr<MxlTap> _mxlTap;

                pipeline::PadPtr _createPreview(bool encoded);
                pipeline::PadPtr _createMxlTap();
        };
    }
}
#endif /* __UTPL_TRANSCODER_STREAM_BASE_H__ */