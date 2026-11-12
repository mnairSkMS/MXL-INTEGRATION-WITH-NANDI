#include <gtest/gtest.h>

#include "utpl/transcoder/mxlcarrier.h"

using namespace utpl::transcoder::mxl_carrier;

namespace {
    const uint64_t MS = 1000000ULL;

    // A section-looking blob: table_id 0xFC, a length, a body that depends on the seed, and four closing bytes.
    std::vector<uint8_t> section(size_t size, uint32_t seed) {
        std::vector<uint8_t> s(size);
        uint32_t x = seed * 2654435761u + 12345u;

        for (size_t i = 0; i < size; i++) {
            x = x * 1664525u + 1013904223u;
            s[i] = (uint8_t) (x >> 24);
        }

        s[0] = 0xFC;

        return s;
    }

    uint64_t frameTime(int n, int num, int den) {
        const uint64_t period = (1000000000ULL * den + num - 1) / num;

        return 5000 * MS + (uint64_t) n * period;
    }
}

TEST(MxlCarrierCodec, EverySizeRoundTripsByteForByte) {
    // Sizes around every fragment boundary, and the largest that fits.
    std::vector<size_t> sizes = { 1, 2, 17, 28, 60, 120, FRAGMENT_BYTES - 1, FRAGMENT_BYTES, FRAGMENT_BYTES + 1, 255, 400, 1000, 2000, 2 * FRAGMENT_BYTES, 2800, maxSectionBytes() };

    for (size_t size : sizes) {
        const Cue in { section(size, (uint32_t) size), 17 };

        const auto packed = pack({ in });

        ASSERT_FALSE(packed.empty()) << size;

        std::vector<Cue> out;
        std::string      error;

        ASSERT_TRUE(unpack(packed.data(), packed.size(), out, &error)) << size << ": " << error;
        ASSERT_EQ(out.size(), 1u) << size;
        EXPECT_EQ(out[0].section, in.section) << size;
        EXPECT_EQ(out[0].countdown, 17) << size;
    }
}

TEST(MxlCarrierCodec, SeveralCuesInOneGrainKeepTheirOrderAndCountdowns) {
    const std::vector<Cue> in = { { section(60, 1), 40 }, { section(500, 2), 3 }, { section(28, 3), 0 } };

    const auto packed = pack(in);

    std::vector<Cue> out;

    ASSERT_TRUE(unpack(packed.data(), packed.size(), out));
    ASSERT_EQ(out.size(), 3u);

    for (size_t i = 0; i < in.size(); i++) {
        EXPECT_EQ(out[i].section, in[i].section) << i;
        EXPECT_EQ(out[i].countdown, in[i].countdown) << i;
    }
}

TEST(MxlCarrierCodec, NoCuesIsAnEmptyBufferAndReadsBackAsNone) {
    EXPECT_TRUE(pack({}).empty());

    std::vector<Cue> out;

    EXPECT_TRUE(unpack(nullptr, 0, out));
    EXPECT_TRUE(out.empty());
}

TEST(MxlCarrierCodec, ANegativeCountdownSurvives) {
    const auto packed = pack({ { section(40, 9), -3 } });

    std::vector<Cue> out;

    ASSERT_TRUE(unpack(packed.data(), packed.size(), out));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].countdown, -3);
}

TEST(MxlCarrierCodec, AFlippedBitIsReportedAndEarlierCuesAreKept) {
    const auto first  = pack({ { section(60, 1), 5 } });
    const auto second = pack({ { section(60, 2), 5 } });

    std::vector<uint8_t> both = first;
    both.insert(both.end(), second.begin(), second.end());

    // Break the second packet in the middle of its user data.
    both[first.size() + 20] ^= 0x10;

    std::vector<Cue> out;
    std::string      error;

    EXPECT_FALSE(unpack(both.data(), both.size(), out, &error));
    EXPECT_FALSE(error.empty());
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].section, section(60, 1));
}

TEST(MxlCarrierCodec, ATruncatedBufferIsReported) {
    const auto packed = pack({ { section(2000, 4), 1 } });

    std::vector<Cue> out;
    std::string      error;

    EXPECT_FALSE(unpack(packed.data(), packed.size() / 2, out, &error));
    EXPECT_TRUE(out.empty());
}

TEST(MxlCarrierSize, TheLimitIsBetweenWhatWasProvedAndWhatFailed) {
    // MXL v1.1.0 (doc/mxl/step5-plan-2026-11-11.md 4.6): a 2800-byte section worked with a 19-byte header; one of 3000 stopped the sink.
    EXPECT_GE(maxSectionBytes(), 2800u);
    EXPECT_LT(maxSectionBytes(), 3200u);
    EXPECT_TRUE(fitsInGrain({ maxSectionBytes() }));
    EXPECT_FALSE(fitsInGrain({ maxSectionBytes() + 1 }));
}

TEST(MxlCarrierSize, CuesTogetherAreCheckedTogether) {
    // A section takes about 1.37 times its size in a grain (10-bit words, 11 bytes of header in every 244-byte fragment).
    EXPECT_TRUE(fitsInGrain({ 1000, 1000 }));
    EXPECT_FALSE(fitsInGrain({ 1000, 1000, 1000 }));
    EXPECT_FALSE(fitsInGrain({ 2000, 2000 }));
    EXPECT_TRUE(fitsInGrain({}));
}

TEST(MxlCarrierSize, TheGrainIsNeverOverTheSinkLimit) {
    for (size_t size = 1; size <= maxSectionBytes(); size += 37) {
        EXPECT_LE(6 + sizeInGrain(size), MAX_GRAIN_BYTES) << size;
    }
}

TEST(MxlCarrierQueue, ACueCountsDownToItsSpliceFrameAndThenRepeatsAFewFrames) {
    CueQueue queue;
    queue.setRate(25, 1);

    ASSERT_EQ(queue.add(section(100, 1).data(), 100, frameTime(10, 25, 1)), CueQueue::Add::ACCEPTED);

    for (int n = 0; n < 10; n++) {
        const auto cues = queue.forFrame(frameTime(n, 25, 1));

        ASSERT_EQ(cues.size(), 1u) << n;
        EXPECT_EQ(cues[0].countdown, 10 - n) << n;
        EXPECT_EQ(cues[0].section, section(100, 1));
    }

    // At the splice frame and for IMMEDIATE_FRAMES frames in all, countdown 0; then gone.
    for (int n = 10; n < 10 + CueQueue::IMMEDIATE_FRAMES; n++) {
        const auto cues = queue.forFrame(frameTime(n, 25, 1));

        ASSERT_EQ(cues.size(), 1u) << n;
        EXPECT_EQ(cues[0].countdown, 0) << n;
    }

    EXPECT_TRUE(queue.forFrame(frameTime(10 + CueQueue::IMMEDIATE_FRAMES, 25, 1)).empty());
    EXPECT_EQ(queue.getPending(), 0u);
}

TEST(MxlCarrierQueue, EveryFrameGetsAnAnswerAndAFrameWithoutCuesGetsNone) {
    CueQueue queue;
    queue.setRate(25, 1);

    for (int n = 0; n < 50; n++) {
        EXPECT_TRUE(queue.forFrame(frameTime(n, 25, 1)).empty());
    }
}

TEST(MxlCarrierQueue, AnImmediateCueAndACueInThePastAreWrittenForAFewFramesAtCountdownZero) {
    CueQueue queue;
    queue.setRate(25, 1);

    ASSERT_EQ(queue.add(section(60, 1).data(), 60, NONE), CueQueue::Add::ACCEPTED);
    ASSERT_EQ(queue.add(section(60, 2).data(), 60, frameTime(75, 25, 1)), CueQueue::Add::ACCEPTED);   // 1 s in the past at frame 100

    for (int n = 100; n < 100 + CueQueue::IMMEDIATE_FRAMES; n++) {
        const auto cues = queue.forFrame(frameTime(n, 25, 1));

        ASSERT_EQ(cues.size(), 2u) << n;
        EXPECT_EQ(cues[0].countdown, 0);
        EXPECT_EQ(cues[1].countdown, 0);
    }

    EXPECT_TRUE(queue.forFrame(frameTime(100 + CueQueue::IMMEDIATE_FRAMES, 25, 1)).empty());
}

TEST(MxlCarrierQueue, CountdownIsInFramesAtEveryRateAndSurvivesJitter) {
    const int rates[][2] = { { 25, 1 }, { 30000, 1001 }, { 50, 1 }, { 60000, 1001 } };

    for (const auto &rate : rates) {
        CueQueue queue;
        queue.setRate(rate[0], rate[1]);

        const int spliceFrame = 40;

        ASSERT_EQ(queue.add(section(80, 5).data(), 80, frameTime(spliceFrame, rate[0], rate[1])), CueQueue::Add::ACCEPTED);

        for (int n = 0; n < spliceFrame; n++) {
            // Frames arrive up to 4 ms early or late.
            const int64_t jitter = ((n * 7) % 9 - 4) * (int64_t) MS;
            const auto    cues   = queue.forFrame((uint64_t) ((int64_t) frameTime(n, rate[0], rate[1]) + jitter));

            ASSERT_EQ(cues.size(), 1u) << rate[0] << "/" << rate[1] << " frame " << n;
            EXPECT_EQ(cues[0].countdown, spliceFrame - n) << rate[0] << "/" << rate[1] << " frame " << n;
        }
    }
}

TEST(MxlCarrierQueue, ACueThatNoFrameCarriedForSecondsIsDroppedNotWrittenLate) {
    CueQueue queue;
    queue.setRate(25, 1);

    const auto cue = section(60, 1);

    // Received at 5 s; the channel sends no frames for 10 s.
    ASSERT_EQ(queue.add(cue.data(), cue.size(), NONE, 5000 * MS), CueQueue::Add::ACCEPTED);
    ASSERT_EQ(queue.add(cue.data(), cue.size(), 6000 * MS, 5000 * MS), CueQueue::Add::ACCEPTED);

    EXPECT_TRUE(queue.forFrame(15000 * MS).empty());
    EXPECT_EQ(queue.getStale(), 2u);
    EXPECT_EQ(queue.getPending(), 0u);

    // A cue received a moment before the first frame is still written.
    ASSERT_EQ(queue.add(cue.data(), cue.size(), NONE, 14900 * MS), CueQueue::Add::ACCEPTED);
    EXPECT_EQ(queue.forFrame(15000 * MS).size(), 1u);
}

TEST(MxlCarrierQueue, ATooLargeCueIsRefusedAndNothingIsQueued) {
    CueQueue queue;
    queue.setRate(25, 1);

    const auto big = section(maxSectionBytes() + 1, 1);

    EXPECT_EQ(queue.add(big.data(), big.size(), NONE), CueQueue::Add::TOO_LARGE);
    EXPECT_EQ(queue.getPending(), 0u);

    const auto fits = section(maxSectionBytes(), 1);

    EXPECT_EQ(queue.add(fits.data(), fits.size(), NONE), CueQueue::Add::ACCEPTED);
}

TEST(MxlCarrierQueue, AnEmptyCueIsRefusedAndCuesWaitForTheFrameRate) {
    CueQueue queue;

    const auto cue = section(60, 1);

    EXPECT_EQ(queue.add(nullptr, 0, NONE), CueQueue::Add::EMPTY);
    EXPECT_EQ(queue.add(cue.data(), 0, NONE), CueQueue::Add::EMPTY);

    // No rate yet: the cue is kept, and no frame carries it.
    EXPECT_EQ(queue.add(cue.data(), cue.size(), NONE), CueQueue::Add::ACCEPTED);
    EXPECT_TRUE(queue.forFrame(1000 * MS).empty());
    EXPECT_EQ(queue.getPending(), 1u);

    queue.setRate(25, 1);

    EXPECT_EQ(queue.forFrame(1040 * MS).size(), 1u);
}

TEST(MxlCarrierQueue, TheQueueIsBounded) {
    CueQueue queue;
    queue.setRate(25, 1);

    const auto cue = section(60, 1);

    for (size_t i = 0; i < CueQueue::MAX_PENDING; i++) {
        ASSERT_EQ(queue.add(cue.data(), cue.size(), NONE), CueQueue::Add::ACCEPTED);
    }

    EXPECT_EQ(queue.add(cue.data(), cue.size(), NONE), CueQueue::Add::QUEUE_FULL);
}

TEST(MxlCarrierQueue, CuesThatDoNotFitTogetherAreLeftOutOfThatGrainNotLost) {
    CueQueue queue;
    queue.setRate(25, 1);

    const auto a = section(2000, 1), b = section(2000, 2);

    ASSERT_EQ(queue.add(a.data(), a.size(), frameTime(20, 25, 1)), CueQueue::Add::ACCEPTED);
    ASSERT_EQ(queue.add(b.data(), b.size(), frameTime(20, 25, 1)), CueQueue::Add::ACCEPTED);

    const auto cues = queue.forFrame(frameTime(0, 25, 1));

    // Both cannot share a grain, so the second is skipped (and counted); it is not removed.
    ASSERT_EQ(cues.size(), 1u);
    EXPECT_EQ(cues[0].section, a);
    EXPECT_EQ(queue.getSkipped(), 1u);
    EXPECT_EQ(queue.getPending(), 2u);

    // What was written can always be read back: the grain is within the limit.
    const auto packed = pack(cues);

    EXPECT_LE(6 + packed.size() * 5 / 4, MAX_GRAIN_BYTES);
}

TEST(MxlCarrierDeduper, ARepeatInTheWindowIsNotNewAndALaterCopyIs) {
    Deduper dedupe(1000 * MS);

    const auto a = section(60, 1), b = section(60, 2);

    EXPECT_TRUE(dedupe.isNew(0, a));

    // The writer repeats a cue in every grain until its splice frame.
    for (uint64_t t = 40; t <= 600; t += 40) {
        EXPECT_FALSE(dedupe.isNew(t * MS, a)) << t;
    }

    EXPECT_TRUE(dedupe.isNew(600 * MS, b));
    EXPECT_FALSE(dedupe.isNew(640 * MS, b));
    EXPECT_FALSE(dedupe.isNew(640 * MS, a));

    // Missing for longer than the window: the same bytes are a new cue (an operator sent it twice).
    EXPECT_TRUE(dedupe.isNew(2000 * MS, a));
}

TEST(MxlCarrierDeduper, TheMemoryIsBounded) {
    Deduper dedupe(1000 * MS);

    for (uint32_t i = 0; i < 1000; i++) {
        dedupe.isNew((uint64_t) i * 40 * MS, section(40, i));
    }

    // 1 s of 40 ms frames holds about 26 distinct cues at most.
    EXPECT_LE(dedupe.size(), 30u);
}

TEST(MxlCarrierEndToEnd, WhatTheQueueGivesTheCodecTakesBackAndTheReaderDelivers) {
    CueQueue queue;
    queue.setRate(25, 1);
    Deduper dedupe;

    const auto cue = section(1200, 77);

    ASSERT_EQ(queue.add(cue.data(), cue.size(), frameTime(12, 25, 1)), CueQueue::Add::ACCEPTED);

    int delivered = 0;
    int spliceFrameSeen = -1;

    for (int n = 0; n < 30; n++) {
        const uint64_t t      = frameTime(n, 25, 1);
        const auto     packed = pack(queue.forFrame(t));

        std::vector<Cue> read;

        ASSERT_TRUE(unpack(packed.data(), packed.size(), read));

        for (const auto &c : read) {
            if (dedupe.isNew(t, c.section)) {
                delivered++;
                spliceFrameSeen = n + c.countdown;

                EXPECT_EQ(c.section, cue);
            }
        }
    }

    EXPECT_EQ(delivered, 1);
    EXPECT_EQ(spliceFrameSeen, 12);
}
