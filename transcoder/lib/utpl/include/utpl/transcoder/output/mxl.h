#ifndef __UTPL_TRANSCODER_OUTPUT_MXL_H__
#define __UTPL_TRANSCODER_OUTPUT_MXL_H__

#include <memory>
#include <vector>

#include "utpl/pipeline/bin.h"

#include "utpl/transcoder/mxltap.h"
#include "utpl/transcoder/output.h"

namespace utpl {
    namespace transcoder {
        // The finished programme, written to MXL flows (see OutputType::MXL). It takes the place of an
        // encoder, a muxer and a sink: the channel feeds it the processor's raw picture and audio through
        // a flowline that does not encode, and each stream ends in an MxlTap, which converts the media
        // in a side pipeline of its own and writes the flow. A tap that cannot run stops itself and the
        // channel carries on, so this output is fail-open like the source tap.
        class MxlOutput : public Output {
            public:
                MxlOutput();
                virtual ~MxlOutput();

                void prepare(const OutputParameters &parameters) override;
                void cleanup() override;

                const std::vector<std::shared_ptr<OutputStream>> &getStreams() override;
                bool isLive() const override;

                pipeline::ElementPtr getNode() override;

                // With `mxl.data.flowId` the programme's SCTE-35 goes into that MXL data flow (Q7: this engine keeps the timeline,
                // the flow carries the finished cues). Without it the output carries none.
                void putScteData(const Scte35Data &data) override;

                const OutputParameters &getOutputParameters() const override;

            private:
                pipeline::BinPtr                           _bin;
                std::vector<std::shared_ptr<OutputStream>> _streams;
                std::vector<std::unique_ptr<MxlTap>>       _taps;
                OutputParameters                           _parameters;
        };
    }
}

#endif /* __UTPL_TRANSCODER_OUTPUT_MXL_H__ */
