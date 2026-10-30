#ifndef __UTPL_TRANSCODER_OUTPUT_SINK_HLS_H__
#define __UTPL_TRANSCODER_OUTPUT_SINK_HLS_H__

#include <gst/app/gstappsink.h>

#include "utpl/transcoder/output/sink.h"

#include "utpl/hls/streamer.h"

namespace utpl {
    namespace transcoder {
        class HlsOutputSink : public utpl::transcoder::OutputSink {
            public:
                HlsOutputSink(
                    const std::string                   &name, 
                    const OutputTransport               &transport, 
                    const std::vector<StreamParameters> &streams, 
                    const ScteOutputParameters          &scteParameters, 
                    Container                            container,
                    std::shared_ptr<hls::Streamer>       streamer,
                    std::shared_ptr<hls::MediaPlaylist>  playlist
                );
        
                void link() override;
        
                pipeline::ElementPtr getNode() override;
        
                pipeline::PadPtr getPad(MediaType mediaType, Pid pid = PID_NULL) override;
        
                void putScteData(const Scte35Data &data) override;
        
                bool isLive() const override;
                bool needExternalSync() const override;
        
                const std::set<MediaType> &getSupportedMediaTypes() const override;
        
            private:
                static gchar* onFormatLocation(GstElement *splitmux, guint fragment_id, HlsOutputSink *self);
                static GstFlowReturn onNewSample(GstAppSink *sink, gpointer data);
            
            private:
                std::string          _name;
                Container            _container;
                pipeline::ElementPtr _splitmuxsink;
                pipeline::ElementPtr _muxer;
                pipeline::ElementPtr _sink;
                pipeline::BinPtr     _bin;
        
                struct StreamPad {
                    MediaType             mediaType;
                    Pid                   pid;
                    pipeline::GhostPadPtr pad;
                };

                std::vector<StreamPad> _pads;
        
                std::mutex guard;
        
                std::shared_ptr<utpl::hls::Streamer> streamer;
                
                std::shared_ptr<hls::Segment> currentSegment;
                std::shared_ptr<hls::Segment> lastSegment;
            
                std::shared_ptr<utpl::hls::MasterPlaylist> masterPlaylist;
                std::shared_ptr<utpl::hls::MediaPlaylist>  mediaPlaylist;
                GstClockTime runningTime;
                utpl::Timestamp chunkLength;
                bool vodMode;

                std::set<MediaType> _supportedMediaTypes;
                ScteOutputParameters _scteParameters;
        
        };
    }
}

#endif /* __UTPL_TRANSCODER_OUTPUT_SINK_HLS_H__ */
