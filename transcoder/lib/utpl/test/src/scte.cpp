#include <gtest/gtest.h>

#include <gst/gst.h>
#include <gst/mpegts/mpegts.h>
#include <tsduck/tsduck.h>
#include <tsduck/tsSpliceSegmentationDescriptor.h>

#include "utpl/common/utils.h"
#include "utpl/common/utils/gstreamer.h"

#include "utpl/hls/playlist.h"
#include "utpl/hls/types.h"

#include "utpl/transcoder/event/scheduler/scte.h"
#include "utpl/transcoder/event.h"

using namespace utpl;
using namespace utpl::transcoder;

// Complete splice_info_sections built to SCTE 35 section 9.2, cross-checked against an
// independent SCTE-35 decoder. Their CRC_32 values are valid.

// splice_insert, out_of_network_indicator set, event 12345, 30s auto-return break.
static const std::string SPLICE_INSERT_OUT =
    "/DAlAAAAAAAAAP/wFAUAADA5f+/+cr0AUP4AKTLgAAAAAAAAa/2GtQ==";

// splice_insert with break_duration_auto_return = 0: the 45s duration it carries is
// advisory, and the break closes only when a return to network arrives. Captured from
// this system's own SSAI panel with auto-return unticked.
static const std::string SPLICE_INSERT_OUT_OPEN_ENDED =
    "/DAlAAAAAAAAAP/wFAVqmBaMf+/yCVN3034APcxQAAAAAAAADtjY4Q==";

// The matching return to network: same event id, out_of_network_indicator clear.
static const std::string SPLICE_INSERT_IN =
    "/DAgAAAAAAAAAP/wDwUAADA5f0/+cr0AUAAAAAAAAEFvN+0=";

// time_signal carrying a Provider Placement Opportunity Start (0x34), 60s, with a UPID.
static const std::string TIME_SIGNAL =
    "/DA/AAAAAAAAAP/wBQb+cr0AUAApAidDVUVJqrvM3X/fAABSZcAMEVNLQU5ESEEt"
    "VEVTVC1VUElENAEBAADYWc/1";

// A well-formed section carrying a command that cannot be injected.
static const std::string SPLICE_NULL = "/DARAAAAAAAAAP/wAAAAAHpPv/8=";

static const GstClockTime SPLICE_PTS = 90 * GST_SECOND;
static const uint64_t     PTS_MASK   = 0x1FFFFFFFFull;


static const GstMpegtsSCTESIT *sitOf(GstMpegtsSection *section) {
    return gst_mpegts_section_get_scte_sit(section);
}


TEST(scteSection, spliceInsertPayloadSurvivesRoundTrip) {
    auto raw = utils::fromBase64(SPLICE_INSERT_OUT);

    auto *section = scheduler::sectionFromBase64(
        raw, SPLICE_PTS, GST_MTS_SCTE_SPLICE_COMMAND_INSERT
    );

    ASSERT_NE(section, nullptr);

    const auto *sit = sitOf(section);
    ASSERT_NE(sit, nullptr);

    EXPECT_EQ(sit->splice_command_type, GST_MTS_SCTE_SPLICE_COMMAND_INSERT);
    ASSERT_EQ(sit->splices->len, 1u);

    auto *event = (GstMpegtsSCTESpliceEvent *) g_ptr_array_index(sit->splices, 0);

    // Everything the operator authored is preserved, splice time included...
    EXPECT_EQ(event->splice_event_id, 12345u);
    EXPECT_TRUE(event->out_of_network_indicator);
    EXPECT_TRUE(event->duration_flag);
    EXPECT_EQ(GST_UTILS_MPEGTIME_TO_GSTTIME(event->break_duration) / GST_MSECOND, 30u * 1000u);
    EXPECT_TRUE(event->program_splice_time_specified);

    // ...and the event is moved onto this output's timeline with pts_adjustment, which a
    // receiver adds to every splice time in the section.
    EXPECT_EQ(
        (event->program_splice_time + sit->pts_adjustment) & PTS_MASK,
        GST_UTILS_GSTTIME_TO_MPEGTIME(SPLICE_PTS)
    );

    gst_mpegts_section_unref(section);
}


TEST(scteSection, onlyPtsAdjustmentAndCrcAreRewritten) {
    auto raw = utils::fromBase64(SPLICE_INSERT_OUT);

    auto *section = scheduler::sectionFromBase64(
        raw, SPLICE_PTS, GST_MTS_SCTE_SPLICE_COMMAND_INSERT
    );
    ASSERT_NE(section, nullptr);

    gsize size = 0;
    const auto *out = gst_mpegts_section_packetize(section, &size);
    ASSERT_NE(out, nullptr);
    ASSERT_EQ(size, raw.size());

    // The operator's bytes must reach the wire untouched apart from the 5 pts_adjustment
    // bytes (4..8, sharing byte 4 with the encryption fields) and the trailing CRC_32.
    for (gsize i = 0; i < size; i++) {
        const bool isPtsAdjustment = (i >= 4 && i <= 8);
        const bool isCrc           = (i >= size - 4);

        if (isPtsAdjustment || isCrc) {
            continue;
        }

        EXPECT_EQ(out[i], raw[i]) << "byte " << i << " was modified";
    }

    // Byte 4 holds encrypted_packet and encryption_algorithm alongside one pts_adjustment
    // bit; those seven bits must survive.
    EXPECT_EQ(out[4] & 0xFE, raw[4] & 0xFE);

    // And the rewritten CRC must actually be correct.
    EXPECT_EQ(
        ts::CRC32(out, size - 4).value(),
        (uint32_t) ((out[size - 4] << 24) | (out[size - 3] << 16) |
                    (out[size - 2] <<  8) |  out[size - 1])
    );

    gst_mpegts_section_unref(section);
}


TEST(scteSection, timeSignalSegmentationDescriptorSurvivesRoundTrip) {
    auto raw = utils::fromBase64(TIME_SIGNAL);

    auto *section = scheduler::sectionFromBase64(
        raw, SPLICE_PTS, GST_MTS_SCTE_SPLICE_COMMAND_TIME
    );

    ASSERT_NE(section, nullptr);

    const auto *sit = sitOf(section);
    ASSERT_NE(sit, nullptr);

    EXPECT_EQ(sit->splice_command_type, GST_MTS_SCTE_SPLICE_COMMAND_TIME);
    EXPECT_TRUE(sit->splice_time_specified);
    EXPECT_EQ(
        (sit->splice_time + sit->pts_adjustment) & PTS_MASK,
        GST_UTILS_GSTTIME_TO_MPEGTIME(SPLICE_PTS)
    );

    // The segmentation descriptor is the payload that carries commercial meaning; it must
    // come through the SIT round trip byte-identical.
    ASSERT_NE(sit->descriptors, nullptr);
    ASSERT_EQ(sit->descriptors->len, 1u);

    auto *gstDescriptor = (GstMpegtsDescriptor *) g_ptr_array_index(sit->descriptors, 0);
    ASSERT_EQ(gstDescriptor->tag, ts::DID_SPLICE_SEGMENT);

    ts::DuckContext duckCtx;
    ts::Descriptor generic(gstDescriptor->data, gstDescriptor->length + 2);
    ASSERT_TRUE(generic.isValid());

    ts::SpliceSegmentationDescriptor descriptor(duckCtx, generic);
    ASSERT_TRUE(descriptor.isValid());

    EXPECT_EQ(descriptor.segmentation_event_id, 0xAABBCCDDu);
    EXPECT_EQ(descriptor.segmentation_type_id,  0x34);
    ASSERT_TRUE(descriptor.segmentation_duration.has_value());
    EXPECT_EQ(
        GST_UTILS_MPEGTIME_TO_GSTTIME(descriptor.segmentation_duration.value()) / GST_MSECOND,
        60u * 1000u
    );

    // The UPID is opaque to us and must not be reinterpreted on the way through.
    EXPECT_EQ(descriptor.segmentation_upid.size(), std::string("SKANDHA-TEST-UPID").size());

    gst_mpegts_section_unref(section);
}


TEST(scteSection, rejectsCommandTypeMismatch) {
    auto timeSignal   = utils::fromBase64(TIME_SIGNAL);
    auto spliceInsert = utils::fromBase64(SPLICE_INSERT_OUT);

    EXPECT_EQ(
        scheduler::sectionFromBase64(timeSignal, SPLICE_PTS, GST_MTS_SCTE_SPLICE_COMMAND_INSERT),
        nullptr
    );

    EXPECT_EQ(
        scheduler::sectionFromBase64(spliceInsert, SPLICE_PTS, GST_MTS_SCTE_SPLICE_COMMAND_TIME),
        nullptr
    );
}


TEST(scteSection, rejectsUninjectableAndMalformedSections) {
    // splice_null is well formed but carries nothing that can be scheduled.
    EXPECT_EQ(
        scheduler::sectionFromBase64(
            utils::fromBase64(SPLICE_NULL), SPLICE_PTS, GST_MTS_SCTE_SPLICE_COMMAND_INSERT
        ),
        nullptr
    );

    EXPECT_EQ(
        scheduler::sectionFromBase64({}, SPLICE_PTS, GST_MTS_SCTE_SPLICE_COMMAND_INSERT),
        nullptr
    );

    std::vector<uint8_t> notASection = {0x00, 0x01, 0x02, 0x03, 0x04};
    EXPECT_EQ(
        scheduler::sectionFromBase64(notASection, SPLICE_PTS, GST_MTS_SCTE_SPLICE_COMMAND_INSERT),
        nullptr
    );
}


TEST(scteSection, rejectsSectionsWhoseLengthDisagreesWithTheBuffer) {
    // section_length is the only statement of where the section ends, and the CRC is
    // written relative to the end of the buffer. If the two disagree the CRC lands
    // outside the section that is actually emitted, so the buffer is refused rather
    // than silently trimmed to whichever part looks right.
    const auto valid = utils::fromBase64(SPLICE_INSERT_OUT);

    {
        // Trailing bytes: parses cleanly, but everything past section_length would be
        // dropped - taking the recomputed CRC with it.
        auto padded = valid;
        padded.insert(padded.end(), 8, 0xAA);

        EXPECT_EQ(
            scheduler::sectionFromBase64(padded, SPLICE_PTS, GST_MTS_SCTE_SPLICE_COMMAND_INSERT),
            nullptr
        ) << "a section with trailing bytes must not be injected";
    }

    {
        // The opposite direction: section_length promises more than was supplied.
        auto truncated = valid;
        truncated.pop_back();

        EXPECT_EQ(
            scheduler::sectionFromBase64(truncated, SPLICE_PTS, GST_MTS_SCTE_SPLICE_COMMAND_INSERT),
            nullptr
        ) << "a section shorter than its declared length must not be injected";
    }

    // The unmodified section still goes through, so the guard is not simply refusing
    // everything.
    auto *section = scheduler::sectionFromBase64(
        valid, SPLICE_PTS, GST_MTS_SCTE_SPLICE_COMMAND_INSERT
    );
    ASSERT_NE(section, nullptr);
    gst_mpegts_section_unref(section);
}


TEST(scteSection, runningTimeMapIsKeyedOffTheSection) {
    const GstClockTime runningTime = 42 * GST_SECOND;

    {
        auto *section = scheduler::sectionFromBase64(
            utils::fromBase64(SPLICE_INSERT_OUT), SPLICE_PTS, GST_MTS_SCTE_SPLICE_COMMAND_INSERT
        );
        ASSERT_NE(section, nullptr);

        auto *map = scheduler::buildRunningTimeMap(section, runningTime);
        ASSERT_NE(map, nullptr);

        // The key carries the event id read back out of the section, not one we assumed.
        guint64 value = 0;
        EXPECT_TRUE(gst_structure_get_uint64(map, "event-12345-splice-time", &value));
        EXPECT_EQ(value, runningTime);

        gst_structure_free(map);
        gst_mpegts_section_unref(section);
    }

    {
        auto *section = scheduler::sectionFromBase64(
            utils::fromBase64(TIME_SIGNAL), SPLICE_PTS, GST_MTS_SCTE_SPLICE_COMMAND_TIME
        );
        ASSERT_NE(section, nullptr);

        auto *map = scheduler::buildRunningTimeMap(section, runningTime);
        ASSERT_NE(map, nullptr);

        guint64 value = 0;
        EXPECT_TRUE(gst_structure_get_uint64(map, "splice-time", &value));
        EXPECT_EQ(value, runningTime);

        gst_structure_free(map);
        gst_mpegts_section_unref(section);
    }
}


// ---------------------------------------------------------------------------------------
// HLS ad-marker flavours
// ---------------------------------------------------------------------------------------

namespace {
    class FakeSegment : public hls::Segment {
        public:
            FakeSegment(const std::string &name, int sequence, GstClockTime begin, Timestamp durationMs)
                : _name(name), _pdt("1970-01-01T00:00:00.000Z") {
                this->_sequence = sequence;
                this->_begin    = begin;
                this->_duration = durationMs;
            }

            void setBeginRunningTime(GstClockTime time) override { this->_begin = time; }
            GstClockTime getBeginRunningTime() const override    { return this->_begin; }
            void setEndRunningTime(GstClockTime time) override    { this->_end = time; }
            GstClockTime getEndRunningTime() override             { return this->_end; }

            Timestamp getDuration() const override        { return this->_duration; }
            void setDuration(Timestamp duration) override { this->_duration = duration; }

            const std::string &getName() const override { return this->_name; }
            int getSequence() const override            { return this->_sequence; }
            uint64_t getBitrate() const override        { return 0; }

            const std::string &getProgramDateTime() override { return this->_pdt; }
            std::string setProgramDateTime() override        { return this->_pdt; }

            bool isEmpty() const override     { return false; }
            bool isBuffered() const override  { return true; }
            bool isBuffering() const override { return false; }
            void setEmpty() override     {}
            void setBuffering() override {}
            void setBuffered() override  {}

            std::shared_ptr<hls::SegmentStorage> getStorage() override { return nullptr; }

        private:
            std::string  _name;
            std::string  _pdt;
            int          _sequence  = 0;
            GstClockTime _begin     = 0;
            GstClockTime _end       = 0;
            Timestamp    _duration  = 0;
    };

    // The break opens here, and the segments are laid out across it.
    const GstClockTime BREAK_START   = 10 * GST_SECOND;
    const Timestamp    SEGMENT_MS    = 6000;

    hls::ScteEvent makeBreakStartEvent() {
        auto raw = utils::fromBase64(SPLICE_INSERT_OUT);

        auto *section = scheduler::sectionFromBase64(
            raw, BREAK_START, GST_MTS_SCTE_SPLICE_COMMAND_INSERT
        );

        transcoder::Scte35Data data;

        data.setSection(section);
        data.setRunningTime(BREAK_START);
        data.setTimingMap(scheduler::buildRunningTimeMap(section, BREAK_START));

        gst_mpegts_section_unref(section);

        return hls::ScteEvent(data);
    }

    std::string serializeWith(const std::vector<hls::ScteMarker> &markers) {
        hls::MediaPlaylist playlist("variant", Container::MPEGTS, {MediaType::VIDEO}, 6);

        std::vector<std::shared_ptr<hls::Segment>> segments = {
            std::make_shared<FakeSegment>("seg0.ts", 0, BREAK_START,                  SEGMENT_MS),
            std::make_shared<FakeSegment>("seg1.ts", 1, BREAK_START + 6 * GST_SECOND, SEGMENT_MS),
        };

        std::vector<hls::ScteEvent> events;
        events.push_back(makeBreakStartEvent());

        return playlist.serialize("", false, nullptr, segments, events, markers);
    }

    // Same fixture, but the avail opens on a time_signal (segmentation type 0x34,
    // Provider Placement Opportunity Start, 60s) instead of a splice_insert.
    hls::ScteEvent makeTimeSignalBreakStartEvent() {
        auto raw = utils::fromBase64(TIME_SIGNAL);

        auto *section = scheduler::sectionFromBase64(
            raw, BREAK_START, GST_MTS_SCTE_SPLICE_COMMAND_TIME
        );

        transcoder::Scte35Data data;

        data.setSection(section);
        data.setRunningTime(BREAK_START);
        data.setTimingMap(scheduler::buildRunningTimeMap(section, BREAK_START));

        gst_mpegts_section_unref(section);

        return hls::ScteEvent(data);
    }

    // Reproduces the passthrough case where the demuxer's running-time-map does not
    // carry the key getSpliceTime() needs for this command type.
    hls::ScteEvent makeTimeSignalEventWithoutTimingMap() {
        auto raw = utils::fromBase64(TIME_SIGNAL);

        auto *section = scheduler::sectionFromBase64(
            raw, BREAK_START, GST_MTS_SCTE_SPLICE_COMMAND_TIME
        );

        transcoder::Scte35Data data;

        data.setSection(section);
        data.setRunningTime(BREAK_START);
        // deliberately no setTimingMap()

        gst_mpegts_section_unref(section);

        return hls::ScteEvent(data);
    }

    std::string serializeEvent(const hls::ScteEvent &event,
                               const std::vector<hls::ScteMarker> &markers) {
        hls::MediaPlaylist playlist("variant", Container::MPEGTS, {MediaType::VIDEO}, 6);

        std::vector<std::shared_ptr<hls::Segment>> segments = {
            std::make_shared<FakeSegment>("seg0.ts", 0, BREAK_START,                  SEGMENT_MS),
            std::make_shared<FakeSegment>("seg1.ts", 1, BREAK_START + 6 * GST_SECOND, SEGMENT_MS),
        };

        std::vector<hls::ScteEvent> events;
        events.push_back(event);

        return playlist.serialize("", false, nullptr, segments, events, markers);
    }

    hls::ScteEvent makeOpenEndedBreakStartEvent() {
        auto raw = utils::fromBase64(SPLICE_INSERT_OUT_OPEN_ENDED);

        auto *section = scheduler::sectionFromBase64(
            raw, BREAK_START, GST_MTS_SCTE_SPLICE_COMMAND_INSERT
        );

        transcoder::Scte35Data data;

        data.setSection(section);
        data.setRunningTime(BREAK_START);
        data.setTimingMap(scheduler::buildRunningTimeMap(section, BREAK_START));

        gst_mpegts_section_unref(section);

        return hls::ScteEvent(data);
    }

    // Segments reaching well past the advisory 45s, so a break that closed itself on
    // that duration would be visible as a CUE-IN here.
    std::string serializeOpenEndedWith(const std::vector<hls::ScteMarker> &markers) {
        hls::MediaPlaylist playlist("variant", Container::MPEGTS, {MediaType::VIDEO}, 6);

        std::vector<std::shared_ptr<hls::Segment>> segments = {
            std::make_shared<FakeSegment>("seg0.ts", 0, BREAK_START,                   SEGMENT_MS),
            std::make_shared<FakeSegment>("seg1.ts", 1, BREAK_START +  6 * GST_SECOND, SEGMENT_MS),
            std::make_shared<FakeSegment>("seg2.ts", 2, BREAK_START + 60 * GST_SECOND, SEGMENT_MS),
        };

        std::vector<hls::ScteEvent> events;
        events.push_back(makeOpenEndedBreakStartEvent());

        return playlist.serialize("", false, nullptr, segments, events, markers);
    }

    // The matching return to network, placed at an arbitrary moment inside a segment
    // rather than near its edge - which is where an operator's cue-in actually lands.
    hls::ScteEvent makeMidSegmentReturnEvent(GstClockTime at) {
        auto raw = utils::fromBase64(SPLICE_INSERT_IN);

        auto *section = scheduler::sectionFromBase64(
            raw, at, GST_MTS_SCTE_SPLICE_COMMAND_INSERT
        );

        transcoder::Scte35Data data;

        data.setSection(section);
        data.setRunningTime(at);
        data.setTimingMap(scheduler::buildRunningTimeMap(section, at));

        gst_mpegts_section_unref(section);

        return hls::ScteEvent(data);
    }

    std::string serializeClosedMidSegment(const std::vector<hls::ScteMarker> &markers) {
        hls::MediaPlaylist playlist("variant", Container::MPEGTS, {MediaType::VIDEO}, 6);

        std::vector<std::shared_ptr<hls::Segment>> segments = {
            std::make_shared<FakeSegment>("seg0.ts", 0, BREAK_START,                   SEGMENT_MS),
            std::make_shared<FakeSegment>("seg1.ts", 1, BREAK_START +  6 * GST_SECOND, SEGMENT_MS),
            std::make_shared<FakeSegment>("seg2.ts", 2, BREAK_START + 12 * GST_SECOND, SEGMENT_MS),
        };

        std::vector<hls::ScteEvent> events;
        events.push_back(makeBreakStartEvent());
        // 3 s into seg1: a quarter-segment margin either side of a boundary misses this.
        events.push_back(makeMidSegmentReturnEvent(BREAK_START + 9 * GST_SECOND));

        return playlist.serialize("", false, nullptr, segments, events, markers);
    }

    std::string serializeTimeSignalWith(const std::vector<hls::ScteMarker> &markers) {
        hls::MediaPlaylist playlist("variant", Container::MPEGTS, {MediaType::VIDEO}, 6);

        std::vector<std::shared_ptr<hls::Segment>> segments = {
            std::make_shared<FakeSegment>("seg0.ts", 0, BREAK_START,                  SEGMENT_MS),
            std::make_shared<FakeSegment>("seg1.ts", 1, BREAK_START + 6 * GST_SECOND, SEGMENT_MS),
        };

        std::vector<hls::ScteEvent> events;
        events.push_back(makeTimeSignalBreakStartEvent());

        return playlist.serialize("", false, nullptr, segments, events, markers);
    }
}


TEST(hlsScteMarkers, elementalFlavourIsUnchanged) {
    auto out = serializeWith({hls::ScteMarker::ELEMENTAL});

    EXPECT_NE(out.find("#EXT-X-CUE-OUT:30.000"), std::string::npos)   << out;
    EXPECT_NE(out.find("#EXT-X-CUE-OUT-CONT:6.000/30.000"), std::string::npos) << out;

    // The base64 belongs to the enhanced flavour only.
    EXPECT_EQ(out.find("EXT-OATCLS-SCTE35"), std::string::npos) << out;
    EXPECT_EQ(out.find("SCTE35="),           std::string::npos) << out;
}


TEST(hlsScteMarkers, enhancedFlavourCarriesTheSectionAsBase64) {
    auto out = serializeWith({hls::ScteMarker::ENHANCED});

    auto expected = makeBreakStartEvent().getBase64();
    ASSERT_FALSE(expected.empty());

    // The section rides ahead of the cue-out, unquoted, and again on the continuation.
    EXPECT_NE(out.find("#EXT-OATCLS-SCTE35:" + expected), std::string::npos) << out;
    EXPECT_NE(out.find("SCTE35=" + expected), std::string::npos)             << out;

    EXPECT_NE(out.find("#EXT-X-CUE-OUT:30.000"), std::string::npos) << out;
    EXPECT_NE(out.find("ElapsedTime=6.000"),     std::string::npos) << out;
    EXPECT_NE(out.find("Duration=30.000"),       std::string::npos) << out;

    // Unquoted: a quoted value would break the OATCLS convention.
    EXPECT_EQ(out.find("#EXT-OATCLS-SCTE35:\""), std::string::npos) << out;
}


TEST(hlsScteMarkers, elementalIsTheDefaultShapeAndEnhancedOnlyAdds) {
    // serialize() has never gated the cue tags themselves on the marker list - whatever
    // events it is handed get the Elemental shape. The gate lives upstream in
    // Streamer::putScteData(), which discards SCTE events outright while the configured
    // marker list is empty, so serialize() never sees any in that case.
    //
    // The marker list therefore selects only the enhanced *additions*, and this pins that:
    // an empty list must produce byte-identical output to an explicit ELEMENTAL list.
    EXPECT_EQ(serializeWith({}), serializeWith({hls::ScteMarker::ELEMENTAL}));

    auto out = serializeWith({});

    EXPECT_NE(out.find("#EXT-X-CUE-OUT:30.000"), std::string::npos) << out;
    EXPECT_EQ(out.find("EXT-OATCLS-SCTE35"),     std::string::npos) << out;
}


int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    gst_mpegts_initialize();

    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}


// Repro for "CUE-OUT works for time_signal under elemental but not enhanced".
TEST(hlsScteMarkers, timeSignalCueOutAppearsInBothFlavours) {
    auto elemental = serializeTimeSignalWith({hls::ScteMarker::ELEMENTAL});
    auto enhanced  = serializeTimeSignalWith({hls::ScteMarker::ENHANCED});

    // First: does the event even register as an avail start?
    auto ev = makeTimeSignalBreakStartEvent();
    EXPECT_TRUE(ev.isOut())  << "time_signal 0x34 must set out";
    EXPECT_FALSE(ev.isIn())  << "time_signal 0x34 must not set in";
    EXPECT_NE(ev.getDuration(), GST_CLOCK_TIME_NONE) << "segmentation_duration must be picked up";

    EXPECT_NE(elemental.find("#EXT-X-CUE-OUT"), std::string::npos)
        << "ELEMENTAL output:\n" << elemental;
    EXPECT_NE(enhanced.find("#EXT-X-CUE-OUT"), std::string::npos)
        << "ENHANCED output:\n" << enhanced;

    // The enhanced additions must be present too.
    EXPECT_NE(enhanced.find("#EXT-OATCLS-SCTE35:"), std::string::npos)
        << "ENHANCED output:\n" << enhanced;
}


// A time_signal whose running-time map lacks the key getSpliceTime() needs resolves to
// GST_CLOCK_TIME_NONE. Before the fix this was dropped silently by every placement check in
// serialize(); Streamer::putScteData() now rejects it up front, and serialize() skips it
// rather than overflowing the eventEnd sum.
TEST(hlsScteMarkers, unplaceableTimeSignalIsSkippedNotSilentlyMisplaced) {
    auto broken = makeTimeSignalEventWithoutTimingMap();

    // The section itself parses fine - it is only the placement that is unresolvable.
    EXPECT_TRUE(broken.isOut());
    EXPECT_NE(broken.getDuration(), GST_CLOCK_TIME_NONE);
    EXPECT_EQ(broken.getStart(), GST_CLOCK_TIME_NONE);

    auto out = serializeEvent(broken, {hls::ScteMarker::ENHANCED});

    // No cue tags, but also no malformed output: the rest of the playlist is intact.
    EXPECT_EQ(out.find("EXT-X-CUE-OUT"),    std::string::npos) << out;
    EXPECT_EQ(out.find("EXT-OATCLS-SCTE35"), std::string::npos) << out;
    EXPECT_NE(out.find("seg0.ts"),          std::string::npos) << out;
    EXPECT_NE(out.find("seg1.ts"),          std::string::npos) << out;
}


// EXTINF must be the last tag before the URI it describes, and the cue tags must precede it.
TEST(hlsScteMarkers, cueTagsPrecedeExtinfWhichPrecedesTheUri) {
    for (auto marker : {hls::ScteMarker::ELEMENTAL, hls::ScteMarker::ENHANCED}) {
        auto out = serializeTimeSignalWith({marker});

        auto cueOut = out.find("#EXT-X-CUE-OUT");
        auto extinf = out.find("#EXTINF");
        auto uri    = out.find("seg0.ts");

        ASSERT_NE(cueOut, std::string::npos) << out;
        ASSERT_NE(extinf, std::string::npos) << out;
        ASSERT_NE(uri,    std::string::npos) << out;

        EXPECT_LT(cueOut, extinf) << "cue tag must come before EXTINF\n" << out;
        EXPECT_LT(extinf, uri)    << "EXTINF must come last before the URI\n" << out;

        if (marker == hls::ScteMarker::ENHANCED) {
            auto oatcls = out.find("#EXT-OATCLS-SCTE35");
            ASSERT_NE(oatcls, std::string::npos) << out;
            EXPECT_LT(oatcls, extinf) << "OATCLS tag must come before EXTINF\n" << out;
        }
    }
}


// RFC 8216 4.3.2.1: "#EXTINF:<duration>,<title>" - the comma is required, title may be empty.
TEST(hlsScteMarkers, extinfCarriesItsMandatoryComma) {
    auto out = serializeTimeSignalWith({hls::ScteMarker::ELEMENTAL});

    EXPECT_NE(out.find("#EXTINF:6.000,"), std::string::npos)
        << "EXTINF must end with a comma\n" << out;
}


// auto_return decides whether a splice_insert break closes itself or waits for a manual
// return-to-network. The flag used to be hardcoded true in the scheduler; these pin that it
// is now carried on the event, and that omitting it keeps the old behaviour.
TEST(spliceInsertAutoReturn, defaultsToTrueSoExistingEventsAreUnchanged) {
    transcoder::EventSpliceInsert event(1, 0, 1001, 30000);

    EXPECT_TRUE(event.isAutoReturn());
}

TEST(spliceInsertAutoReturn, carriesAnExplicitFalse) {
    transcoder::EventSpliceInsert manual(1, 0, 1001, 30000, false);
    transcoder::EventSpliceInsert automatic(1, 0, 1001, 30000, true);

    EXPECT_FALSE(manual.isAutoReturn());
    EXPECT_TRUE(automatic.isAutoReturn());
}

TEST(spliceInsertAutoReturn, twoEventsDifferingOnlyByTheFlagAreNotEqual) {
    // Equality drives scheduler de-duplication, so the flag has to take part in it.
    transcoder::EventSpliceInsert manual(1, 0, 1001, 30000, false);
    transcoder::EventSpliceInsert automatic(1, 0, 1001, 30000, true);

    EXPECT_FALSE(manual == automatic);
    EXPECT_TRUE(manual == transcoder::EventSpliceInsert(1, 0, 1001, 30000, false));
}


// An auto_return = 0 break used to publish one bare CUE-OUT and nothing else: no
// continuation, so the only marker scrolled out of the live window while the break was
// still open, and a player joining mid-break had no way to know it was inside an avail.
TEST(hlsScteMarkers, anOpenEndedBreakKeepsSignallingWithoutPromisingAnEnd) {
    auto out = serializeOpenEndedWith({hls::ScteMarker::ENHANCED});

    auto expected = makeOpenEndedBreakStartEvent().getBase64();
    ASSERT_FALSE(expected.empty());

    // The cue-out stays bare: the duration this section carries is advisory.
    EXPECT_NE(out.find("#EXT-X-CUE-OUT\n"), std::string::npos) << out;
    EXPECT_EQ(out.find("#EXT-X-CUE-OUT:"),   std::string::npos) << out;

    // The continuation is what a late joiner reads, and it carries the section.
    EXPECT_NE(out.find("#EXT-X-CUE-OUT-CONT:"), std::string::npos) << out;
    EXPECT_NE(out.find("ElapsedTime=6.000"),    std::string::npos) << out;
    EXPECT_NE(out.find("SCTE35=" + expected),   std::string::npos) << out;

    // Stating a Duration would promise an end this break has not signalled.
    EXPECT_EQ(out.find("Duration="), std::string::npos) << out;
}


TEST(hlsScteMarkers, anOpenEndedBreakIsNotClosedByItsAdvisoryDuration) {
    auto out = serializeOpenEndedWith({hls::ScteMarker::ENHANCED});

    // seg2 begins 60s in, past the 45s the section carries. Only a return to network
    // ends this break, so there is no CUE-IN and the continuation is still running.
    EXPECT_EQ(out.find("#EXT-X-CUE-IN"),      std::string::npos) << out;
    EXPECT_NE(out.find("ElapsedTime=60.000"), std::string::npos) << out;
}


TEST(hlsScteMarkers, theElementalFlavourOmitsAnOpenEndedContinuationRatherThanInventOne) {
    auto out = serializeOpenEndedWith({hls::ScteMarker::ELEMENTAL});

    // "<elapsed>/<duration>" has nowhere to put an unknown duration, so this flavour
    // signals the break start and stays silent rather than fabricating an end time.
    EXPECT_NE(out.find("#EXT-X-CUE-OUT\n"),    std::string::npos) << out;
    EXPECT_EQ(out.find("#EXT-X-CUE-OUT-CONT"), std::string::npos) << out;
    EXPECT_EQ(out.find("#EXT-X-CUE-IN"),       std::string::npos) << out;
}


// The auto-return path must keep behaving exactly as it did.
TEST(hlsScteMarkers, anAutoReturnBreakStillPublishesItsDuration) {
    auto out = serializeWith({hls::ScteMarker::ENHANCED});

    EXPECT_NE(out.find("#EXT-X-CUE-OUT:30.000"), std::string::npos) << out;
    EXPECT_NE(out.find("Duration=30.000"),       std::string::npos) << out;
}


// The return to network used to be recognised only within a quarter-segment of a segment
// boundary. An auto-returning break always satisfies that - its end is derived from a start
// that was itself aligned - but an operator's cue-in lands wherever they press it, and the
// avail then stopped being signalled with no CUE-IN ever closing it.
TEST(hlsScteMarkers, aReturnToNetworkInsideASegmentStillClosesTheAvail) {
    auto out = serializeClosedMidSegment({hls::ScteMarker::ENHANCED});

    EXPECT_NE(out.find("#EXT-X-CUE-IN"), std::string::npos) << out;

    // seg1 is still inside the break and continues; seg2 is the first segment beginning
    // after the return, so it carries the close and no continuation. No segment may be
    // marked as both continuing and ending.
    EXPECT_NE(out.find("ElapsedTime=6.000"),  std::string::npos) << out;
    EXPECT_EQ(out.find("ElapsedTime=12.000"), std::string::npos) << out;

    auto cueIn = out.find("#EXT-X-CUE-IN");
    auto lastCont = out.rfind("#EXT-X-CUE-OUT-CONT");
    EXPECT_LT(lastCont, cueIn) << out;
}
