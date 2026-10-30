#ifndef __UTPL_TRANSCODER_PREVIEW_H__
#define __UTPL_TRANSCODER_PREVIEW_H__

#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <gst/gst.h>

#include "utpl/pipeline/bin.h"

namespace utpl {
    namespace transcoder {
        class InputStream;

        // What an input's preview tap produces. A tap with no demand is closed and costs
        // one dropped buffer per frame on a branch of the stream's tee.
        enum class PreviewMode {
            OFF,
            STILL, // keyframes only, about one JPEG every GOP
            LIVE   // up to PREVIEW_LIVE_FPS JPEGs a second
        };

        // A JPEG preview of one input video stream, in two parts.
        //
        // In the media graph: a branch off the stream's tee made only of simple synchronous
        // elements - a valve that starts closed, a keyframe filter, a leaky queue and an
        // appsink - so it can never stall or post errors into the on-air pipeline.
        //
        // Outside it: a small pipeline of its own (appsrc, decoder when the stream is
        // encoded, scale, JPEG, appsink), created when someone starts watching and torn
        // down when demand stops. Buffers cross by reference. An idle input has no decoder.
        class PreviewTap {
            public:
                PreviewTap(InputStream *stream, bool encoded);
                ~PreviewTap();

                pipeline::BinPtr getBin();

                // Opens or closes the valve and sets the frame rate. Thread safe.
                void apply(PreviewMode mode);

            private:
                static GstFlowReturn onFeed(GstElement *sink, gpointer self);
                static GstFlowReturn onSample(GstElement *sink, gpointer self);

                GstElement *_startRenderer(GstCaps *caps);
                void        _stopRenderer(bool synchronous);

            private:
                InputStream     *_stream;
                bool             _encoded;
                pipeline::BinPtr _bin;
                GstElement      *_valve      = nullptr;
                GstElement      *_keyframes  = nullptr;
                GstElement      *_renderer   = nullptr; // the decode/JPEG pipeline, while watched
                GstElement      *_source     = nullptr; // its appsrc
                GstElement      *_rate       = nullptr; // its videorate
                PreviewMode      _mode       = PreviewMode::OFF;
                std::mutex       _guard;

                std::chrono::steady_clock::time_point _lastPush; // decoded streams: last frame handed on
        };

        // Process-wide registry joining preview demand (by input id, from the API) to the
        // taps (by stream, from the media graph). The API renews demand every few seconds
        // while someone watches; each renewal also binds the input's current video streams,
        // so streams created later, such as the next playlist item, are picked up by the
        // next renewal. A tap closes itself once its input's demand has expired.
        class PreviewHub {
            public:
                static PreviewHub &instance();

                // False when UTPL_PREVIEW is "off" or "0": no stream gets a tap, and the
                // media graph is exactly what it is without this feature.
                bool isEnabled() const;

                // Directory the JPEGs are written to: <directory>/<input id>.jpg. Defaults
                // to /dev/shm/utpl-preview so frames never touch the disk.
                void setDirectory(const std::string &directory);
                std::string framePath(const std::string &inputId);

                // Renews demand for an input and binds its video streams. Returns how many of
                // those streams carry a tap.
                int demand(
                    const std::string &inputId,
                    PreviewMode mode,
                    std::chrono::milliseconds ttl,
                    const std::vector<InputStream *> &videoStreams
                );

                void registerTap(InputStream *stream, PreviewTap *tap);
                void unregisterTap(InputStream *stream);

                // Called by a tap for every frame: the mode it should be in now, and where to
                // write the frame. OFF means the demand expired.
                PreviewMode currentMode(InputStream *stream, std::string &path);

            private:
                PreviewHub();

                PreviewMode _modeOf(const std::string &inputId);

            private:
                struct Demand {
                    std::chrono::steady_clock::time_point still;
                    std::chrono::steady_clock::time_point live;
                };

                std::mutex                             _guard;
                bool                                   _enabled   = true;
                std::string                            _directory = "/dev/shm/utpl-preview"; // UTPL_PREVIEW_DIR
                std::map<InputStream *, PreviewTap *>  _taps;
                std::map<InputStream *, std::string>   _bindings;
                std::map<std::string, Demand>          _demands;
        };
    }
}

#endif /* __UTPL_TRANSCODER_PREVIEW_H__ */
