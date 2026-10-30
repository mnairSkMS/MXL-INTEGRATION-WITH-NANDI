#ifndef __UTPL_PIPELINE_TYPES_H__
#define __UTPL_PIPELINE_TYPES_H__

#include <gst/gst.h>

namespace utpl {
    namespace pipeline {
        using TimedCallbackId = GstClockID;
    }
}

#endif