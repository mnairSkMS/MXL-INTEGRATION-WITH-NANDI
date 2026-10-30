#ifndef __UTPL_TRANSCODER_OUTPUT_GROUP_H__
#define __UTPL_TRANSCODER_OUTPUT_GROUP_H__

#include "utpl/transcoder/output.h"
#include "utpl/transcoder/output/single.h"

#include "utpl/hls/streamer.h"

namespace utpl {
    namespace transcoder {
        class GroupOutput : public Output {
            public:
                GroupOutput(const std::vector<std::shared_ptr<SingleOutput>> &elements);
                virtual ~GroupOutput();

                void prepare(const OutputParameters &parameters) override;
                void cleanup() override;

                const std::vector<std::shared_ptr<OutputStream>> &getStreams() override;
                bool isLive() const override;

                pipeline::ElementPtr getNode() override;

                void putScteData(const Scte35Data &data) override;

                const OutputParameters &getOutputParameters() const override;

            private:
                void prepareDash(const OutputParameters &parameters);

            private:
                OutputParameters _parameters;

                std::vector<std::shared_ptr<SingleOutput>> _children;

                std::shared_ptr<hls::Streamer>       _hlsStreamer;
                std::shared_ptr<hls::MasterPlaylist> _hlsMasterPlaylist;
        };
    }
}

#endif /* __UTPL_TRANSCODER_OUTPUT_GROUP_H__ */
