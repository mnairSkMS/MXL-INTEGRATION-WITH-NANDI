#ifndef __UTPL_TRANSCODER_OUTPUT_SINK_H__
#define __UTPL_TRANSCODER_OUTPUT_SINK_H__

#include <memory>
#include <set>

#include "utpl/common/utils/class.h"

#include "utpl/pipeline/element.h"
#include "utpl/pipeline/bin.h"

#include "utpl/hls/streamer.h"

namespace utpl {
    namespace transcoder {
        namespace dash {
            class Presentation;
        }
    }
}

#include "utpl/transcoder/types.h"
#include "utpl/transcoder/output/parameters.h"

namespace utpl {
    namespace transcoder {
        class OutputSink {
            UTPL_CLASS_INTERFACE(OutputSink) {}
        
            public:
                virtual void link() = 0;
        
                virtual pipeline::ElementPtr getNode() = 0;
        
                virtual pipeline::PadPtr getPad(MediaType mediaType, Pid pid = PID_NULL) = 0;
        
                virtual void putScteData(const utpl::transcoder::Scte35Data &data) = 0;
        
                virtual bool isLive() const = 0;
                virtual bool needExternalSync() const = 0;
        
                virtual const std::set<MediaType> &getSupportedMediaTypes() const = 0;
        
            public:
                static std::unique_ptr<OutputSink> create(
                    const utpl::transcoder::OutputTransport &transport,
                    const std::vector<utpl::transcoder::StreamParameters> &streams,
                    const utpl::transcoder::ScteOutputParameters &scteParameters,
                    Container container
                );

                static std::unique_ptr<OutputSink> createHls(
                    const utpl::transcoder::OutputTransport &transport,
                    const std::vector<utpl::transcoder::StreamParameters> &streams,
                    const utpl::transcoder::ScteOutputParameters &scteParameters,
                    Container container,
                    std::shared_ptr<hls::Streamer> streamer,
                    std::shared_ptr<hls::MediaPlaylist> playlist
                );

                /**
                 * CMAF DASH sink. A GROUP output passes the presentation its members share and a
                 * per-member object prefix; a single output passes nullptr and gets its own.
                 */
                static std::unique_ptr<OutputSink> createDash(
                    const utpl::transcoder::OutputTransport &transport,
                    const std::vector<utpl::transcoder::StreamParameters> &streams,
                    const utpl::transcoder::ScteOutputParameters &scteParameters,
                    std::shared_ptr<dash::Presentation> presentation,
                    const std::string &variantPrefix
                );

                static std::shared_ptr<dash::Presentation> createDashPresentation(const utpl::transcoder::OutputTransport &transport);
        };
        
    }
}

#endif /* __UTPL_TRANSCODER_OUTPUT_SINK_H__ */