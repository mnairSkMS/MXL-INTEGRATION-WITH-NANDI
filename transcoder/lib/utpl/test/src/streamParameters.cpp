#include <gtest/gtest.h>

#include "utpl/transcoder/stream.h"

using namespace utpl;
using namespace utpl::transcoder;

namespace {
    // Outputs whose StreamParameters compare equal share one encoder (Flowline::compatible).
    StreamParameters aac(const std::string &bitrate) {
        AudioParameters audio;
        audio.setRate(48000);
        audio.setChannels(2);

        GstEncoderParameters encoder("fdkaacenc");
        encoder.addProperty("bitrate", bitrate);

        StreamParameters params;
        params.setCodec(Codec::AAC);
        params.setAudioParameters(audio);
        params.setEncoderParameters(encoder);
        return params;
    }

    StreamParameters h264(int height, const std::string &bitrate) {
        VideoParameters video;
        video.setWidth(height * 16 / 9);
        video.setHeight(height);

        GstEncoderParameters encoder("x264enc");
        encoder.addProperty("bitrate", bitrate);
        encoder.addCapability("profile", "main");

        StreamParameters params;
        params.setCodec(Codec::H264);
        params.setVideoParameters(video);
        params.setEncoderParameters(encoder);
        return params;
    }
}

TEST(StreamParameters, SameEncoderSettingsAreEqual) {
    EXPECT_EQ(aac("128000"), aac("128000"));
    EXPECT_EQ(h264(720, "3000"), h264(720, "3000"));
}

TEST(StreamParameters, AudioEncoderPropertyValueMatters) {
    EXPECT_NE(aac("128000"), aac("96000"));
}

TEST(StreamParameters, VideoEncoderPropertyValueMatters) {
    EXPECT_NE(h264(720, "3000"), h264(720, "2000"));
}

TEST(StreamParameters, EncoderNameAndCapabilitiesMatter) {
    auto other = aac("128000");
    other.setEncoderParameters(GstEncoderParameters("avenc_aac"));
    EXPECT_NE(aac("128000"), other);

    GstEncoderParameters high("x264enc");
    high.addProperty("bitrate", "3000");
    high.addCapability("profile", "high");

    auto withHigh = h264(720, "3000");
    withHigh.setEncoderParameters(high);
    EXPECT_NE(h264(720, "3000"), withHigh);
}

TEST(StreamParameters, AudioParametersNotEqualToItselfIsFalse) {
    AudioParameters audio;
    audio.setRate(48000);

    EXPECT_FALSE(audio != audio);
}
