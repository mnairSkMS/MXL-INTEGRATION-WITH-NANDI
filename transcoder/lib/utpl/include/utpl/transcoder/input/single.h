#ifndef __UTPL_TRANSCODER_INPUT_SINGLE_H__
#define __UTPL_TRANSCODER_INPUT_SINGLE_H__

#include <deque>

#include "utpl/common/utils.h"

#include "utpl/pipeline/bin.h"

#include "utpl/transcoder/input.h"
#include "utpl/transcoder/stream/input/base.h"

namespace utpl {
    namespace transcoder {
        class SingleInput : public Input {
            private:
                class SingleInputStream : public transcoder::BaseInputStream {
                    public:
                        SingleInputStream(pipeline::PadPtr &passthroughPeekPad, pipeline::PadPtr &passthroughPad, pipeline::BinPtr bin);
                        
                        pipeline::PadPtr peekPad(bool passthrough) override;

                        void link() override;
                        void unlink() override;

                    public:
                        void unblock();

                        // Demuxer stream id of the current pads (empty when the pad has none).
                        const std::string &getStreamId() const;

                        // The demuxer replaced the pads of this stream (an MPEG-TS program update that changes a stream
                        // type, e.g. AAC -> E-AC-3 on the same PID). Rebuilds the internal chain on the new pads, so the
                        // pads already handed to the channel keep flowing with the new codec.
                        void replacePads(pipeline::PadPtr &passthroughPeekPad, pipeline::PadPtr &passthroughPad);

                    protected:
                        pipeline::PadPtr getSrcPad(bool passthrough) override;

                    private:
                        void block(const std::vector<pipeline::PadPtr> &newBlocked);

                        static GValueArray *_decoderSortCallback(GstElement *bin, GstPad *pad, GstCaps *caps, GValueArray *factories, SingleInputStream *self);

                        std::string _getSplitterName();
                        std::string _getDecoderName();

                    private:
                        std::recursive_mutex _blockedPadsMutex;

                        bool _linkPassthrough;
                        bool _linkDecoded;

                        pipeline::PadPtr _passthroughPad;
                        pipeline::PadPtr _passthroughPeekPad;

                        bool _linked;
                        bool _streaming;

                        std::string _streamId;

                        std::unordered_map<pipeline::PadPtr, guint> _blockedPads;
                };

                enum class HlsTagType {
                    EXTINF,
                    CUE_OUT,
                    CUE_IN
                };

                struct HlsTag {
                    int        sequence;
                    double     duration;
                    HlsTagType tag;
                };

            public:
                SingleInput(bool useBuffering, const std::string &name = std::string());
                ~SingleInput();

                void prepare(const InputParameters &parameters) override;
                void probe(ProbeCallback callback) override;
                void cleanup() override;

                const std::vector<std::shared_ptr<InputStream>> &getStreams() const override;
                const InputParameters &getInputParameters() const override;

                void onScteData(ScteDataCallback callback) override;
                void onStateChanged(StateChangedCallback callback) override;
                void onStreamsChanged(StreamsChangedCallback callback) override;
                void onError(ErrorCallback callback) override;

                pipeline::ElementPtr getNode() override;
                
                bool isLive() const override;
                Protocol getProtocol() const override;
                std::string getName() const override;
                GstClockTime getDuration() const override;

                const std::string &getId() const override;

                InputStatus getStatus() override;

                Error setCurrentStream(const std::string &streamId) override;

            protected:
                static gboolean _autoPlugContinueCallback(
                    GstElement *bin, GstPad *pad, GstCaps *caps, Input *self
                );

                static int _autoPlugSelectCallback(
                    GstElement *bin, GstPad *pad, GstCaps *caps, GstElementFactory *factory, Input *self
                );

                static void _demuxerElementAdded(
                    GstBin *bin, GstElement *element, SingleInput *self
                );

            private:
                pipeline::ElementPtr _createDemuxer(bool filterAllScteEvents = false);

                static void _onBufferUnderrun(GstElement *buffer, SingleInput *input);
                static void _onBufferOverrun(GstElement *buffer, SingleInput *input);

                void _blockBuffer(bool block);

                void _getHlsTags(const gchar *buffer, std::vector<HlsTag> &tags);
                void _processHlsScteEvent(
                    GstClockTime prerollTime,
                    GstClockTime duration,
                    GstClockTime currentPosition,
                    GstClockTime currentSegmentDuration,
                    bool cueOut
                );
                static void _onPlaylistUpdate(GstElement *element, const gchar* buffer, gint64 currentSequence, GstClockTime currentPosition, SingleInput *input);
                static bool _restartSrc(SingleInput *self);

                // Exposes the pads of an adaptive streaming demuxer (hlsdemux, dashdemux2), one demuxer
                // decodebin per pad, and re-targets them when the source is restarted.
                void _linkAdaptiveDemuxer(pipeline::BinPtr &srcBin, pipeline::ElementPtr &demuxer, bool filterScteFromSegments, bool streamsAware = false);
                void _startAdaptiveStreams();

                static void _onDashElementAdded(GstBin *bin, GstBin *subBin, GstElement *element, SingleInput *self);
                static GstPadProbeReturn _onDashSegmentData(GstPad *pad, GstPadProbeInfo *info, gpointer data);
                void _injectDashScte35(uint32_t eventId, GstClockTime presentationTime, const std::vector<uint8_t> &section);
                
                uint32_t _calculateBackoffDelay(uint32_t attempt);
                void _clearRetry();

                GstClockTime _duration;

            private:
                pipeline::BinPtr _bin;
                InputParameters _inputParameters;

                std::mutex                                _streamsGuard;
                std::vector<std::shared_ptr<InputStream>> _streams;

                std::vector<pipeline::ElementPtr> _demuxers;

                ProbeCallback        _probeCallback;
                ScteDataCallback     _scteDataCallback;
                StateChangedCallback _stateChangeCallback;
                ErrorCallback        _errorCallback;

                std::recursive_mutex _bufferMutex;
                pipeline::ElementPtr _buffer;
                InputState           _state;
                
                std::unordered_map<pipeline::Pad *, guint> _bufferBlockId;
                
                Protocol             _protocol;
                std::string          _name;
                bool                 _isLive;
                bool                 _srcHasAllStreams;
                int                  _srcStreamsCount;
                int                  _srcDemuxersCount;
                bool                 _useBuffering;
                
                utils::AutoGeneratedId  _id;
                
                std::map<int, HlsTagType>  _hlsScteTags;
                uint32_t                   _hlsScteEventId;

                // dashdemux2 of a DASH input; its output segment maps 'emsg' times to running time
                pipeline::ElementPtr  _adaptiveDemuxer;
                std::recursive_mutex  _srcStreamsGuard;
                int                   _srcExpectedStreams;
                std::mutex            _dashEmsgGuard;
                std::deque<uint32_t>  _dashEmsgIds;

                guint    _srcRestartTimerId;
                uint32_t _srcRetryAttempt;
                guint32  _srcErrorSeq;
        };
    }
}

#endif /* __UTPL_TRANSCODER_INPUT_SINGLE_H__ */