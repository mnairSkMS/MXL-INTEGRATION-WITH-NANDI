#include <gtest/gtest.h>

#include <cstring>

#include <gst/gst.h>
#include <gst/mpegts/mpegts.h>

#include "utpl/transcoder/input/dash/emsg.h"

using namespace utpl::transcoder::dash;

namespace {
    // A version 1 'emsg' captured from the DASH-IF live simulator
    // (livesim2/scte35_2/testpic_2s): a splice_insert announcing a 10 s break.
    const std::string EMSG_SCTE35_V1 =
        "00000062656d73670100000000015f90000092c09f5c4340000dbba06adca2d4"
        "75726e3a736374653a7363746533353a323031333a62696e0000fc3025000000"
        "00000000fff014056adca2d47feffe9f5c4340fe000dbba0000000000000249e"
        "239c";

    std::vector<uint8_t> fromHex(const std::string &hex) {
        std::vector<uint8_t> ret;

        for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
            ret.push_back((uint8_t) std::stoul(hex.substr(i, 2), nullptr, 16));
        }

        return ret;
    }

    std::vector<uint8_t> box(const char *type, std::size_t payloadSize, uint8_t fill = 0x00) {
        std::vector<uint8_t> ret(8 + payloadSize, fill);
        uint32_t size = ret.size();

        ret[0] = size >> 24; ret[1] = size >> 16; ret[2] = size >> 8; ret[3] = size;
        memcpy(ret.data() + 4, type, 4);

        return ret;
    }

    // A box using the 64-bit largesize form
    std::vector<uint8_t> largeBox(const char *type, std::size_t payloadSize, uint8_t fill = 0x00) {
        std::vector<uint8_t> ret(16 + payloadSize, fill);
        uint64_t size = ret.size();

        ret[0] = 0; ret[1] = 0; ret[2] = 0; ret[3] = 1;
        memcpy(ret.data() + 4, type, 4);

        for (int i = 0; i < 8; i++) {
            ret[8 + i] = size >> (56 - 8 * i);
        }

        return ret;
    }

    void append(std::vector<uint8_t> &to, const std::vector<uint8_t> &from) {
        to.insert(to.end(), from.begin(), from.end());
    }

    // styp, emsg, moof, mdat (largesize, its payload spelling "emsg" to catch a byte search), emsg
    std::vector<uint8_t> mediaSegment() {
        std::vector<uint8_t> segment;

        append(segment, box("styp", 16));
        append(segment, fromHex(EMSG_SCTE35_V1));
        append(segment, box("moof", 200));

        {
            auto mdat = largeBox("mdat", 4096);

            for (std::size_t i = 16; i + 4 <= mdat.size(); i += 4) {
                memcpy(mdat.data() + i, "emsg", 4);
            }

            append(segment, mdat);
        }

        append(segment, fromHex(EMSG_SCTE35_V1));

        return segment;
    }
}

TEST(DashEmsg, ParsesVersion1Scte35Box) {
    auto raw = fromHex(EMSG_SCTE35_V1);

    EmsgBox emsg;
    ASSERT_TRUE(parseEmsg(raw.data() + 8, raw.size() - 8, emsg));

    EXPECT_EQ(emsg.version, 1);
    EXPECT_EQ(emsg.schemeIdUri, "urn:scte:scte35:2013:bin");
    EXPECT_EQ(emsg.value, "");
    EXPECT_EQ(emsg.timescale, 90000u);
    EXPECT_EQ(emsg.presentationTime, 0x92c09f5c4340ull);
    EXPECT_EQ(emsg.eventDuration, 900000u);
    EXPECT_EQ(emsg.id, 0x6adca2d4u);

    ASSERT_EQ(emsg.messageData.size(), 40u);
    EXPECT_EQ(emsg.messageData[0], 0xFC);
}

TEST(DashEmsg, MessageDataIsASpliceInfoSectionGStreamerAccepts) {
    gst_init(nullptr, nullptr);
    gst_mpegts_initialize();

    auto raw = fromHex(EMSG_SCTE35_V1);

    EmsgBox emsg;
    ASSERT_TRUE(parseEmsg(raw.data() + 8, raw.size() - 8, emsg));

    auto *section = gst_mpegts_section_new(0, (guint8 *) g_memdup2(emsg.messageData.data(), emsg.messageData.size()), emsg.messageData.size());
    ASSERT_NE(section, nullptr);

    const auto *sit = gst_mpegts_section_get_scte_sit(section);
    ASSERT_NE(sit, nullptr);
    EXPECT_EQ(sit->splice_command_type, GST_MTS_SCTE_SPLICE_COMMAND_INSERT);
    ASSERT_EQ(sit->splices->len, 1u);

    auto *event = (GstMpegtsSCTESpliceEvent *) g_ptr_array_index(sit->splices, 0);
    EXPECT_EQ(event->splice_event_id, emsg.id);
    EXPECT_TRUE(event->out_of_network_indicator);
    EXPECT_EQ(event->break_duration, 900000u);

    gst_mpegts_section_unref(section);
}

TEST(DashEmsg, ParsesVersion0Box) {
    std::vector<uint8_t> body = { 0, 0, 0, 0 };
    const std::string scheme = "urn:scte:scte35:2013:bin";

    body.insert(body.end(), scheme.begin(), scheme.end());
    body.push_back(0);
    body.push_back('7');
    body.push_back(0);

    for (uint32_t field : { 90000u, 180000u, 2700000u, 42u }) {
        body.push_back(field >> 24); body.push_back(field >> 16); body.push_back(field >> 8); body.push_back(field);
    }

    body.push_back(0xFC);

    EmsgBox emsg;
    ASSERT_TRUE(parseEmsg(body.data(), body.size(), emsg));

    EXPECT_EQ(emsg.version, 0);
    EXPECT_EQ(emsg.schemeIdUri, scheme);
    EXPECT_EQ(emsg.value, "7");
    EXPECT_EQ(emsg.timescale, 90000u);
    EXPECT_EQ(emsg.presentationTime, 180000u);
    EXPECT_EQ(emsg.eventDuration, 2700000u);
    EXPECT_EQ(emsg.id, 42u);
    EXPECT_EQ(emsg.messageData, std::vector<uint8_t>{ 0xFC });
}

TEST(DashEmsg, RejectsTruncatedAndUnknownVersions) {
    auto raw = fromHex(EMSG_SCTE35_V1);

    EmsgBox emsg;

    // Cut inside the scheme_id_uri: no terminator
    EXPECT_FALSE(parseEmsg(raw.data() + 8, 30, emsg));
    EXPECT_FALSE(parseEmsg(raw.data() + 8, 3, emsg));

    raw[8] = 2;
    EXPECT_FALSE(parseEmsg(raw.data() + 8, raw.size() - 8, emsg));
}

TEST(DashEmsg, RecognisesBinaryScte35SchemesOnly) {
    EXPECT_TRUE(isScte35Scheme("urn:scte:scte35:2013:bin"));
    EXPECT_TRUE(isScte35Scheme("urn:scte:scte35:2014:bin"));

    EXPECT_FALSE(isScte35Scheme("urn:scte:scte35:2014:xml+bin"));
    EXPECT_FALSE(isScte35Scheme("urn:scte:scte35:2013:xml"));
    EXPECT_FALSE(isScte35Scheme("urn:mpeg:dash:event:2012"));
    EXPECT_FALSE(isScte35Scheme("urn:scte:scte35::bin"));
}

TEST(DashEmsg, ScannerFindsTopLevelBoxesAcrossAnyChunking) {
    auto segment = mediaSegment();

    for (std::size_t chunk : { (std::size_t) 1, (std::size_t) 3, (std::size_t) 7, (std::size_t) 100, segment.size() }) {
        EmsgScanner scanner;
        std::vector<EmsgBox> found;

        for (std::size_t pos = 0; pos < segment.size(); pos += chunk) {
            scanner.push(segment.data() + pos, std::min(chunk, segment.size() - pos), found);
        }

        ASSERT_EQ(found.size(), 2u) << "chunk size " << chunk;
        EXPECT_EQ(found[0].id, 0x6adca2d4u);
        EXPECT_EQ(found[1].messageData.size(), 40u);
    }
}

TEST(DashEmsg, ScannerStopsAtSizeZeroBoxUntilReset) {
    EmsgScanner scanner;
    std::vector<EmsgBox> found;

    auto toEnd = box("mdat", 32);
    toEnd[0] = toEnd[1] = toEnd[2] = toEnd[3] = 0;

    auto emsg = fromHex(EMSG_SCTE35_V1);

    scanner.push(toEnd.data(), toEnd.size(), found);
    scanner.push(emsg.data(), emsg.size(), found);
    EXPECT_TRUE(found.empty());

    scanner.reset();
    scanner.push(emsg.data(), emsg.size(), found);
    EXPECT_EQ(found.size(), 1u);
}

TEST(DashEmsg, ScannerSkipsOversizedEmsg) {
    EmsgScanner scanner;
    std::vector<EmsgBox> found;

    auto huge = box("emsg", EmsgScanner::MAX_EMSG_SIZE + 1);
    auto emsg = fromHex(EMSG_SCTE35_V1);

    scanner.push(huge.data(), huge.size(), found);
    scanner.push(emsg.data(), emsg.size(), found);

    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].id, 0x6adca2d4u);
}

TEST(DashEmsg, ResetRecoversFromAnAbortedDownload) {
    EmsgScanner scanner;
    std::vector<EmsgBox> found;

    auto segment = mediaSegment();
    auto emsg    = fromHex(EMSG_SCTE35_V1);

    // A download dropped half way through the moof
    scanner.push(segment.data(), 24 + emsg.size() + 50, found);
    ASSERT_EQ(found.size(), 1u);

    scanner.reset();
    scanner.push(segment.data(), segment.size(), found);

    EXPECT_EQ(found.size(), 3u);
}
