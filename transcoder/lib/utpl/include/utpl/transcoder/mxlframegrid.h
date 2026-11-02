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

                uint64_t getDropped() const { return this->_dropped; }
                uint64_t getMoved() const   { return this->_moved; }

            private:
                uint64_t _period  = 0;
                bool     _started = false;
                uint64_t _origin  = 0;
                int64_t  _last    = -1;   // number of the last frame written
                int      _movedInARow = 0;

                uint64_t _dropped = 0;
                uint64_t _moved   = 0;
        };
    }
}

#endif /* __UTPL_TRANSCODER_MXLFRAMEGRID_H__ */
