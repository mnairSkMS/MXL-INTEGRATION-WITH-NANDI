#ifndef __UTPL_HLS_SEGMENT_H__
#define __UTPL_HLS_SEGMENT_H__

#include "utpl/common/utils/class.h"
#include "utpl/common/time.h"

#include "utpl/hls/segment/storage/factory.h"

namespace utpl {
    namespace hls {
        class Segment {
            UTPL_CLASS_INTERFACE(Segment) {}

            public:
                virtual void setBeginRunningTime(GstClockTime time) = 0;
                virtual GstClockTime getBeginRunningTime() const = 0;
                virtual void setEndRunningTime(GstClockTime time) = 0;
                virtual GstClockTime getEndRunningTime() = 0;

                virtual utpl::Timestamp getDuration() const = 0;
                virtual void setDuration(Timestamp duration) = 0;

                virtual const std::string &getName() const = 0;
                virtual int getSequence() const = 0;
                virtual uint64_t getBitrate() const = 0;

                virtual const std::string &getProgramDateTime() = 0;
                virtual std::string setProgramDateTime() = 0;

                virtual bool isEmpty() const = 0;
                virtual bool isBuffered() const = 0;
                virtual bool isBuffering() const = 0;
                virtual void setEmpty() = 0;
                virtual void setBuffering() = 0;
                virtual void setBuffered() = 0;

                virtual std::shared_ptr<SegmentStorage> getStorage() = 0;
        };
    }
}

#endif