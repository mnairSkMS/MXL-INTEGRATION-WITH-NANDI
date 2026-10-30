#include "utpl/common/debug.h"

#include "utpl/hls/streamer/uploader/file.h"
#include "utpl/hls/streamer/uploader/http.h"
#include "utpl/hls/segment/storage/factory/memory.h"

#include "utils.h"

using namespace utpl;

std::shared_ptr<hls::Streamer> transcoder::createHlsStreamer(const OutputTransport &transport) {
    auto ret = std::make_shared<hls::Streamer>();

    {
        const auto *hlsParameters = reinterpret_cast<const transcoder::HlsOutputTransportParameters *>(transport.getTransportParameters(Protocol::HLS));

        utpl::hls::Streamer::Parameters params;
                
        params
            .setScteMarkers     (hlsParameters->getAdMarkers())
            .setNumberOfSegments(hlsParameters->isVodMode() ? hls::Streamer::SEGMENTS_COUNT_UNLIMITED : hlsParameters->getMaxSegments())
            .setUploadTimeout   (hlsParameters->getTimeout())
            .setRetriesCount    (hlsParameters->getRetryCount())
            .setRetryInterval   (hlsParameters->getRetryInterval())
            .setSingleDir       (hlsParameters->isSingleDir())
            .setBaseDirName     (hlsParameters->getBaseFileName())
            .setStorageFactory(
                std::make_shared<utpl::hls::SegmentMemoryStorageFactory>()
            );

        {
            Protocol subprotocol;

            utpl::parseProtocol(transport.getUri(), subprotocol);

            if (subprotocol == Protocol::FILE) {
                std::string path;
                Protocol subprotocol;

                utpl::parseProtocolAndPath(transport.getUri(), subprotocol, path);

                auto fileUploader = std::make_unique<utpl::hls::FileUploader>(path);

                params.addUploader(std::move(fileUploader));

            } else {
                auto httpUploader = std::make_unique<utpl::hls::HttpUploader>(
                    transport.getUri(), 
                    hlsParameters->getTransferType()    == HlsOutputTransportParameters::TransferType::HTTP_DAV,
                    hlsParameters->getTransferFraming() == HlsOutputTransportParameters::TransferFraming::CHUNKED
                );

                httpUploader->setAuthorization(hlsParameters->getCredentials(), hlsParameters->getAuthentication());

                params.addUploader(std::move(httpUploader));
            }
        }

        ret->setParameters(params);
    }

    return ret;
}

std::shared_ptr<hls::MasterPlaylist> transcoder::createHlsMasterPlaylist(const HlsOutputTransportParameters *hlsParameters) {
    std::string masterName;

    if (hlsParameters) {
        masterName = hlsParameters->getBaseFileName();
        if (! masterName.empty()) {
            masterName += ".m3u8";
        }
    }

    if (masterName.empty()) {
        masterName = "master.m3u8";
    }

    return std::make_shared<utpl::hls::MasterPlaylist>(masterName);
}

std::shared_ptr<hls::MediaPlaylist> transcoder::createHlsVariant(
    std::shared_ptr<hls::MasterPlaylist> masterPlaylist, 
    const HlsOutputTransportParameters  *hlsParameters, 
    const std::vector<StreamParameters> &streams,
    Container                            container,
    const std::string                   &nameModifier
) {
    std::shared_ptr<hls::MediaPlaylist> ret;

    {
        std::set<utpl::MediaType> mediaTypes;

        for (const auto &s : streams) {
            if (s.hasAudioParameters()) {
                mediaTypes.insert(utpl::MediaType::AUDIO);

            } else if (s.hasVideoParameters()) {
                mediaTypes.insert(utpl::MediaType::VIDEO);
            }
        }

        {
            std::string variantName = "variant";
            
            if (! nameModifier.empty()) {
                variantName += nameModifier;

            } else {
                variantName += ("_" + std::to_string(masterPlaylist->getVariants().size()));
            }
            
            variantName += ".m3u8";

            if (! hlsParameters->getBaseFileName().empty()) {
                variantName = hlsParameters->getBaseFileName() + "_" + variantName;
            }

            ret = masterPlaylist->createMediaPlaylist(
                variantName, 
                container,
                std::vector<utpl::MediaType>(mediaTypes.begin(), mediaTypes.end()),
                hlsParameters->getMaxSegmentDuration() / 1000
            );
        }

        auto &variant = masterPlaylist->createVariant();

        variant.setPlaylist(*ret);

        for (const auto &s : streams) {
            if (s.hasVideoParameters()) {
                const auto &video     = s.getVideoParameters();
                const auto &framerate = video.getFramerate();

                variant.setResolution(video.getWidth(), video.getHeight());

                if (framerate.getNumerator() > 0 && framerate.getDenominator() > 0) {
                    variant.setFrameRate(static_cast<double>(framerate.getNumerator()) / framerate.getDenominator());
                }

                break;
            }
        }
    }

    return ret;
}