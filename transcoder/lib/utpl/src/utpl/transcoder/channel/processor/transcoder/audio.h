#ifndef __PROCESSOR_TRANSCODER_AUDIO_H__
#define __PROCESSOR_TRANSCODER_AUDIO_H__

#include "utpl/transcoder/channel/processor.h"

namespace utpl {
    namespace transcoder {
        class AudioTranscodingProcessor : public Processor {
            public:
                AudioTranscodingProcessor(const std::string &processorName, const transcoder::StreamParameters &outParams);

            public:
                std::shared_ptr<Flowline> createFlowLine(Container container, const transcoder::StreamParameters &outParams) override;

                bool isAudio() const override;
                bool isVideo() const override;
                bool isCaption() const override;

                const StreamParameters &getInputParameters() override;

                void prepare(pipeline::BinPtr &bin) override;
                void link() override;
                const std::vector<std::shared_ptr<Flowline>> &getFlowlines() const override;
                std::shared_ptr<Compositor> getCompositor() const override;
                pipeline::ElementPtr getMuter() override;

                std::shared_ptr<transcoder::StreamConnector> getConnector() override;
                void setConnector(std::shared_ptr<transcoder::StreamConnector> connector) override;

                const std::string &getName() override;

                void blank(bool blank) override;

                bool compatible(const StreamParameters &outputParameters) override;

            private:
                std::string          _name;
                pipeline::PadPtr     _decoderPad; // Target Pad for decoder
                pipeline::ElementPtr _muter;
                pipeline::BinPtr     _bin;
                StreamParameters     _inputParameters;
                StreamParameters     _outputParameters;

                bool _prepared;
                bool _linked;

                std::shared_ptr<transcoder::StreamConnector> _connector;
                std::vector<std::shared_ptr<Flowline>>       _flowLines;
        };
    }
}

#endif /* __PROCESSOR_TRANSCODER_AUDIO_H__ */
