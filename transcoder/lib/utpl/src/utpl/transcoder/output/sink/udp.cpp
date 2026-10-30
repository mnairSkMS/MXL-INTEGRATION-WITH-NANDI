#include <boost/url/url.hpp>
#include <boost/url/parse.hpp>

#include "utpl/common/debug.h"
#include "utpl/pipeline/utils.h"

#include "udp.h"

namespace urls = boost::urls;

using namespace utpl;

transcoder::UdpOutputSink::UdpOutputSink(const transcoder::OutputTransport &transport) : _supportedMediaTypes({
    MediaType::DATA, MediaType::AUDIO, MediaType::VIDEO, MediaType::CAPTION
}) {
    auto url = urls::parse_uri(transport.getUri());

    this->_bin  = pipeline::Bin::create();
    this->_host = url->host_ipv4_address().to_string();
    this->_port = url->port_number();

    {
        const auto *udpParameters = reinterpret_cast<const transcoder::UdpOutputTransportParameters *>(transport.getTransportParameters(Protocol::UDP));

        if (udpParameters) {
            this->_transportParameters = *udpParameters;
        }
    }
}

void transcoder::UdpOutputSink::link() {
    auto sink = this->_bin->add(pipeline::Element::make("udpsink"));
                
    sink->setProperty<const gchar *>("host", this->_host.c_str());
    sink->setProperty<gint>("port", this->_port);
    sink->setProperty<gboolean>("async", false);

    {
        const auto &mcastParams = this->_transportParameters.getMulticastParameters();
        
        if (! mcastParams.getInterface().empty()) {
            sink->setProperty<const gchar *>("multicast-iface", mcastParams.getInterface().c_str());
        }

        sink->setProperty<gint>("ttl-mc", mcastParams.getTtl());
    }

    {
        const auto &ucastParams = this->_transportParameters.getUnicastParameters();

        sink->setProperty<gint>("ttl", ucastParams.getTtl());
    }

    {
        auto queue = pipeline::utils::createQueue(this->_bin, std::string(), 16, 0, 0);

        queue->link(sink);
        
        this->_pad->setTarget(queue->getPad("sink"));
    }
}

pipeline::ElementPtr transcoder::UdpOutputSink::getNode() {
    return this->_bin;
}

pipeline::PadPtr transcoder::UdpOutputSink::getPad(MediaType mediaType, Pid pid) {
    if (! this->_pad) {
        this->_pad = this->_bin->addPad("", nullptr, false);
    }

    return this->_pad;
}

bool transcoder::UdpOutputSink::isLive() const {
    return true;
};

bool transcoder::UdpOutputSink::needExternalSync() const {
    return false;
}

const std::set<MediaType> &transcoder::UdpOutputSink::getSupportedMediaTypes() const {
    return this->_supportedMediaTypes;
};

void transcoder::UdpOutputSink::putScteData(const transcoder::Scte35Data &data) {
}