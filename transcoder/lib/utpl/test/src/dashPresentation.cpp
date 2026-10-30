#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <gst/gst.h>
#include <gst/mpegts/mpegts.h>
#include <pugixml.hpp>

#include "utpl/common/utils.h"

#include "utpl/hls/types.h"
#include "utpl/transcoder/event/scheduler/scte.h"

#include "utpl/transcoder/output/dash/presentation.h"

using namespace utpl;
using namespace utpl::transcoder;

namespace {
    // A live manifest as dashsink2 (patched) writes it: one video and one audio Representation.
    std::string dashsink2Manifest(const std::string &startTime, const std::string &type = "dynamic") {
        return
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" profiles=\"urn:mpeg:dash:profile:isoff-live:2011\" type=\"" + type + "\""
            " availabilityStartTime=\"" + startTime + "\" publishTime=\"" + startTime + "\" minimumUpdatePeriod=\"PT2S\" minBufferTime=\"PT4S\">"
            "<Period id=\"P0\" start=\"PT0S\">"
            "<AdaptationSet id=\"video\" contentType=\"video\" mimeType=\"video/mp4\" segmentAlignment=\"true\">"
            "<Representation id=\"video_0\" bandwidth=\"3000000\" codecs=\"avc1.64001F\" width=\"1280\" height=\"720\">"
            "<SegmentTemplate media=\"video_0_segment_$Number$.cmfv\" initialization=\"video_0_init.cmfi\" startNumber=\"0\" duration=\"2000\" timescale=\"1000\"/>"
            "</Representation></AdaptationSet>"
            "<AdaptationSet id=\"audio\" contentType=\"audio\" mimeType=\"audio/mp4\" segmentAlignment=\"true\">"
            "<Representation id=\"audio_1\" bandwidth=\"128000\" codecs=\"mp4a.40.2\">"
            "<SegmentTemplate media=\"audio_1_segment_$Number$.cmfa\" initialization=\"audio_1_init.cmfi\" startNumber=\"0\" duration=\"2000\" timescale=\"1000\"/>"
            "</Representation></AdaptationSet>"
            "</Period></MPD>\n";
    }

    dash::ManifestOptions liveOptions() {
        dash::ManifestOptions options;

        options.live              = true;
        options.segmentDuration   = 2000;
        options.windowSegments    = 15;
        options.presentationDelay = 6000;

        return options;
    }

    pugi::xml_node period(pugi::xml_document &doc) {
        return doc.child("MPD").child("Period");
    }

    // splice_insert, out_of_network_indicator set, event 12345, 30 s auto-return break
    // (the same section the HLS SCTE tests use).
    const std::string SPLICE_INSERT_OUT = "/DAlAAAAAAAAAP/wFAUAADA5f+/+cr0AUP4AKTLgAAAAAAAAa/2GtQ==";

    Scte35Data spliceInsertAt(GstClockTime runningTime) {
        auto raw     = utils::fromBase64(SPLICE_INSERT_OUT);
        auto section = scheduler::sectionFromBase64(raw, runningTime, GST_MTS_SCTE_SPLICE_COMMAND_INSERT);

        Scte35Data data;

        data.setSection(section);
        data.setRunningTime(runningTime);
        data.setTimingMap(scheduler::buildRunningTimeMap(section, runningTime));

        gst_mpegts_section_unref(section);

        return data;
    }

    bool waitForFile(const std::filesystem::path &path, bool exists = true) {
        for (int i = 0; i < 200; i++) {
            if (std::filesystem::exists(path) == exists) {
                return true;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        return false;
    }

    std::string readFile(const std::filesystem::path &path) {
        std::ifstream f(path);

        return std::string(std::istreambuf_iterator<char>(f), {});
    }
}

TEST(DashManifest, SingleOutputKeepsNamesAndAddsLiveAttributes) {
    std::string pinned;

    auto xml = dash::mergeManifests({{ "", dashsink2Manifest("2026-10-27T10:00:00Z") }}, liveOptions(), {}, pinned);

    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_string(xml.c_str()));

    auto mpd = doc.child("MPD");

    EXPECT_STREQ(mpd.attribute("type").value(), "dynamic");
    EXPECT_STREQ(mpd.attribute("timeShiftBufferDepth").value(), "PT30S");
    EXPECT_STREQ(mpd.attribute("suggestedPresentationDelay").value(), "PT6S");
    EXPECT_EQ(pinned, "2026-10-27T10:00:00Z");

    auto video = period(doc).find_child_by_attribute("AdaptationSet", "contentType", "video");
    auto rep   = video.child("Representation");

    EXPECT_STREQ(rep.attribute("id").value(), "video_0");
    EXPECT_STREQ(rep.child("SegmentTemplate").attribute("media").value(), "video_0_segment_$Number$.cmfv");
    EXPECT_STREQ(rep.child("SegmentTemplate").attribute("initialization").value(), "video_0_init.cmfi");
    EXPECT_FALSE(period(doc).child("EventStream"));
}

TEST(DashManifest, GroupMergesRepresentationsUnderOneStartTime) {
    std::string pinned;

    std::vector<dash::VariantManifest> variants = {
        { "variant_720p", dashsink2Manifest("2026-10-27T10:00:00Z") },
        { "variant_360p", dashsink2Manifest("2026-10-27T10:00:01Z") },
    };

    auto xml = dash::mergeManifests(variants, liveOptions(), {}, pinned);

    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_string(xml.c_str()));

    // One AdaptationSet per content type, holding every variant's Representation.
    EXPECT_EQ(std::distance(period(doc).children("AdaptationSet").begin(), period(doc).children("AdaptationSet").end()), 2);

    auto video = period(doc).find_child_by_attribute("AdaptationSet", "contentType", "video");
    std::vector<std::string> ids, media;

    for (auto rep : video.children("Representation")) {
        ids.push_back(rep.attribute("id").value());
        media.push_back(rep.child("SegmentTemplate").attribute("media").value());
    }

    EXPECT_EQ(ids,   (std::vector<std::string>{ "variant_720p_video_0", "variant_360p_video_0" }));
    EXPECT_EQ(media, (std::vector<std::string>{ "variant_720p_video_0_segment_$Number$.cmfv", "variant_360p_video_0_segment_$Number$.cmfv" }));

    EXPECT_STREQ(doc.child("MPD").attribute("availabilityStartTime").value(), "2026-10-27T10:00:00Z");

    // A later manifest with a different start time still publishes the pinned one.
    variants[0].xml = dashsink2Manifest("2026-10-27T10:00:05Z");
    xml = dash::mergeManifests(variants, liveOptions(), {}, pinned);

    ASSERT_TRUE(doc.load_string(xml.c_str()));
    EXPECT_STREQ(doc.child("MPD").attribute("availabilityStartTime").value(), "2026-10-27T10:00:00Z");
}

TEST(DashManifest, Scte35BecomesEventStream) {
    std::string pinned;
    std::vector<hls::ScteEvent> events = { hls::ScteEvent(spliceInsertAt(10 * GST_SECOND)) };

    auto xml = dash::mergeManifests({{ "", dashsink2Manifest("2026-10-27T10:00:00Z") }}, liveOptions(), events, pinned);

    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_string(xml.c_str()));

    auto stream = period(doc).first_child();

    ASSERT_STREQ(stream.name(), "EventStream") << "EventStream must precede the AdaptationSets";
    EXPECT_STREQ(stream.attribute("schemeIdUri").value(), "urn:scte:scte35:2014:xml+bin");
    EXPECT_STREQ(stream.attribute("timescale").value(), "1000");

    auto event = stream.child("Event");

    EXPECT_STREQ(event.attribute("presentationTime").value(), "10000");
    EXPECT_STREQ(event.attribute("duration").value(), "30000");
    EXPECT_STREQ(event.child("Signal").attribute("xmlns").value(), "http://www.scte.org/schemas/35/2016");
    EXPECT_FALSE(std::string(event.child("Signal").child("Binary").text().get()).empty());
}

TEST(DashManifest, VodLeavesStaticManifestAlone) {
    std::string pinned;
    auto options = liveOptions();

    options.live = false;

    auto xml = dash::mergeManifests({{ "", dashsink2Manifest("2026-10-27T10:00:00Z", "static") }}, options, {}, pinned);

    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_string(xml.c_str()));

    EXPECT_FALSE(doc.child("MPD").attribute("timeShiftBufferDepth"));
    EXPECT_TRUE(pinned.empty());
}

TEST(DashPresentation, UploadsObjectsKeepsWindowAndSkipsFinalStaticManifest) {
    auto dir = std::filesystem::temp_directory_path() / ("utpl-dash-test-" + std::to_string(getpid()));
    std::filesystem::remove_all(dir);

    DashOutputTransportParameters parameters;

    parameters.setSegmentDuration(2000);
    parameters.setWindowSegments(2);

    dash::Presentation presentation("dash+file://" + dir.string(), parameters);

    int variant = presentation.addVariant("");

    presentation.start();

    presentation.putObject(variant, dash::Presentation::ObjectKind::INIT, "video_0_init.cmfi", "init");

    // Window 2 plus the 2 extra kept segments: the fifth segment removes the first.
    for (int i = 0; i < 5; i++) {
        presentation.putObject(variant, dash::Presentation::ObjectKind::SEGMENT, "video_0_segment_" + std::to_string(i) + ".cmfv", "seg");
    }

    presentation.putObject(variant, dash::Presentation::ObjectKind::MANIFEST, "manifest.mpd", dashsink2Manifest("2026-10-27T10:00:00Z"));

    EXPECT_TRUE(waitForFile(dir / "manifest.mpd"));
    EXPECT_TRUE(waitForFile(dir / "video_0_segment_4.cmfv"));
    EXPECT_TRUE(waitForFile(dir / "video_0_segment_0.cmfv", false)) << "segment 0 left the window and should be removed";
    EXPECT_TRUE(std::filesystem::exists(dir / "video_0_segment_1.cmfv"));
    EXPECT_EQ(readFile(dir / "video_0_init.cmfi"), "init");

    auto live = readFile(dir / "manifest.mpd");

    // On stop dashsink2 emits a static manifest; for a live output it must not replace the live one.
    presentation.putObject(variant, dash::Presentation::ObjectKind::MANIFEST, "manifest.mpd", dashsink2Manifest("2026-10-27T10:00:00Z", "static"));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(readFile(dir / "manifest.mpd"), live);
    EXPECT_NE(live.find("type=\"dynamic\""), std::string::npos);

    // Files are written beside the target and renamed into place; nothing is left behind.
    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
        EXPECT_NE(entry.path().extension(), ".tmp") << entry.path();
    }

    presentation.stop();

    std::filesystem::remove_all(dir);
}

TEST(DashPresentation, GroupWaitsForEveryVariantBeforePublishing) {
    auto dir = std::filesystem::temp_directory_path() / ("utpl-dash-group-test-" + std::to_string(getpid()));
    std::filesystem::remove_all(dir);

    DashOutputTransportParameters parameters;

    parameters.setBaseFileName("live");

    dash::Presentation presentation("dash+file://" + dir.string(), parameters);

    int hi = presentation.addVariant("live_variant_hi");
    int lo = presentation.addVariant("live_variant_lo");

    presentation.start();

    presentation.putObject(hi, dash::Presentation::ObjectKind::MANIFEST, "manifest.mpd", dashsink2Manifest("2026-10-27T10:00:00Z"));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(std::filesystem::exists(dir / "live.mpd")) << "published before every variant had a manifest";

    presentation.putObject(lo, dash::Presentation::ObjectKind::MANIFEST, "manifest.mpd", dashsink2Manifest("2026-10-27T10:00:01Z"));

    ASSERT_TRUE(waitForFile(dir / "live.mpd"));

    auto xml = readFile(dir / "live.mpd");

    EXPECT_NE(xml.find("live_variant_hi_video_0"), std::string::npos);
    EXPECT_NE(xml.find("live_variant_lo_video_0"), std::string::npos);

    presentation.stop();

    std::filesystem::remove_all(dir);
}

int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    gst_mpegts_initialize();

    testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
