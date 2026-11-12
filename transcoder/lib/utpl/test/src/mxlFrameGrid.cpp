#include <gtest/gtest.h>

#include "utpl/transcoder/mxlframegrid.h"

using namespace utpl::transcoder;

namespace {
    const uint64_t MS = 1000000ULL;

    // Frames of 25 fps at the given offsets in ms from an arbitrary running time.
    uint64_t at(int64_t ms) {
        return 5000 * MS + (uint64_t) (ms * (int64_t) MS);
    }
}

TEST(MxlFrameGrid, RegularFramesGetConsecutiveSlots) {
    MxlFrameGrid grid;
    grid.setRate(25, 1);

    for (int i = 0; i < 100; i++) {
        uint64_t stamped = 0;

        ASSERT_EQ(grid.place(at(i * 40), &stamped), MxlFrameGrid::Result::WRITE);
        EXPECT_EQ(stamped, at(i * 40));
    }

    EXPECT_EQ(grid.getDropped(), 0u);
    EXPECT_EQ(grid.getMoved(), 0u);
}

TEST(MxlFrameGrid, JitterWithinHalfAPeriodIsAbsorbed) {
    MxlFrameGrid grid;
    grid.setRate(25, 1);

    const int jitter[] = { 0, 6, -8, 12, -15, 3, 18, -18, 0, 9 };
    uint64_t  previous = 0;

    for (int i = 0; i < 10; i++) {
        uint64_t stamped = 0;

        ASSERT_EQ(grid.place(at(i * 40 + jitter[i]), &stamped), MxlFrameGrid::Result::WRITE) << "frame " << i;
        EXPECT_EQ(stamped, at(i * 40)) << "frame " << i << " is put on its own slot";
        if (i) {
            EXPECT_EQ(stamped - previous, 40 * MS);
        }
        previous = stamped;
    }
}

TEST(MxlFrameGrid, EverySlotIsStrictlyLaterThanTheLastWhateverTheTimes) {
    MxlFrameGrid grid;
    grid.setRate(25, 1);

    // The spacing seen when a source was cut off: 34, 40, 32, 44, 36, 44 ms, then a pair at 5 ms.
    const int steps[] = { 34, 40, 32, 44, 36, 44, 5, 5, 40, 20, 20, 40 };
    int64_t   t = 0;
    uint64_t  last = 0;
    bool      any = false;

    for (int step : steps) {
        t += step;

        uint64_t stamped = 0;

        if (grid.place(at(t), &stamped) == MxlFrameGrid::Result::WRITE) {
            if (any) {
                EXPECT_GT(stamped, last) << "at " << t << " ms";
            }
            last = stamped;
            any  = true;
        }
    }
}

TEST(MxlFrameGrid, ASlotIsNeverUsedTwiceNorAnEarlierOne) {
    // Pseudo-random times, a frame every 40 ms with up to 35 ms of jitter either way.
    MxlFrameGrid grid;
    grid.setRate(25, 1);

    uint64_t seed = 12345;
    auto     next = [&]() { seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; return (int) ((seed >> 33) % 71) - 35; };

    uint64_t last = 0;
    bool     any  = false;

    for (int i = 0; i < 5000; i++) {
        uint64_t stamped = 0;

        if (grid.place(at(i * 40 + next()), &stamped) == MxlFrameGrid::Result::WRITE) {
            if (any) {
                ASSERT_GT(stamped, last) << "frame " << i;
                ASSERT_EQ((stamped - last) % (40 * MS), 0u) << "stamps stay on the grid";
            }
            last = stamped;
            any  = true;
        }
    }
}

TEST(MxlFrameGrid, ACollidingFrameTakesTheNextSlotThenTheStreamIsBroughtBackIntoStep) {
    MxlFrameGrid grid;
    grid.setRate(25, 1);

    uint64_t stamped = 0;

    ASSERT_EQ(grid.place(at(0), &stamped), MxlFrameGrid::Result::WRITE);    // number 0
    ASSERT_EQ(grid.place(at(40), &stamped), MxlFrameGrid::Result::WRITE);   // number 1

    // The times go back by one frame and stay there: the next frames are numbers 1, 2, 3, 4, ... but
    // the grid has already used 1, so each lands on the last slot. Three are moved on by a slot ...
    for (int i = 0; i < 3; i++) {
        const uint64_t before = stamped;

        ASSERT_EQ(grid.place(at(40 + i * 40), &stamped), MxlFrameGrid::Result::WRITE) << i;
        EXPECT_EQ(stamped - before, 40 * MS) << "moved to the very next slot, " << i;
    }
    EXPECT_EQ(grid.getMoved(), 3u);

    // ... the fourth is dropped, which puts the stream back in step without leaving a slot empty ...
    EXPECT_EQ(grid.place(at(40 + 3 * 40), &stamped), MxlFrameGrid::Result::DROP);
    EXPECT_EQ(grid.getDropped(), 1u);

    // ... and from then on every frame sits on its own slot, each one right after the last.
    uint64_t previous = stamped;

    for (int i = 4; i < 30; i++) {
        ASSERT_EQ(grid.place(at(40 + i * 40), &stamped), MxlFrameGrid::Result::WRITE) << i;
        EXPECT_EQ(stamped, at(40 + i * 40));
        EXPECT_EQ(stamped - previous, 40 * MS) << i;
        previous = stamped;
    }

    EXPECT_EQ(grid.getMoved(), 3u);
    EXPECT_EQ(grid.getDropped(), 1u);
}

TEST(MxlFrameGrid, AFrameTwoOrMoreSlotsEarlyIsARepeat) {
    MxlFrameGrid grid;
    grid.setRate(25, 1);

    uint64_t stamped = 0;

    for (int i = 0; i < 10; i++) {
        ASSERT_EQ(grid.place(at(i * 40), &stamped), MxlFrameGrid::Result::WRITE);
    }

    // The last frame was number 9. A frame at number 7 is two slots early.
    EXPECT_EQ(grid.place(at(7 * 40), &stamped), MxlFrameGrid::Result::DROP);
    EXPECT_EQ(grid.getDropped(), 1u);

    // The next frame, on its own slot, is written as normal.
    EXPECT_EQ(grid.place(at(10 * 40), &stamped), MxlFrameGrid::Result::WRITE);
    EXPECT_EQ(stamped, at(10 * 40));
}

TEST(MxlFrameGrid, AGapIsKeptAsAGap) {
    MxlFrameGrid grid;
    grid.setRate(25, 1);

    uint64_t stamped = 0;

    ASSERT_EQ(grid.place(at(0), &stamped), MxlFrameGrid::Result::WRITE);
    ASSERT_EQ(grid.place(at(40), &stamped), MxlFrameGrid::Result::WRITE);

    // The source stalls for 1.4 s: the next frame is written where it belongs, 36 slots on.
    ASSERT_EQ(grid.place(at(40 + 1440), &stamped), MxlFrameGrid::Result::WRITE);
    EXPECT_EQ(stamped, at(40 + 1440));
}

TEST(MxlFrameGrid, TimesThatStepBackByOverASecondAskForARestart) {
    MxlFrameGrid grid;
    grid.setRate(25, 1);

    uint64_t stamped = 0;

    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(grid.place(at(i * 40), &stamped), MxlFrameGrid::Result::WRITE);
    }

    EXPECT_EQ(grid.place(at(100 * 40 - 1200), &stamped), MxlFrameGrid::Result::RESTART);

    // A little under a second back is a repeat, not a restart.
    EXPECT_EQ(grid.place(at(100 * 40 - 800), &stamped), MxlFrameGrid::Result::DROP);

    // After a restart the grid begins again at whatever time comes next.
    grid.reset();
    ASSERT_EQ(grid.place(at(100 * 40 - 1200), &stamped), MxlFrameGrid::Result::WRITE);
    EXPECT_EQ(stamped, at(100 * 40 - 1200));
}

TEST(MxlFrameGrid, NonIntegerFrameRatesNeverShareASlot) {
    MxlFrameGrid grid;
    grid.setRate(30000, 1001);   // 33.3666... ms

    const uint64_t period = (1000000000ULL * 1001 + 29999) / 30000;
    uint64_t       last   = 0;

    for (int i = 0; i < 100000; i++) {
        uint64_t stamped = 0;
        // Exact times of the rate, in ns, rounded down.
        const uint64_t exact = 5000 * MS + (uint64_t) i * 1000000000ULL * 1001 / 30000;

        ASSERT_EQ(grid.place(exact, &stamped), MxlFrameGrid::Result::WRITE) << i;
        if (i) {
            ASSERT_EQ(stamped - last, period) << i;
        }
        last = stamped;
    }

    EXPECT_EQ(grid.getDropped(), 0u);
    EXPECT_EQ(grid.getMoved(), 0u);
}

TEST(MxlFrameGrid, WithoutARateTimesPassUnchanged) {
    MxlFrameGrid grid;
    grid.setRate(0, 1);

    uint64_t stamped = 0;

    EXPECT_EQ(grid.place(12345, &stamped), MxlFrameGrid::Result::WRITE);
    EXPECT_EQ(stamped, 12345u);
    EXPECT_EQ(grid.place(100, &stamped), MxlFrameGrid::Result::WRITE);
    EXPECT_EQ(stamped, 100u);
}

// Putting the pictures on MXL's grain grid (see MxlFrameGrid::alignmentDelay).

namespace {
    // The time of grain k at 25 fps is k * 40 ms from the epoch.
    int64_t delay25(uint64_t ts) {
        return MxlFrameGrid::alignmentDelay(ts, 25, 1, 1 * MS);
    }
}

TEST(MxlFrameGridAlignment, AFrameJustPastABoundaryIsMovedBackToIt) {
    // 17 ms past a boundary: move 17 ms earlier, then the margin of 1 ms
    EXPECT_EQ(delay25(1000 * 40 * MS + 17 * MS), -16 * (int64_t) MS);
}

TEST(MxlFrameGridAlignment, AFrameJustBeforeABoundaryIsMovedForwardToIt) {
    // 3 ms before the boundary: forward 3 ms, then the margin
    EXPECT_EQ(delay25(1000 * 40 * MS - 3 * MS), 4 * (int64_t) MS);
}

TEST(MxlFrameGridAlignment, TheMoveIsNeverMoreThanHalfAPeriodAndAMargin) {
    for (uint64_t ms = 0; ms < 200; ms++) {
        const int64_t move = delay25(7 * 1000 * 1000 * 40 * MS + ms * MS);

        EXPECT_GT(move, -20 * (int64_t) MS) << ms;
        EXPECT_LE(move, 21 * (int64_t) MS) << ms;
    }
}

TEST(MxlFrameGridAlignment, AfterTheMoveTheFrameIsJustPastABoundary) {
    // Any rate, any time: the moved time is a boundary plus the margin, to the nanosecond.
    const int rates[][2] = { { 25, 1 }, { 30000, 1001 }, { 50, 1 }, { 60000, 1001 }, { 24000, 1001 }, { 30, 1 } };

    for (const auto &rate : rates) {
        for (uint64_t k = 0; k < 40; k++) {
            const uint64_t ts    = 1794466514976806942ULL + k * 7919ULL * 1000ULL;
            const int64_t  move  = MxlFrameGrid::alignmentDelay(ts, rate[0], rate[1], 1 * MS);
            const uint64_t moved = (uint64_t) ((int64_t) ts + move);

            // the distance past the nearest boundary below, in nanoseconds: the boundary is at k * den * 1e9 / num
            const __int128 unit     = (__int128) rate[1] * 1000000000;
            const __int128 index    = ((__int128) (moved - 1 * MS) * rate[0] + unit / 2) / unit;
            const __int128 boundary = (index * unit + rate[0] / 2) / rate[0];

            EXPECT_EQ((__int128) moved - (__int128) 1 * MS, boundary) << rate[0] << "/" << rate[1] << " k " << k;
        }
    }
}

TEST(MxlFrameGridAlignment, AnInvalidRateMovesNothing) {
    EXPECT_EQ(MxlFrameGrid::alignmentDelay(123456789, 0, 1, 1 * MS), 0);
    EXPECT_EQ(MxlFrameGrid::alignmentDelay(123456789, 25, 0, 1 * MS), 0);
}

TEST(MxlFrameGridAlignment, TheFrameTimesAtFractionalRatesHaveNoGrowingError) {
    MxlFrameGrid grid;

    grid.setRate(30000, 1001);
    grid.setExact(true);

    // frame n is at n * 1001 / 30 ms exactly: after a million frames (9 hours) it is still within a nanosecond of that
    const int64_t n = 1000000;

    EXPECT_EQ(grid.timeOfFrame(n), (uint64_t) ((__int128) n * 1001 * 1000000000 / 30000));
    EXPECT_EQ(grid.timeOfFrame(0), 0u);
    EXPECT_LT(grid.timeOfFrame(n + 1) - grid.timeOfFrame(n), 33366668ULL);
    EXPECT_GT(grid.timeOfFrame(n + 1) - grid.timeOfFrame(n), 33366665ULL);
}

TEST(MxlFrameGridAlignment, TheGridKnowsWhenItHasStarted) {
    MxlFrameGrid grid;

    grid.setRate(25, 1);
    EXPECT_FALSE(grid.isStarted());

    uint64_t stamped = 0;

    ASSERT_EQ(grid.place(at(0), &stamped), MxlFrameGrid::Result::WRITE);
    EXPECT_TRUE(grid.isStarted());

    grid.reset();
    EXPECT_FALSE(grid.isStarted());
}
