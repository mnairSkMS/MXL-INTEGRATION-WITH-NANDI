#include "utpl/transcoder/stream/input/video.h"

using namespace utpl;


transcoder::DummyVideoStream::DummyVideoStream(pipeline::BinPtr bin, const StreamParameters &streamParameters) :
    BaseInputStream(pipeline::Bin::create())
{
    auto dummySrc = this->getBin()->add(pipeline::Element::make("videotestsrc"));

    dummySrc->setProperty<gint>("pattern", 2);
    dummySrc->setProperty<gboolean>("is-live", true);

    {
        auto filter = this->getBin()->add(pipeline::Element::make("capsfilter"));

        {
            auto caps = pipeline::Caps::create();

            caps->setMediaFormat(MediaType::VIDEO, Container::ES, Codec::ES);

            {
                const auto &videoParams = streamParameters.getVideoParameters();

                caps->setFormat(videoParams.getPixelFormat());
                caps->setWidth(videoParams.getWidth());
                caps->setHeight(videoParams.getHeight());

                caps->setFraction("pixel-aspect-ratio", { 1, 1 });
            }

            filter->setProperty<GstCaps *>("caps", (*caps)->get());
        }

        dummySrc->link(filter);

        this->_srcPad = filter->getPad("src");
    }

    this->setStreamParameters(streamParameters);

    bin->add(this->getBin());
}

pipeline::PadPtr transcoder::DummyVideoStream::getSrcPad(bool passthrough) {
    return this->_srcPad;
}