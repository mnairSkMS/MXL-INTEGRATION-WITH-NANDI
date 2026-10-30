#ifndef __PROCESSOR_TRANSCODER_FLOWLINE_H__
#define __PROCESSOR_TRANSCODER_FLOWLINE_H__

#include "utpl/transcoder/channel/flowline.h"

namespace utpl {
    namespace transcoder {
        class TranscodingFlowline : public Flowline {
            public:
                TranscodingFlowline(const std::string &name, Container container, const transcoder::StreamParameters &outParameters);

            public:
                void addOutput(transcoder::OutputStream *stream) override;
                bool compatible(const transcoder::StreamParameters &outputParameters) override;
                void prepare(const transcoder::StreamParameters &inParameters, pipeline::BinPtr &bin) override;

                const transcoder::StreamParameters &getParameters() const override;
                pipeline::PadPtr getInputPad() override;

                void link() override;
    
            private:
                std::string                  _name;
                pipeline::ElementPtr         _encoder;
                pipeline::PadPtr             _encoderInputPad;
                transcoder::StreamParameters _outputParameters;
                Container                    _outputContainer;

                std::unordered_map<transcoder::OutputStream *, pipeline::PadPtr> _outputs;
                std::vector<pipeline::ElementPtr> _filters;
        };
    }
}

#endif /* __PROCESSOR_TRANSCODER_FLOWLINE_H__ */