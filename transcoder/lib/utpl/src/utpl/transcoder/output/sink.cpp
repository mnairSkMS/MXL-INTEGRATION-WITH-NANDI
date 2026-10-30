#include "utpl/common/debug.h"
#include "utpl/common/utils.h"

#include "utpl/pipeline/element.h"
#include "utpl/pipeline/bin.h"

#include "utpl/transcoder/stream.h"
#include "utpl/transcoder/output/parameters.h"
#include "utpl/transcoder/output/sink.h"

#include "sink/file.h"
#include "sink/udp.h"
#include "sink/srt.h"
#include "sink/hls.h"
#include "sink/s3.h"
#include "sink/dash.h"

using namespace utpl;


std::unique_ptr<transcoder::OutputSink> transcoder::OutputSink::create(
    const utpl::transcoder::OutputTransport &transport,
    const std::vector<utpl::transcoder::StreamParameters> &streams,
    const utpl::transcoder::ScteOutputParameters &scteParameters,
    Container container
) {
    std::unique_ptr<OutputSink> ret;

    {
        Protocol subprotocol;

        switch (utpl::parseProtocol(transport.getUri(), subprotocol)) {
            case Protocol::NONE:
            case Protocol::FILE:
                ret = std::make_unique<FileOutputSink>(transport);
                break;

            case Protocol::UDP:
                ret = std::make_unique<UdpOutputSink>(transport);
                break;

            case Protocol::SRT:
                ret = std::make_unique<SrtOutputSink>(transport);
                break;

            case Protocol::HLS:
            case Protocol::HTTP:
                ret = createHls(transport, streams, scteParameters, container, nullptr, nullptr);
                break;

            case Protocol::S3:
                ret = std::make_unique<S3OutputSink>(transport);
                break;

            case Protocol::DASH:
                ret = createDash(transport, streams, scteParameters, nullptr, std::string());
                break;

            default:
                break;
        }
    }
        
    return ret;
}

std::unique_ptr<transcoder::OutputSink> transcoder::OutputSink::createHls(
    const utpl::transcoder::OutputTransport &transport,
    const std::vector<utpl::transcoder::StreamParameters> &streams,
    const utpl::transcoder::ScteOutputParameters &scteParameters,
    Container container,
    std::shared_ptr<hls::Streamer> streamer,
    std::shared_ptr<hls::MediaPlaylist> playlist
) {
    return std::make_unique<HlsOutputSink>("", transport, streams, scteParameters, container, streamer, playlist);
}

std::shared_ptr<transcoder::dash::Presentation> transcoder::OutputSink::createDashPresentation(const utpl::transcoder::OutputTransport &transport) {
    const auto *parameters = reinterpret_cast<const DashOutputTransportParameters *>(transport.getTransportParameters(Protocol::DASH));

    return std::make_shared<dash::Presentation>(transport.getUri(), parameters ? *parameters : DashOutputTransportParameters());
}

std::unique_ptr<transcoder::OutputSink> transcoder::OutputSink::createDash(
    const utpl::transcoder::OutputTransport &transport,
    const std::vector<utpl::transcoder::StreamParameters> &streams,
    const utpl::transcoder::ScteOutputParameters &scteParameters,
    std::shared_ptr<dash::Presentation> presentation,
    const std::string &variantPrefix
) {
    if (! presentation) {
        presentation = createDashPresentation(transport);

        return std::make_unique<DashOutputSink>(streams, scteParameters, presentation, presentation->getParameters().getBaseFileName());
    }

    return std::make_unique<DashOutputSink>(streams, scteParameters, presentation, variantPrefix);
}
