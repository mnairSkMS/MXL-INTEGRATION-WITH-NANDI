#ifndef __UTPL_TRANSCODER_MXLTAP_H__
#define __UTPL_TRANSCODER_MXLTAP_H__

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include <gst/gst.h>

#include "utpl/pipeline/bin.h"
#include "utpl/transcoder/input.h"
#include "utpl/transcoder/mxlcarrier.h"
#include "utpl/transcoder/mxlframegrid.h"

namespace utpl {
    namespace transcoder {
        // A copy of one decoded input stream, written to an MXL flow (see MxlTapParameters).
        //
        // It is built like the input preview: in the media graph it is only a leaky queue and an
        // appsink hanging off the stream's tee, so it can drop frames but never hold the channel
        // back. The conversion and `mxlsink` live in a pipeline of their own, which shares the
        // channel's clock and base time so buffer times mean the same moment in both. Errors there
        // (a full tmpfs, a domain that disappeared) stay there: the side pipeline is dropped and
        // started again later, and the channel is not told.
        class MxlTap {
            public:
                enum class Kind {
                    VIDEO,
                    AUDIO
                };

                // Shared by the video and audio taps of one programme: see _alignment.
                struct Alignment;

                // Nullptr when there is nothing to do: no flow configured for this kind, the MXL
                // plugin is not installed, or another tap already writes this flow.
                static std::unique_ptr<MxlTap> create(const MxlTapParameters &parameters, Kind kind);

                ~MxlTap();

                // The branch to link to a tee: a "sink" pad, then queue and appsink.
                pipeline::BinPtr getBin();

                // True for a video tap that also writes the SCTE-35 data flow (see MxlTapParameters::getDataFlowId).
                bool hasCueFlow() const {
                    return ! this->_dataFlowId.empty();
                }

                // A finished SCTE-35 section for the data flow. It is written into the grain of every video frame from now
                // until its splice time (see mxl_carrier::CueQueue). A cue too large for a grain is refused and logged.
                // Safe to call from any thread.
                void putCue(const Scte35Data &data);

            private:
                MxlTap(const MxlTapParameters &parameters, Kind kind, const std::string &owner, const std::string &dataOwner);

                static GstFlowReturn onFeed(GstElement *sink, gpointer self);

                // 1: started. 0: the channel is not running yet, try again with the next buffer.
                // -1: failed, the caller backs off.
                int  _start(GstSample *sample);
                void _stop();

                // The side pipeline for pictures with this caps and size (video) or for the configured channels (audio).
                std::string _describe(GstCaps *caps, gsize bufferSize) const;

                // Once a second: lets go of the flow when the stream has delivered nothing for a while,
                // so that a stream that left the air does not leave a flow that looks alive.
                void _checkIdle();

            private:
                struct Watch;

                MxlTapParameters _parameters;
                Kind             _kind;
                std::string      _owner;    // "<domain>|<flow id>", claimed for the tap's lifetime
                std::string      _dataOwner; // the same for the data flow; empty without one
                std::string      _flowId;
                std::string      _dataFlowId; // empty when this tap writes no data flow
                pipeline::BinPtr _bin;
                GstElement      *_feed = nullptr;

                std::mutex       _guard;
                GstElement      *_pipeline = nullptr; // the side pipeline, while it runs
                GstElement      *_source   = nullptr; // its appsrc
                GstElement      *_dataSource = nullptr; // the appsrc of the data flow's chain in the same pipeline
                std::string      _shape;              // size and rate it was started for (video)

                MxlFrameGrid     _grid;               // video only: the place of each frame in the flow
                int              _rateNum = 0;        // video only: the frame rate of the picture being written
                int              _rateDen = 1;

                // How far the programme's pictures and sound are moved to put the pictures exactly on the grain grid of the flow
                // (see MxlFrameGrid::alignmentDelay). The video tap works it out when its grid starts; the audio tap of the same
                // programme (the same video flow id) takes it from there, so that both move together.
                std::shared_ptr<Alignment> _alignment;
                std::chrono::steady_clock::time_point _firstBufferAt;   // of the first buffer to reach the tap; the epoch until then

                std::mutex                _cueGuard;      // putCue comes from the channel's thread, the grain from the feed's
                mxl_carrier::CueQueue     _cues;
                guint64                   _framePeriod = 0;   // nanoseconds, video with a data flow
                guint64                   _cuesAccepted = 0;
                guint64                   _cuesRefused  = 0;
                guint64                   _grains       = 0;   // data grains written
                guint64                   _grainsWithCues = 0;

                std::shared_ptr<Watch> _watch;
                guint                  _watchId = 0;
                std::chrono::steady_clock::time_point _lastSample;

                std::atomic<bool> _errored { false };
                std::chrono::steady_clock::time_point _retryAt;

                // Counters, for the log line written when the pipeline is replaced or stopped.
                guint64 _pushed   = 0;
                guint64 _restarts = 0;
                guint64 _statsPushed = 0;  // _pushed at the last line of statistics

                // How old the buffers were when they reached the tap, the channel's time then minus the time they carry, since
                // the last line of statistics (nanoseconds). Picture and sound reach a tap behind the processing of their own
                // chains, so the two kinds differ by a constant, which a reader of the flows has to allow for.
                gint64  _ageLeast = G_MAXINT64;
                gint64  _ageMost  = G_MININT64;
                std::chrono::steady_clock::time_point _statsAt;
        };
    }
}

#endif /* __UTPL_TRANSCODER_MXLTAP_H__ */
