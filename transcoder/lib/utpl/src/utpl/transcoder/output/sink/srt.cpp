#include <boost/url/url.hpp>
#include <boost/url/parse.hpp>

#include "utpl/common/debug.h"

#include "srt.h"

namespace urls = boost::urls;

using namespace utpl;

namespace {
constexpr guint64 SRT_BACKLOG_MAX_TIME = 1000 * GST_MSECOND;
}

transcoder::SrtOutputSink::SrtOutputSink(const transcoder::OutputTransport &transport) : _supportedMediaTypes({
    MediaType::DATA, MediaType::AUDIO, MediaType::VIDEO, MediaType::CAPTION
}) {
    auto url = urls::parse_uri(transport.getUri());

    this->_bin  = pipeline::Bin::create();
    
    this->_targetUri         = transport.getUri();
    this->_waitForConnection = true;

    {
        const auto *srtParameters = reinterpret_cast<const SrtOutputTransportParameters *>(transport.getTransportParameters(Protocol::SRT));
        if (srtParameters) {
            urls::url mutableUrl = url.value();

            {
                auto params = mutableUrl.params();

                if (srtParameters->getLatency() > 0) {
                    params.set("latency", std::to_string(srtParameters->getLatency()), urls::ignore_case);
                }
                
                {
                    std::string modeValue;

                    switch (srtParameters->getMode()) {
                        case SrtProtocolMode::CALLER:
                            modeValue = "caller";
                            break;

                        case SrtProtocolMode::LISTENER:
                            modeValue = "listener";
                            this->_waitForConnection = false;
                            break;

                        case SrtProtocolMode::UNKNOWN:
                            break;
                    }

                    if (! modeValue.empty()) {
                        params.set("mode", modeValue);
                    }
                }

                if (srtParameters->getBufferSize() > 0) {
                    params.set("sndbuf", std::to_string(srtParameters->getBufferSize()));
                }
            }

            this->_targetUri = mutableUrl.buffer();
        }
    }
}

void transcoder::SrtOutputSink::link() {
    // [N1-60] A leaky queue isolates a slow or absent SRT peer from the rest of the graph. Keep
    // only a short recovery window: a multi-second queue preserves stale media and turns a brief
    // network stall or late receiver connection into avoidable end-to-end latency.
    // N1-128: added chopmydata element to improve stability of the output stream
    auto sinkDescription =
        "queue silent=true leaky=downstream max-size-buffers=0 max-size-bytes=0 max-size-time=" +
        std::to_string(SRT_BACKLOG_MAX_TIME) +
        " ! chopmydata min-size=1316 max-size=1316 ! srtsink async=false";

    // In listener mode, waiting inside srtsink pins one stale buffer outside the leaky queue until
    // a receiver arrives. Drop while disconnected instead; the listener continues accepting a
    // later client and starts it on current live media. Preserve the existing caller behavior.
    if (! this->_waitForConnection) {
        sinkDescription += " wait-for-connection=false";
    }

    auto sinkBin = pipeline::Bin::parse(sinkDescription + " uri=" + this->_targetUri);

    auto sink = this->_bin->add(sinkBin);
    
    this->_pad->setTarget(sink->getPad("sink"));
}

pipeline::ElementPtr transcoder::SrtOutputSink::getNode() {
    return this->_bin;
}

pipeline::PadPtr transcoder::SrtOutputSink::getPad(MediaType mediaType, Pid pid) {
    if (! this->_pad) {
        this->_pad = this->_bin->addPad("", nullptr, false);
    }

    return this->_pad;
}

bool transcoder::SrtOutputSink::isLive() const {
    return true;
};

bool transcoder::SrtOutputSink::needExternalSync() const {
    // External synchronization is required due to the leaky queue located just before srtsink.
    return true;
}

const std::set<MediaType> &transcoder::SrtOutputSink::getSupportedMediaTypes() const {
    return this->_supportedMediaTypes;
};

void transcoder::SrtOutputSink::putScteData(const transcoder::Scte35Data &data) {
}
