#include <gtest/gtest.h>

#include "utpl/common/types/exception.h"

#include "utpl/playlist/parser.h"

#include "utpl/transcoder/input.h"
#include "utpl/transcoder/input/single.h"

using namespace utpl;

static const std::string contents = 
R"(
#EXTM3U
#EXTINF:-1,scte35Source=manifest,user="test",password="secret",bandwidth=1000000
https://example.com/stream.m3u8
#EXTINF:-1,mode=caller,latency=120,bufferSize=1000
srt://example.com:1935
#EXTINF:-1,
udp://example.com:1234
#EXTINF:60.000
file:///path/to/video.ts
)";

TEST(PlaylistParserTest, ParseDifferentProtocols) {
    playlist::Parser parser;
    
    std::istringstream stream(contents);

    auto inputs = parser.parse(stream);
    ASSERT_EQ(inputs.size(), 4);

    // Test HLS protocol
    {
        const auto &params = inputs[0];
        ASSERT_EQ(params.getUris().size(), 1);
        EXPECT_EQ(params.getUris()[0], "https://example.com/stream.m3u8");
        
        const auto *hlsParams = reinterpret_cast<const transcoder::HlsInputTransportParameters *>(params.getTransportParameters(Protocol::HLS));
        ASSERT_NE(hlsParams, nullptr);
        
        const auto &scteParams = hlsParams->getScteParameters();
        EXPECT_EQ(scteParams.getSource(), transcoder::ScteSource::MANIFEST);
        
        const auto &creds = hlsParams->getCredentials();
        EXPECT_EQ(creds.getUsername(), "test");
        EXPECT_EQ(creds.getPassword(), "secret");

        EXPECT_EQ(hlsParams->getBandwith(), 1000000);
    }

    // Test SRT protocol
    {
        const auto &params = inputs[1];
        ASSERT_EQ(params.getUris().size(), 1);
        EXPECT_EQ(params.getUris()[0], "srt://example.com:1935");
        
        const auto *srtParams = reinterpret_cast<const transcoder::SrtInputTransportParameters *>(params.getTransportParameters(Protocol::SRT));
        ASSERT_NE(srtParams, nullptr);
        
        EXPECT_EQ(srtParams->getMode(), transcoder::SrtProtocolMode::CALLER);
        EXPECT_EQ(srtParams->getLatency(), 120);
        EXPECT_EQ(srtParams->getBufferSize(), 1000);
    }

    // Test UDP protocol
    {
        const auto &params = inputs[2];
        ASSERT_EQ(params.getUris().size(), 1);
        EXPECT_EQ(params.getUris()[0], "udp://example.com:1234");
        
        const auto *udpParams = reinterpret_cast<const transcoder::UdpInputTransportParameters *>(params.getTransportParameters(Protocol::UDP));
        ASSERT_EQ(udpParams, nullptr);
    }

    // Test File protocol
    {
        const auto &params = inputs[3];
        ASSERT_EQ(params.getUris().size(), 1);
        EXPECT_EQ(params.getUris()[0], "file:///path/to/video.ts");
        
        const auto *fileParams = reinterpret_cast<const transcoder::FileInputTransportParameters *>(params.getTransportParameters(Protocol::FILE));
        ASSERT_EQ(fileParams, nullptr);
    }


    // Test missing EXTM3U
    {
        std::string contents = 
R"(
#EXTINF:-1
http://example.com/stream.m3u8
)";

        std::istringstream stream(contents);
        EXPECT_THROW(parser.parse(stream), utpl::InvalidArgumentException);
    }
} 