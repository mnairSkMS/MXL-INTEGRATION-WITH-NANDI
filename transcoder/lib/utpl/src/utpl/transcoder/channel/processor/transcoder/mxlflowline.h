#ifndef __PROCESSOR_TRANSCODER_MXLFLOWLINE_H__
#define __PROCESSOR_TRANSCODER_MXLFLOWLINE_H__

#include <vector>

#include "utpl/transcoder/channel/flowline.h"

namespace utpl {
    namespace transcoder {
        // A flowline that does not encode: the processor's raw picture or audio goes straight to the
        // output stream, which for an MXL output (MxlOutput) ends in a tap that writes the flow. Each
        // MXL stream has a flowline of its own, since it ends in one flow.
        class MxlFlowline : public Flowline {
            public:
                MxlFlowline(const std::string &name, const transcoder::StreamParameters &outParameters);

            public:
                void addOutput(transcoder::OutputStream *stream) override;
                bool compatible(const transcoder::StreamParameters &outputParameters) override;
                void prepare(const transcoder::StreamParameters &inParameters, pipeline::BinPtr &bin) override;

                const transcoder::StreamParameters &getParameters() const override;
                pipeline::PadPtr getInputPad() override;

                void link() override;

            private:
                std::string                       _name;
                transcoder::StreamParameters      _outputParameters;
                pipeline::ElementPtr              _identity;
                pipeline::PadPtr                  _src;
                std::vector<transcoder::OutputStream *> _outputs;
        };
    }
}

#endif /* __PROCESSOR_TRANSCODER_MXLFLOWLINE_H__ */
