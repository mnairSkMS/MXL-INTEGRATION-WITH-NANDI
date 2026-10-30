#include <boost/url/url.hpp>
#include <boost/url/parse.hpp>

#include "utpl/common/debug.h"
#include "utpl/pipeline/utils.h"

#include "file.h"

namespace urls = boost::urls;

using namespace utpl;

transcoder::FileOutputSink::FileOutputSink(const transcoder::OutputTransport &transport) : _supportedMediaTypes({
    MediaType::DATA, MediaType::AUDIO, MediaType::VIDEO, MediaType::CAPTION
}) {
    this->_bin      = pipeline::Bin::create();
    this->_location = urls::parse_uri(transport.getUri())->path().c_str();
}

void transcoder::FileOutputSink::link() {
    auto sink = this->_bin->add(pipeline::Element::make("filesink"));

    sink->setProperty<const gchar *>("location", this->_location.c_str());
    sink->setProperty<gboolean>("async", false);

    {
        auto queue = pipeline::utils::createQueue(this->_bin, std::string(), 16, 0, 0);
    
        queue->link(sink);
        
        this->_pad->setTarget(queue->getPad("sink"));
    }
}

pipeline::ElementPtr transcoder::FileOutputSink::getNode() {
    return this->_bin;
}

pipeline::PadPtr transcoder::FileOutputSink::getPad(MediaType mediaType, Pid pid) {
    if (! this->_pad) {
        this->_pad = this->_bin->addPad("", nullptr, false);
    }

    return this->_pad;
}

bool transcoder::FileOutputSink::isLive() const {
    return false;
};

bool transcoder::FileOutputSink::needExternalSync() const {
    return false;
}

const std::set<MediaType> &transcoder::FileOutputSink::getSupportedMediaTypes() const {
    return this->_supportedMediaTypes;
};

void transcoder::FileOutputSink::putScteData(const transcoder::Scte35Data &data) {
}