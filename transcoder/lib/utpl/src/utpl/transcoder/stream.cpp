#include "utpl/common/types/exception.h"

#include "utpl/transcoder/stream.h"

using namespace utpl;


transcoder::StreamParameters transcoder::StreamParameters::fromCaps(const pipeline::Caps &caps) {
    transcoder::StreamParameters ret;

    ret.setCodec(caps.getCodec());

    switch (caps.getMediaType()) {
        case MediaType::AUDIO:
            {
                AudioParameters audioParams;

                audioParams.setChannels(caps.getChannels());
                audioParams.setRate(caps.getRate());

                ret.setAudioParameters(audioParams);
            }
            break;

        case MediaType::VIDEO:
            {
                VideoParameters videoParams;

                videoParams.setWidth      (caps.getWidth    (videoParams.getHeight()));
                videoParams.setHeight     (caps.getHeight   (videoParams.getHeight()));
                videoParams.setFramerate  (caps.getFrameRate(videoParams.getFramerate()));
                videoParams.setInterlace  (caps.getInterlace(videoParams.getInterlace()));
                videoParams.setPixelFormat(caps.getFormat   (videoParams.getPixelFormat()));

                ret.setVideoParameters(videoParams);
            }
            break;

        case MediaType::CAPTION:
            {
                CaptionParameters captionParams;

                ret.setCaptionParameters(captionParams);
            }
            break;

        case MediaType::UNKNOWN:
            {
                throw InvalidArgumentException("Unknown media type!");
            }
            break;
    }

    return ret;
}

std::string transcoder::StreamParameters::toString() const {
    std::string ret;

    if (this->hasAudioParameters()) {
        const auto &params = this->getAudioParameters();

        ret += "audio";
        ret += (", c=" + utpl::toString(this->getCodec()));
        ret += (", ch=" + std::to_string(params.getChannels()));
        ret += (", r=" + std::to_string(params.getRate()));

    } else if (this->hasVideoParameters()) {
        const auto &params = this->getVideoParameters();

        ret += "video";
        ret += (", c=" + utpl::toString(this->getCodec()));
        ret += (", w=" + std::to_string(params.getWidth()));
        ret += (", h=" + std::to_string(params.getHeight()));
        ret += (", r=" + std::to_string(params.getFramerate()));
        ret += (", p=" + utpl::toString(params.getPixelFormat()));

    } else if (this->hasCaptionParameters()) {
        ret = "caption";
        ret += "," + utpl::toString(this->getCodec());

    } else {
        ret = "empty";
    }

    return ret;
}