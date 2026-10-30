#ifndef __UTPL_TRANSCODER_OUTPUT_SINK_FILE_H__
#define __UTPL_TRANSCODER_OUTPUT_SINK_FILE_H__

#include "utpl/transcoder/output/sink.h"

namespace utpl {
    namespace transcoder {
        class FileOutputSink : public OutputSink {
            public:
                FileOutputSink(const OutputTransport &transport);

                void link() override;
                pipeline::ElementPtr getNode() override;

                pipeline::PadPtr getPad(MediaType mediaType, Pid pid = PID_NULL) override;

                bool isLive() const override;
                bool needExternalSync() const override;

                const std::set<MediaType> &getSupportedMediaTypes() const override;

                void putScteData(const transcoder::Scte35Data &data) override;

            private:
                std::string           _location;
                pipeline::BinPtr      _bin;
                pipeline::GhostPadPtr _pad;

                std::set<MediaType> _supportedMediaTypes;
        };
    }
}

#endif /* __UTPL_TRANSCODER_OUTPUT_SINK_FILE_H__ */