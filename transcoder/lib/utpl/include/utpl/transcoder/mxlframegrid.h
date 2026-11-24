#ifndef __UTPL_TRANSCODER_MXLFRAMEGRID_H__
#define __UTPL_TRANSCODER_MXLFRAMEGRID_H__

#include <cstdint>

namespace utpl {
    namespace transcoder {
        // Gives each video frame of an MXL tap its place on a grid of exact frame periods.
        //
        // An MXL video flow is a ring of slots, one per frame period, and its writer refuses a frame that
        // falls in the slot of the frame it wrote last, or before it. The times of frames inside the
        // channel are not that exact: they jitter, and a source that is cut off or replaced leaves them a
        // frame or two out of step. So every frame is stamped `origin + n * period` with a strictly
        // increasing n, which the writer then maps to consecutive slots.
        //
        //   n = the frame's own time, rounded to the nearest period, when that is past the last n.
        //   A frame that lands on the last n, or one before it, would collide. It takes the next n (the
        //   stream is then one slot late), but only for a few frames in a row: a stream that stays late
        //   gets one frame dropped, which puts it back in step without leaving an empty slot, which a
        //   reader would stumble over.
        //   A frame two or more periods early is a repeat: dropped.
        //   Times that step back by over a second cannot be followed: the flow has to start again.
        class MxlFrameGrid {
            public:
                enum class Result {
                    WRITE,    // write the frame, stamped with the time given back
                    DROP,     // leave the frame out
                    RESTART   // the times went back by over a second; start the flow again
                };

                // Frames in a row that may be moved by one slot before one is dropped to catch up.
                static const int MAX_MOVED_IN_A_ROW = 3;

                // The period is rounded up to a whole nanosecond, so that two frames are never closer
                // than a slot of the flow. A non-positive rate switches the grid off: times pass unchanged.
                void setRate(int num, int den);

                // Forget the grid: the next frame starts a new one.
                void reset();

                Result place(uint64_t runningTime, uint64_t *stamped);

                // True once a frame has set the grid's origin (the grid is not started again until reset() or setRate()).
                bool isStarted() const { return this->_started; }

                // How far a time has to be moved so that a frame stamped with it takes the grain of an MXL flow of this rate
                // exactly, not the nearest one. MXL puts a frame in the grain `index(time)`, the grain being a whole number of
                // periods from the epoch, and a reader takes the frame to have the time of its grain: a frame that is
                // 17 ms past a grain boundary is therefore shown 17 ms early (or 23 ms late, whichever way the writer
                // rounds), by an amount that depends on when the source happened to start and is different at every start.
                // The sound is written to the sample, so it would be that far out of step with the picture. Moving the pictures AND the sound by the
                // same amount keeps them in step and puts the pictures on the grid.
                //
                // `timestamp` is the frame's time in MXL's own clock (nanoseconds since the epoch). The result is the move, in
                // nanoseconds, from -period/2 (exclusive) to period/2 + margin, which puts the frame `margin` after the nearest grain boundary: so that
                // either way of rounding takes the boundary's own grain. Zero when the rate is not valid.
                static int64_t alignmentDelay(uint64_t timestamp, int num, int den, uint64_t margin);

                // The time of the n-th frame after the origin. By default it is n whole periods, the period rounded up to a nanosecond, so that two
                // frames are never closer than a slot of the flow; at a fractional rate that is a little long and the error grows with n (0.3 ns
                // a frame at 29.97, a millisecond in two days). With setExact(true) it is the exact time of the rate rounded down, with no growing
                // error, and a frame can then lie up to a nanosecond closer to the last than a slot: safe only when the frames are put a
                // margin past their grain boundary (alignmentDelay), which is when the tap asks for it. The setting goes with reset().
                uint64_t timeOfFrame(int64_t n) const;
                void     setExact(bool exact) { this->_exact = exact; }

                uint64_t getDropped() const { return this->_dropped; }
                uint64_t getMoved() const   { return this->_moved; }

            private:
                uint64_t _period  = 0;
                int      _num     = 0;
                int      _den     = 1;
                bool     _started = false;
                bool     _exact   = false;
                uint64_t _origin  = 0;
                int64_t  _last    = -1;   // number of the last frame written
                int      _movedInARow = 0;

                uint64_t _dropped = 0;
                uint64_t _moved   = 0;
        };
    }
}

#endif /* __UTPL_TRANSCODER_MXLFRAMEGRID_H__ */
