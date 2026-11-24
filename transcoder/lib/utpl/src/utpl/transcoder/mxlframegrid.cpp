#include "utpl/transcoder/mxlframegrid.h"

using namespace utpl;

namespace {
    const uint64_t NANOSECONDS = 1000000000ULL;
}

void transcoder::MxlFrameGrid::setRate(int num, int den) {
    this->_period = (num > 0 && den > 0) ? (NANOSECONDS * (uint64_t) den + (uint64_t) num - 1) / (uint64_t) num : 0;
    this->_num    = num;
    this->_den    = den;

    this->reset();
}

void transcoder::MxlFrameGrid::reset() {
    this->_started     = false;
    this->_exact       = false;
    this->_last        = -1;
    this->_movedInARow = 0;
}

transcoder::MxlFrameGrid::Result transcoder::MxlFrameGrid::place(uint64_t runningTime, uint64_t *stamped) {
    if (! this->_period) {
        *stamped = runningTime;

        return Result::WRITE;
    }

    if (! this->_started) {
        this->_started = true;
        this->_origin  = runningTime;
        this->_last    = -1;
    }

    // The frame number nearest to this time, counting from the first frame.
    const int64_t period = (int64_t) this->_period;
    const int64_t offset = (int64_t) runningTime - (int64_t) this->_origin;
    int64_t       number = (offset >= 0) ? (offset + period / 2) / period : -((-offset + period / 2) / period);

    if (number > this->_last) {
        this->_movedInARow = 0;

    } else {
        const int64_t behind = this->_last - number;

        if (behind > (int64_t) (NANOSECONDS / this->_period)) {
            return Result::RESTART;
        }

        if (behind >= 2) {
            this->_dropped++;

            return Result::DROP;
        }

        // On the slot of the last frame, or the one before it. Taking the next slot costs a slot of lateness;
        // after a few frames of that the stream is dropped back into step with one frame.
        if (this->_movedInARow >= MAX_MOVED_IN_A_ROW) {
            this->_movedInARow = 0;
            this->_dropped++;

            return Result::DROP;
        }

        number = this->_last + 1;
        this->_movedInARow++;
        this->_moved++;
    }

    this->_last = number;
    *stamped    = this->_origin + this->timeOfFrame(number);

    return Result::WRITE;
}

uint64_t transcoder::MxlFrameGrid::timeOfFrame(int64_t n) const {
    if (! this->_exact) {
        return n < 0 ? 0 : (uint64_t) n * this->_period;
    }

    if (this->_num <= 0 || this->_den <= 0) {
        return 0;
    }

    // n * den * 10^9 / num, in 128 bits: a period rounded up to a nanosecond would be a little long and the error would
    // grow with every frame (0.3 ns a frame at 29.97, which is a millisecond in two days).
    const __int128 scaled = (__int128) n * (__int128) this->_den * (__int128) NANOSECONDS / (__int128) this->_num;

    return scaled < 0 ? 0 : (uint64_t) scaled;
}

int64_t transcoder::MxlFrameGrid::alignmentDelay(uint64_t timestamp, int num, int den, uint64_t margin) {
    if (num <= 0 || den <= 0) {
        return 0;
    }

    // The grain boundaries are at k * den * 10^9 / num. Find the one nearest to the time.
    const __int128 unit = (__int128) den * (__int128) NANOSECONDS;                // nanoseconds times num, per frame
    const __int128 ts   = (__int128) timestamp * (__int128) num;                  // the time in the same unit
    const __int128 k    = (ts + unit / 2) / unit;                                  // nearest boundary number
    const __int128 boundary = (k * unit + (__int128) num / 2) / (__int128) num;    // its time in nanoseconds

    return (int64_t) (boundary - (__int128) timestamp + (__int128) margin);
}
