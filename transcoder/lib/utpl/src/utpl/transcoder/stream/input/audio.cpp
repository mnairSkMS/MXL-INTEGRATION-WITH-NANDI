#include "utpl/common/debug.h"

#include "utpl/transcoder/stream/input/audio.h"

using namespace utpl;


transcoder::DummyAudioStream::DummyAudioStream(pipeline::BinPtr bin, const StreamParameters &streamParameters) : 
    BaseInputStream(pipeline::Bin::create())
{
    auto dummySrc = this->getBin()->add(pipeline::Element::make("audiotestsrc"));

    dummySrc->setProperty<gint>("wave", 4); // silence, 0 - 440kHz sine
    dummySrc->setProperty<gboolean>("is-live", true);

    auto targetParameters = streamParameters;

    // Gstreamer has problems when requested more than 6 channels. This is
    // happening on Dolby transcoding
    {
        auto &params = targetParameters.getAudioParameters();
     
        if (params.getChannels() > 6) {
            params.setChannels(6);
        }
    }

    {
        auto filter = this->getBin()->add(pipeline::Element::make("capsfilter"));

        {
            auto caps = pipeline::Caps::create();

            caps->setMediaFormat(MediaType::AUDIO, Container::ES, Codec::ES);

            {
                const auto &audioParams = targetParameters.getAudioParameters();

                caps->setChannels(audioParams.getChannels());
                caps->setRate(audioParams.getRate());
            }

            filter->setProperty<GstCaps *>("caps", (*caps)->get());
        }

        dummySrc->link(filter);

        this->_srcPad = filter->getPad("src");
    }

    this->setStreamParameters(targetParameters);

    bin->add(this->getBin());
}


pipeline::PadPtr transcoder::DummyAudioStream::getSrcPad(bool passthrough) {
    if (! passthrough) {
        return this->_srcPad;
    }

    return nullptr;
}