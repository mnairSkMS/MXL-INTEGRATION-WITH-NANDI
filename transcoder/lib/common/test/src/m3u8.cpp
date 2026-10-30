#include <gtest/gtest.h>

#include "utpl/common/utils/m3u8.h"

using namespace utpl;

static const std::string contents = 
R"(
#EXTM3U
#EXTINF:-1,
udp://127.0.0.1:5555
#EXTINF:-1,mode=caller,latency=120,bufferSize=0
srt://127.0.0.1:2222
#EXTINF:10.5,string="hello",enum=value,rational=1/2
file:///media/video.ts
hls://127.0.0.1:8080/playlist.m3u8
)";

TEST(M3U8Test, ParsePlaylist) {
    std::istringstream stream(contents);

    auto m3u8 = utils::M3U8::parse(stream);
    
    ASSERT_NE(m3u8, nullptr);

    const auto &entries = m3u8->getEntries();
    ASSERT_EQ(entries.size(), 8); // EXTM3U + 3 pairs of EXTINF+URI + 1 additional URI

    {
        auto extm3u = std::dynamic_pointer_cast<utils::M3U8::TagEntry>(entries[0]);
        ASSERT_NE(extm3u, nullptr);
        EXPECT_EQ(extm3u->getTag(), "EXTM3U");
        EXPECT_TRUE(extm3u->getAttributes().empty());
    }

    {
        auto extinf = std::dynamic_pointer_cast<utils::M3U8::TagEntry>(entries[1]);
        ASSERT_NE(extinf, nullptr);
        EXPECT_EQ(extinf->getTag(), "EXTINF");
        ASSERT_EQ(extinf->getAttributes().size(), 1);
        EXPECT_TRUE(extinf->getAttributes()[0].getName().empty());
        auto duration = std::dynamic_pointer_cast<utils::M3U8::AttributeValueInteger>(extinf->getAttributes()[0].getValue());
        ASSERT_NE(duration, nullptr);
        EXPECT_EQ(duration->getValue(), -1);

        {
            auto uri = std::dynamic_pointer_cast<utils::M3U8::UriEntry>(entries[2]);
            ASSERT_NE(uri, nullptr);
            EXPECT_EQ(uri->getUri(), "udp://127.0.0.1:5555");
        }
    }

    {
        auto extinf = std::dynamic_pointer_cast<utils::M3U8::TagEntry>(entries[3]);
        ASSERT_NE(extinf, nullptr);
        EXPECT_EQ(extinf->getTag(), "EXTINF");

        const auto &attrs = extinf->getAttributes();
        ASSERT_EQ(attrs.size(), 4); // duration + 3 attributes

        {
            EXPECT_TRUE(attrs[0].getName().empty());
            auto duration = std::dynamic_pointer_cast<utils::M3U8::AttributeValueInteger>(attrs[0].getValue());
            ASSERT_NE(duration, nullptr);
            EXPECT_EQ(duration->getValue(), -1);
        }

        {
            EXPECT_EQ(attrs[1].getName(), "mode");
            auto mode = std::dynamic_pointer_cast<utils::M3U8::AttributeValueEnum>(attrs[1].getValue());
            ASSERT_NE(mode, nullptr);
            EXPECT_EQ(mode->getValue(), "caller");
        }

        {
            EXPECT_EQ(attrs[2].getName(), "latency");
            auto latency = std::dynamic_pointer_cast<utils::M3U8::AttributeValueInteger>(attrs[2].getValue());
            ASSERT_NE(latency, nullptr);
            EXPECT_EQ(latency->getValue(), 120);
        }

        {
            EXPECT_EQ(attrs[3].getName(), "bufferSize");
            auto bufferSize = std::dynamic_pointer_cast<utils::M3U8::AttributeValueInteger>(attrs[3].getValue());
            ASSERT_NE(bufferSize, nullptr);
            EXPECT_EQ(bufferSize->getValue(), 0);
        }

        {
            auto uri = std::dynamic_pointer_cast<utils::M3U8::UriEntry>(entries[4]);
            ASSERT_NE(uri, nullptr);
            EXPECT_EQ(uri->getUri(), "srt://127.0.0.1:2222");
        }
    }

    {
        auto extinf = std::dynamic_pointer_cast<utils::M3U8::TagEntry>(entries[5]);
        ASSERT_NE(extinf, nullptr);
        EXPECT_EQ(extinf->getTag(), "EXTINF");

        const auto &attrs = extinf->getAttributes();
        ASSERT_EQ(attrs.size(), 4);

        {
            EXPECT_TRUE(attrs[0].getName().empty());
            auto duration = std::dynamic_pointer_cast<utils::M3U8::AttributeValueFloat>(attrs[0].getValue());
            ASSERT_NE(duration, nullptr);
            EXPECT_FLOAT_EQ(duration->getValue(), 10.5f);
        }

        {
            EXPECT_EQ(attrs[1].getName(), "string");
            auto str = std::dynamic_pointer_cast<utils::M3U8::AttributeValueString>(attrs[1].getValue());
            ASSERT_NE(str, nullptr);
            EXPECT_EQ(str->getValue(), "hello");
        }

        {
            EXPECT_EQ(attrs[2].getName(), "enum");
            auto enumVal = std::dynamic_pointer_cast<utils::M3U8::AttributeValueEnum>(attrs[2].getValue());
            ASSERT_NE(enumVal, nullptr);
            EXPECT_EQ(enumVal->getValue(), "value");
        }

        {
            EXPECT_EQ(attrs[3].getName(), "rational");
            auto rational = std::dynamic_pointer_cast<utils::M3U8::AttributeValueRational>(attrs[3].getValue());
            ASSERT_NE(rational, nullptr);
            EXPECT_FLOAT_EQ(rational->getNumerator(), 1.0f);
            EXPECT_FLOAT_EQ(rational->getDenominator(), 2.0f);
        }

        {
            auto uri = std::dynamic_pointer_cast<utils::M3U8::UriEntry>(entries[6]);
            ASSERT_NE(uri, nullptr);
            EXPECT_EQ(uri->getUri(), "file:///media/video.ts");
        }
    }

    {
        auto uri = std::dynamic_pointer_cast<utils::M3U8::UriEntry>(entries[7]);
        ASSERT_NE(uri, nullptr);
        EXPECT_EQ(uri->getUri(), "hls://127.0.0.1:8080/playlist.m3u8");
    }
} 
// A rundown titles each entry with its media file name. That title is tokenized as an attribute
// like any other, and both the '/' and the '.' branches used to convert it with std::stof, which
// throws on a file name. The throw escaped the playlist parser and killed the channel during input
// probing, reported only as "Transcoder thread caught unknown exception".
static const std::string filenameTitles =
R"(
#EXTM3U
#EXTINF:-1,one-cent-call-1080p.mp4
file:///media/one-cent-call-1080p.mp4
#EXTINF:-1,archive/2026/promo.mov
file:///media/promo.mov
)";

TEST(M3U8Test, ParseFileNameTitles) {
    std::istringstream stream(filenameTitles);

    std::unique_ptr<utils::M3U8> m3u8;
    ASSERT_NO_THROW(m3u8 = utils::M3U8::parse(stream));
    ASSERT_NE(m3u8, nullptr);

    const auto &entries = m3u8->getEntries();
    ASSERT_EQ(entries.size(), 5); // EXTM3U + 2 pairs of EXTINF+URI

    {
        // The dotted title falls back to an enum value, and the duration ahead of it still parses.
        auto extinf = std::dynamic_pointer_cast<utils::M3U8::TagEntry>(entries[1]);
        ASSERT_NE(extinf, nullptr);

        const auto &attrs = extinf->getAttributes();
        ASSERT_EQ(attrs.size(), 2);

        auto duration = std::dynamic_pointer_cast<utils::M3U8::AttributeValueInteger>(attrs[0].getValue());
        ASSERT_NE(duration, nullptr);
        EXPECT_EQ(duration->getValue(), -1);

        auto title = std::dynamic_pointer_cast<utils::M3U8::AttributeValueEnum>(attrs[1].getValue());
        ASSERT_NE(title, nullptr);
        EXPECT_EQ(title->getValue(), "one-cent-call-1080p.mp4");
    }

    {
        // A title with a '/' takes the rational branch and must fall back the same way.
        auto extinf = std::dynamic_pointer_cast<utils::M3U8::TagEntry>(entries[3]);
        ASSERT_NE(extinf, nullptr);

        const auto &attrs = extinf->getAttributes();
        ASSERT_EQ(attrs.size(), 2);

        auto title = std::dynamic_pointer_cast<utils::M3U8::AttributeValueEnum>(attrs[1].getValue());
        ASSERT_NE(title, nullptr);
        EXPECT_EQ(title->getValue(), "archive/2026/promo.mov");
    }
}
