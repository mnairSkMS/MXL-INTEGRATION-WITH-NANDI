#include "utpl/transcoder/mxlframegrid.h"

using namespace utpl;

namespace {
    const uint64_t NANOSECONDS = 1000000000ULL;
}

void transcoder::MxlFrameGrid::setRate(int num, int den) {
    this->_period = (num > 0 && den > 0) ? (NANOSECONDS * (uint64_t) den + (uint64_t) num - 1) / (uint64_t) num : 0;

    this->reset();
}

void transcoder::MxlFrameGrid::reset() {
    this->_started     = false;
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
    *stamped    = this->_origin + (uint64_t) number * this->_period;

    return Result::WRITE;
}
