#ifndef _UTPL_TRANSCODER_MUXER_H_
#define _UTPL_TRANSCODER_MUXER_H_

#include "utpl/common/utils/class.h"

#include "utpl/pipeline/pad.h"
#include "utpl/pipeline/element.h"
#include "utpl/pipeline/bin.h"

#include "utpl/transcoder/output/parameters.h"
#include "utpl/transcoder/stream.h"

namespace utpl {
    namespace transcoder {
        namespace output {
            class Muxer {
                UTPL_CLASS_INTERFACE(Muxer) {}

                public:
                    virtual void link() = 0;
                    virtual const std::string &getName() = 0;

                    virtual void putScteData(const Scte35Data &data) = 0;

                    virtual pipeline::ElementPtr getNode() = 0;
                    virtual pipeline::PadPtr getSrcPad() = 0;
                    virtual pipeline::PadPtr getSinkPad(MediaType mediaType, Pid pid = PID_NULL) = 0;
            };
        }
    }
}

#endif // _UTPL_TRANSCODER_MUXER_H_
