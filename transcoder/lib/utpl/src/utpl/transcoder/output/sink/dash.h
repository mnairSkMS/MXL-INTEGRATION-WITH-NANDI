#ifndef __UTPL_TRANSCODER_OUTPUT_SINK_DASH_H__
#define __UTPL_TRANSCODER_OUTPUT_SINK_DASH_H__

#include <gio/gio.h>

#include "utpl/transcoder/output/sink.h"

#include "../dash/presentation.h"

namespace utpl {
    namespace transcoder {
        /**
         * CMAF DASH output: parsers into dashsink2 (gst-plugins-rs, patched in
         * transcoder/patches/gst-plugins-rs). dashsink2 cuts every stream on one running-time
         * grid and writes the MPD; its get-*-stream signals hand each object to the shared
         * dash::Presentation, which uploads it, keeps the live window and publishes the MPD.
         */
        class DashOutputSink : public OutputSink {
            public:
                DashOutputSink(
                    const std::vector<StreamParameters> &streams,
                    const ScteOutputParameters          &scteParameters,
                    std::shared_ptr<dash::Presentation>  presentation,
                    const std::string                   &variantPrefix
                );

                void link() override;

                pipeline::ElementPtr getNode() override;

                pipeline::PadPtr getPad(MediaType mediaType, Pid pid = PID_NULL) override;

                void putScteData(const Scte35Data &data) override;

                bool isLive() const override;
                bool needExternalSync() const override;

                const std::set<MediaType> &getSupportedMediaTypes() const override;

            private:
                static GOutputStream *onInitStream(GstElement *sink, const gchar *location, DashOutputSink *self);
                static GOutputStream *onSegmentStream(GstElement *sink, const gchar *location, DashOutputSink *self);
                static GOutputStream *onManifestStream(GstElement *sink, const gchar *location, DashOutputSink *self);

                GOutputStream *createStream(dash::Presentation::ObjectKind kind, const gchar *location);

            private:
                struct StreamPad {
                    MediaType             mediaType;
                    pipeline::GhostPadPtr pad;
                };

                pipeline::BinPtr     _bin;
                pipeline::ElementPtr _sink;

                std::vector<StreamPad> _pads;

                Codec                _videoCodec;
                std::set<MediaType>  _supportedMediaTypes;
                ScteOutputParameters _scteParameters;

                std::shared_ptr<dash::Presentation> _presentation;
                int                                 _variant;
        };
    }
}

#endif /* __UTPL_TRANSCODER_OUTPUT_SINK_DASH_H__ */
