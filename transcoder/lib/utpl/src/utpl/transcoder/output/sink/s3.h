#ifndef __UTPL_TRANSCODER_OUTPUT_SINK_S3_H__
#define __UTPL_TRANSCODER_OUTPUT_SINK_S3_H__

#include "utpl/transcoder/output/sink.h"

namespace utpl {
    namespace transcoder {
        class S3OutputSink : public OutputSink {
            public:
                S3OutputSink(const OutputTransport &transport);

                void link() override;
                pipeline::ElementPtr getNode() override;

                pipeline::PadPtr getPad(MediaType mediaType, Pid pid = PID_NULL) override;

                bool isLive() const override;
                bool needExternalSync() const override;

                const std::set<MediaType> &getSupportedMediaTypes() const override;

                void putScteData(const transcoder::Scte35Data &data) override;

            private:
                std::string           _bucket;
                std::string           _accessKey;
                std::string           _secretAccessKey;
                std::string           _region;
                std::string           _keyPrefix;
                pipeline::BinPtr      _bin;
                pipeline::GhostPadPtr _pad;

                S3OutputTransportParameters _transportParameters;

                std::set<MediaType> _supportedMediaTypes;

            public:
                pipeline::ElementPtr   _queue;
                pipeline::ElementPtr   _demux;
                pipeline::ElementPtr   _audioQueue;
                pipeline::ElementPtr   _videoQueue;
                pipeline::ElementPtr   _videoParser;
                pipeline::ElementPtr   _audioParser;
                pipeline::ElementPtr   _hlssink;
                pipeline::ElementPtr   _awss3hlssink;
                std::shared_ptr<utpl::pipeline::Pad>    _demuxAudioPad;

        };
    }
}

#endif /* __UTPL_TRANSCODER_OUTPUT_SINK_S3_H__ */