#ifndef __UTPL_TRANSCODER_OUTPUT_SINK_UDP_H__
#define __UTPL_TRANSCODER_OUTPUT_SINK_UDP_H__

#include "utpl/transcoder/output/sink.h"

namespace utpl {
    namespace transcoder {
        class UdpOutputSink : public OutputSink {
            public:
                UdpOutputSink(const OutputTransport &transport);

                void link() override;
                pipeline::ElementPtr getNode() override;

                pipeline::PadPtr getPad(MediaType mediaType, Pid pid = PID_NULL) override;

                bool isLive() const override;
                bool needExternalSync() const override;

                const std::set<MediaType> &getSupportedMediaTypes() const override;

                void putScteData(const transcoder::Scte35Data &data) override;

            private:
                std::string           _host;
                uint16_t              _port;
                pipeline::BinPtr      _bin;
                pipeline::GhostPadPtr _pad;

                UdpOutputTransportParameters _transportParameters;

                std::set<MediaType> _supportedMediaTypes;
        };
    }
}

#endif /* __UTPL_TRANSCODER_OUTPUT_SINK_UDP_H__ */