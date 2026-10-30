#ifndef __UTPL_TRANSCODER_OUTPUT_UTILS_H__
#define __UTPL_TRANSCODER_OUTPUT_UTILS_H__

#include "utpl/hls/streamer.h"
#include "utpl/transcoder/output.h"

namespace utpl {
    namespace transcoder {
        std::shared_ptr<hls::Streamer> createHlsStreamer(const OutputTransport &transport);

        std::shared_ptr<hls::MasterPlaylist> createHlsMasterPlaylist(const HlsOutputTransportParameters *hlsParameters);

        std::shared_ptr<hls::MediaPlaylist>  createHlsVariant(
            std::shared_ptr<hls::MasterPlaylist> masterPlaylist, 
            const HlsOutputTransportParameters  *hlsParameters, 
            const std::vector<StreamParameters> &streams,
            Container                            container,
            const std::string                   &nameModifier
        );
    }
}

#endif /* __UTPL_TRANSCODER_OUTPUT_UTILS_H__ */