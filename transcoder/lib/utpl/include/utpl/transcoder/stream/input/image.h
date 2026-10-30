#ifndef __UTPL_TRANSCODER_STREAM_INPUT_IMAGE_H__
#define __UTPL_TRANSCODER_STREAM_INPUT_IMAGE_H__

#include <functional>

#include "utpl/pipeline/pipe.h"
#include "utpl/transcoder/stream.h"

#include "utpl/common/utils/class.h"

namespace utpl {
    namespace transcoder {
        class ImageInputStream : public InputStream {
            UTPL_CLASS_NOBUILD_NOCOPY(ImageInputStream);

            public:
                using OnPreloadedCallback = std::function<void()>;
                using OnFailedCallbcak    = std::function<void(Error error)>;

            public:
                ImageInputStream(pipeline::BinPtr pipe, const std::vector<std::string> &uris, const StreamParameters &params);
                ~ImageInputStream();
    
                pipeline::PadPtr requestPad(bool passthrough) override;
                void             releasePad(pipeline::PadPtr pad) override;
                pipeline::PadPtr peekPad(bool passthrough) override;

                const StreamParameters &getStreamParameters() const override;
                
                void link() override;
                void unlink() override;

            public:
                void preload(utpl::Timestamp timeout = -1, OnPreloadedCallback preloadedCallback = nullptr, OnFailedCallbcak onFailedCallback = nullptr);
                bool isPreloaded();
                void block(std::function<bool()> callback = {});
                void unblock();
                void updatePosition(GstClockTime pos);

            private:
                class Pimpl;
                std::unique_ptr<Pimpl> self;
        };
    }
}

#endif /* __UTPL_TRANSCODER_STREAM_IMAGE_H__ */
