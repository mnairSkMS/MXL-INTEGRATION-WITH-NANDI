#ifndef __TRANSCODER_CHANNEL_CONNECTOR_PASSTHROUGH_H__
#define __TRANSCODER_CHANNEL_CONNECTOR_PASSTHROUGH_H__

#include "utpl/transcoder/channel/connector.h"

namespace utpl {
    namespace transcoder {
        class PasstroughConnector : public StreamConnector {
            public:
                PasstroughConnector(const std::string &name, std::shared_ptr<InputStream> inputStream, const StreamParameters &outputParameters);

            public:
                std::shared_ptr<InputStream> getStream() override;
                pipeline::PadPtr getPad(const StreamParameters &parameters) override;
                pipeline::PadPtr peekPad() override;
                
                const StreamParameters &getStreamParameters() override;

                void prepare(pipeline::BinPtr &bin) override;
                void link() override;
                void unlink() override;
                void cleanup() override;
                pipeline::ElementPtr getNode() override;

            private:
                pipeline::BinPtr             _bin;
                std::shared_ptr<InputStream> _inputStream;
                pipeline::PadPtr             _inputPad;

                StreamParameters             _outputParameters;
                pipeline::ElementPtr         _splitter;
                pipeline::ElementPtr         _identity;

                std::vector<pipeline::GhostPadPtr> _pads;
        };
    }
}

#endif /* __TRANSCODER_CHANNEL_CONNECTOR_PASSTHROUGH_H__ */