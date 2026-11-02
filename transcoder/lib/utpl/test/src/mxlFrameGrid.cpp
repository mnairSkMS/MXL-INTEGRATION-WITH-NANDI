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
