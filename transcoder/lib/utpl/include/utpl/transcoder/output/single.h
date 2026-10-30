#ifndef __UTPL_TRANSCODER_OUTPUT_SINGLE_H__
#define __UTPL_TRANSCODER_OUTPUT_SINGLE_H__

#include <memory>
#include <vector>
#include <string>
#include <map>

#include "utpl/pipeline/bin.h"

#include "utpl/transcoder/output/sink.h"
#include "utpl/transcoder/output/muxer.h"

#include "utpl/transcoder/output.h"

namespace utpl {
    namespace transcoder {
        class SingleOutput : public Output {
            public:
                SingleOutput();
                virtual ~SingleOutput();

                void prepare(const OutputParameters &parameters) override;
                void cleanup() override;

                const std::vector<std::shared_ptr<OutputStream>> &getStreams() override;
                bool isLive() const override;

                pipeline::ElementPtr getNode() override;

                void putScteData(const Scte35Data &data) override;

                const OutputParameters &getOutputParameters() const override;

            public:
                void prepare(const OutputParameters &parameters, std::vector<std::unique_ptr<OutputSink>> &&sinks);

            private:
                std::shared_ptr<output::Muxer> getMuxer(const OutputParameters &parameters);
                void linkSplitterToPad(pipeline::ElementPtr splitter, pipeline::PadPtr pad);

            private:
                pipeline::BinPtr _bin;
                std::shared_ptr<output::Muxer>              _muxer;
                std::vector<std::shared_ptr<OutputStream>>  _streams;
                std::vector<std::unique_ptr<OutputSink>>    _sinks;
                OutputParameters _parameters;
        };
    }
}

#endif /* __UTPL_TRANSCODER_OUTPUT_SINGLE_H__ */
