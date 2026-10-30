#ifndef __UTPL_TRANSCODER_OUTPUT_H__
#define __UTPL_TRANSCODER_OUTPUT_H__

#include "utpl/common/utils/class.h"

#include "utpl/pipeline/element.h"

#include "utpl/transcoder/output/parameters.h"

namespace utpl {
    namespace transcoder {
        class Output {
            UTPL_CLASS_INTERFACE(Output) {}

            public:
                virtual void prepare(const OutputParameters &parameters) = 0;
                virtual void cleanup() = 0;

                virtual const std::vector<std::shared_ptr<OutputStream>> &getStreams() = 0;
                virtual bool isLive() const = 0;

                virtual pipeline::ElementPtr getNode() = 0;

                virtual void putScteData(const Scte35Data &data) = 0;

                virtual const OutputParameters &getOutputParameters() const = 0;
        };
    }
}

#endif /* __UTPL_TRANSCODER_OUTPUT_H__ */
