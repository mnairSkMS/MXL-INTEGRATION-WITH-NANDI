#include <boost/url/url.hpp>
#include <boost/url/parse.hpp>

#include "utpl/common/debug.h"
#include "utpl/pipeline/utils.h"

#include "utpl/pipeline/caps.h"
#include "s3.h"

namespace urls = boost::urls;

using namespace utpl;

transcoder::S3OutputSink::S3OutputSink(const transcoder::OutputTransport &transport) : _supportedMediaTypes({
    MediaType::DATA, MediaType::AUDIO, MediaType::VIDEO, MediaType::CAPTION
}) {
    auto url = urls::parse_uri(transport.getUri());

    this->_bin = pipeline::Bin::create();
    
    {
        const auto *s3Parameters = reinterpret_cast<const transcoder::S3OutputTransportParameters *>(transport.getTransportParameters(Protocol::S3));

        if (s3Parameters) {
            this->_transportParameters = *s3Parameters;

            this->_bucket = this->_transportParameters.getBucket();
            this->_accessKey = this->_transportParameters.getAccessKey();
            this->_secretAccessKey = this->_transportParameters.getSecretAccessKey();
            this->_region = this->_transportParameters.getRegion();
            this->_keyPrefix = this->_transportParameters.getKeyPrefix();
        }
    }
}

gboolean link_audio_pads_now(gpointer data) {
    transcoder::S3OutputSink *s3 = (transcoder::S3OutputSink *) data;
    DBG(("Skandha: Linking Audio Pads Now"));

    auto awsSinkPad = s3->_awss3hlssink->requestPad("audio");
    auto parserSinkPad = s3->_audioParser->getPad("sink");
    auto parserSrcPad = s3->_audioParser->getPad("src");

    s3->_demuxAudioPad->link(parserSinkPad);
    parserSrcPad->link(awsSinkPad);
    return FALSE;
}

void transcoder::S3OutputSink::link() {
    this->_queue = this->_bin->add(pipeline::Element::make("queue"));
    this->_demux = this->_bin->add(pipeline::Element::make("parsebin"));

    this->_audioQueue = this->_bin->add(pipeline::Element::make("queue"));
    this->_videoQueue = this->_bin->add(pipeline::Element::make("queue"));
    this->_videoParser = this->_bin->add(pipeline::Element::make("h264parse"));
    this->_audioParser = this->_bin->add(pipeline::Element::make("aacparse"));
    this->_awss3hlssink = this->_bin->add(pipeline::Element::make("awss3hlssink"));
    
    // hlssink2 for awss3hlssink
    this->_hlssink = pipeline::Element::make("hlssink");
    this->_hlssink->setProperty<gint>("target-duration", 6);
    this->_hlssink->setProperty<gint>("max-files", 20);
    
    GstElement *hlssink;
    g_object_get((*this->_awss3hlssink)->get(), "hlssink", &hlssink, NULL);
    g_object_set(hlssink, 
                "target-duration", 6, 
                "max-files", 20,
                NULL);
    if (this->_awss3hlssink) {
        this->_awss3hlssink->setProperty<const gchar*>("bucket", this->_bucket.c_str());
        this->_awss3hlssink->setProperty<const gchar*>("access-key", this->_accessKey.c_str());
        this->_awss3hlssink->setProperty<const gchar*>("secret-access-key", this->_secretAccessKey.c_str());
        this->_awss3hlssink->setProperty<const gchar*>("region", this->_region.c_str());
        this->_awss3hlssink->setProperty<const gchar*>("key-prefix", this->_keyPrefix.c_str());
        this->_awss3hlssink->setProperty<GstElement *>("hlssink", hlssink);
    }

    this->_demux->onPadAdded([this](std::shared_ptr<utpl::pipeline::Pad>& pad) -> bool {
        MediaType mt = pad->getCaps().getMediaType();

        DBG(("Skandha: Demux Pad Added Callback: {}", pad->getName()));
        DBG(("Skandha: Caps MediaType: {}", static_cast<int>(mt)));

        if(mt == MediaType::VIDEO) {
            auto awsSinkPad = this->_awss3hlssink->requestPad("video");
            DBG(("Skandha: awss3hlssink get pad: {}", awsSinkPad->getName()));

            auto videoQueuePad = this->_videoQueue->getPad("sink");
            auto parserSinkPad = this->_videoParser->getPad("sink");
            auto parserSrcPad = this->_videoParser->getPad("src");

            pad->link(videoQueuePad);
            this->_videoQueue->link(this->_videoParser);
            parserSrcPad->link(awsSinkPad);
        }
        else if(mt == MediaType::AUDIO) {
            auto awsSinkPad = this->_awss3hlssink->requestPad("audio");
            DBG(("Skandha: awss3hlssink get pad: {}", awsSinkPad->getName()));

            auto audioQueuePad = this->_audioQueue->getPad("sink");
            auto parserSinkPad = this->_audioParser->getPad("sink");
            auto parserSrcPad = this->_audioParser->getPad("src");

            // tsdemux ! queue ! aacparse ! awss3hlssink.audio
            pad->link(audioQueuePad);
            this->_audioQueue->link(this->_audioParser);
            parserSrcPad->link(awsSinkPad);
        }
        
        return true;
    });
    

    this->_queue->link(this->_demux);
    
    this->_pad->setTarget(this->_queue->getPad("sink"));

}

pipeline::ElementPtr transcoder::S3OutputSink::getNode() {
    return this->_bin;
}

pipeline::PadPtr transcoder::S3OutputSink::getPad(MediaType mediaType, Pid pid) {
    if (! this->_pad) {
        this->_pad = this->_bin->addPad("", nullptr, false);
    }

    return this->_pad;
}

bool transcoder::S3OutputSink::isLive() const {
    return true;
};

bool transcoder::S3OutputSink::needExternalSync() const {
    return false;
}

const std::set<MediaType> &transcoder::S3OutputSink::getSupportedMediaTypes() const {
    return this->_supportedMediaTypes;
};

void transcoder::S3OutputSink::putScteData(const transcoder::Scte35Data &data) {
}