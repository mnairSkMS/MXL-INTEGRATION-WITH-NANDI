#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <set>

#include "utpl/common/debug.h"
#include "utpl/common/types/exception.h"

#include "utpl/pipeline/utils.h"

#include "utpl/transcoder/input/single.h"
#include "utpl/transcoder/input/playlist.h"

#include "utpl/playlist/parser.h"

#include "utpl/logger.h"

using namespace utpl;

static auto &logger = Logger::getLogger(UTPL_LOGGER_NAME_INPUT);

// Gap left between the connectors' filler and a playlist item committed after it (one video frame)
constexpr GstClockTime PLAYLIST_FILLER_MARGIN = 40 * GST_MSECOND;

// A hold queue pushes from its own thread, and an event it pushes before the cut links its src pad
// fails with not-linked, which the queue posts as a fatal error. Buffers are held by _holdUntilCut;
// sticky events are stored on the unlinked pad and resent once it is linked.
static GstPadProbeReturn _dropEventsWhileUnlinked(GstPad *pad, GstPadProbeInfo *info, gpointer) {
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);

    if (GST_EVENT_IS_STICKY(event) && GST_EVENT_TYPE(event) != GST_EVENT_EOS) {
        return GST_PAD_PROBE_OK;
    }

    return gst_pad_is_linked(pad) ? GST_PAD_PROBE_OK : GST_PAD_PROBE_DROP;
}

static GstPadProbeReturn _dropAll(GstPad *, GstPadProbeInfo *, gpointer) {
    return GST_PAD_PROBE_DROP;
}

// How long before the end of an item its follower starts being prepared.
//
// Preparing means building a source and probing it, which takes a few hundred milliseconds for a
// local file and longer for anything remote. Two seconds of warning was not enough: a transition
// that missed it played black until the probe finished, and an input frozen off air mid-item came
// back with nothing prepared at all. Six seconds covers a slow probe with room to spare, at the
// cost of holding the next item open for that much longer.
//
// It cannot be raised indefinitely: an item shorter than this is already inside the window when it
// starts, so the follower is prepared immediately - which is correct, but means a playlist of very
// short items keeps two sources open continuously.
static constexpr GstClockTime SWITCH_THRESHOLD = 6000 * GST_MSECOND;

// Decoded output an armed item may queue while the cut waits (see CUT_HOLD_MAX_US)
static constexpr guint64 HOLD_QUEUE_MAX_TIME = 2 * GST_SECOND;

// The longest a live item's audio waits for its first video frame before playing without it
static constexpr GstClockTime AUDIO_WAIT_FOR_VIDEO = 2 * GST_SECOND;

#define INFO_OVERLAY 0


// A live source (UDP, SRT, RTMP) stamps its buffers with the pipeline running time at arrival,
// while a file starts at zero. concat places every new item after everything already played, so
// a live item committed after T seconds of output would land T seconds in the future and the
// output would show black for that long. Each live item's first buffer therefore fixes one
// offset, shared by all of the item's streams so that audio and video stay in sync, that removes
// the smaller of its own arrival time and concat's position at commit (base). After a file, base
// is at or past the arrival time and the item continues the playlist like a file would. At
// start-up base is zero and the item keeps its arrival time: rebasing it to zero there would
// leave every frame late by the time the first one took to arrive, and the compositor would drop
// them all.
struct LiveTimeline {
    std::mutex   mutex;
    bool         known  = false;
    gint64       offset = 0;
    GstClockTime base   = 0;

    // Running time of the first buffer, and how far it was played earlier than its timestamps
    GstClockTime firstAt = GST_CLOCK_TIME_NONE;
    GstClockTime trim    = 0;

    // Pads the offset was applied to, so a later, tighter stream can move them all together
    std::vector<GstPad *> pads;

    // The item has video: its audio waits for, and is keyed on, the first video frame. Audio
    // decoded before it is from before the first IDR and arrives late against the clock.
    bool         expectVideo  = false;
    GstClockTime firstAudioAt = GST_CLOCK_TIME_NONE;

    ~LiveTimeline() {
        for (auto *pad : this->pads) {
            gst_object_unref(pad);
        }
    }
};

// A live MPEG-TS item is stamped ahead of its arrival by the encoder's mux delay (PTS - PCR,
// typically 0.5-0.8 s), and the playlist would hold each frame for that long. Playing it this
// long after its decoded arrival instead absorbs decoder and SRT delivery jitter.
static GstClockTime _liveArrivalMargin() {
    static GstClockTime margin = []() {
        const char *value = std::getenv("UTPL_PLAYLIST_LIVE_MARGIN_MS");

        return (value && *value ? (GstClockTime) std::strtoull(value, nullptr, 10) : 80) * GST_MSECOND;
    }();

    return margin;
}

// Clock running time of the pipeline the pad is in, or NONE
static GstClockTime _clockRunningTime(GstPad *pad) {
    GstClockTime ret     = GST_CLOCK_TIME_NONE;
    GstElement  *element = gst_pad_get_parent_element(pad);

    if (element) {
        if (GstClock *clock = gst_element_get_clock(element)) {
            GstClockTime now  = gst_clock_get_time(clock);
            GstClockTime base = gst_element_get_base_time(element);

            if (GST_CLOCK_TIME_IS_VALID(now) && GST_CLOCK_TIME_IS_VALID(base) && now >= base) {
                ret = now - base;
            }

            gst_object_unref(clock);
        }

        gst_object_unref(element);
    }

    return ret;
}

// How much earlier than its timestamps a live buffer can play: its lead over the clock less the margin
static GstClockTime _liveTrim(GstClockTime at, GstClockTime now) {
    if (! GST_CLOCK_TIME_IS_VALID(at) || ! GST_CLOCK_TIME_IS_VALID(now) || at <= now + _liveArrivalMargin()) {
        return 0;
    }

    return at - now - _liveArrivalMargin();
}

class transcoder::PlaylistInput::PlaylistStream : public transcoder::BaseInputStream {
    public:
        struct StreamContext {
            pipeline::PadPtr streamDecodedPad;
            pipeline::PadPtr streamPassthroughPad;

            std::shared_ptr<transcoder::InputStream> stream;
            std::shared_ptr<LiveTimeline> timeline;
            // An armed decoded pad feeds concat through this queue, so the item keeps decoding
            // while its output is held for the cut
            pipeline::ElementPtr holdQueue;
            bool linked;
            // Pads requested and decoders built, but not yet connected to concat
            bool armed;

            StreamContext(std::shared_ptr<transcoder::InputStream> s, std::shared_ptr<LiveTimeline> t) {
                this->stream   = s;
                this->timeline = t;
                this->linked   = false;
                this->armed    = false;
            }
        };

    public:
        PlaylistStream(pipeline::BinPtr bin, bool video, std::function<void()> onEosCallback) : BaseInputStream(bin) {
            this->onEosCallback = onEosCallback;

            {
                auto parameters = this->getStreamParameters();

                if (video) {
                    parameters.setVideoParameters({});

                } else {
                    parameters.setAudioParameters({});
                }

                this->setStreamParameters(parameters);
            }

            this->passthrough.prepare(this->getBin(), video, true);
            this->decoded.prepare(this->getBin(), video, false);

            this->useDecoded     = false;
            this->usePassthrough = false;
        }

        pipeline::PadPtr getSrcPad(bool passthrough) override {
            auto &element = passthrough ? this->passthrough : this->decoded;

            if (passthrough) {
                this->usePassthrough = true;

            } else {
                this->useDecoded = true;
            }

            return element.getSrcPad();
        }

        void _unlink(StreamContext &context) {
            if (context.linked || context.armed) {
                if (context.holdQueue) {
                    auto queueSrc = context.holdQueue->getPad("src");

                    // The queue still holds this item's frames: drop them rather than let its
                    // thread push into the pad unlinked below, or after the cut
                    gst_pad_add_probe(
                        (*queueSrc)->get(),
                        (GstPadProbeType) (GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM),
                        _dropAll, nullptr, nullptr
                    );

                    auto peer = queueSrc->getPeer();

                    if (peer) {
                        queueSrc->unlink();

                        this->decoded.releasePad(peer);
                    }

                    if (context.streamDecodedPad && context.streamDecodedPad->getPeer()) {
                        context.streamDecodedPad->unlink();
                    }

                    context.holdQueue->null();

                    this->getBin()->remove(context.holdQueue);

                    context.holdQueue = nullptr;
                }

                if (context.streamDecodedPad) {
                    auto peer = context.streamDecodedPad->getPeer();
    
                    if (peer) {
                        context.streamDecodedPad->unlink();
    
                        this->decoded.releasePad(peer);
                    }
    
                    context.stream->releasePad(
                        context.streamDecodedPad
                    );
    
                    context.streamDecodedPad = nullptr;
                }
    
                if (context.streamPassthroughPad) {
                    auto peer = context.streamPassthroughPad->getPeer();
    
                    if (peer) {
                        context.streamPassthroughPad->unlink();
    
                        this->passthrough.releasePad(peer);
                    }
    
                    context.stream->releasePad(
                        context.streamPassthroughPad
                    );
    
                    context.streamPassthroughPad = nullptr;
                }

                context.stream->unlink();

                context.linked = false;
                context.armed  = false;
            }
        }

        void _monitorEos(const pipeline::PadPtr &pad) {
            pad->event(
                true,
                [this](GstEvent *event, bool &discard) {
                    bool ret = GST_EVENT_TYPE(event) != GST_EVENT_EOS;

                    if (! ret) {
                        this->eos = true;

                        this->onEosCallback();

                        discard = true;
                    }

                    return ret;
                }
            );
        }

        static void _rebaseLiveTimeline(const pipeline::PadPtr &pad, std::shared_ptr<LiveTimeline> timeline, bool video) {
            struct Context {
                std::shared_ptr<LiveTimeline> timeline;
                bool                          video;
            };

            // Runs on the item's own pad, before concat. gst_pad_push_data re-sends the sticky
            // segment after the probe, so the offset set here already applies to this buffer.
            gst_pad_add_probe(
                (*pad)->get(),
                GST_PAD_PROBE_TYPE_BUFFER,
                [](GstPad *pad, GstPadProbeInfo *info, gpointer data) -> GstPadProbeReturn {
                    auto *context  = static_cast<Context *>(data);
                    auto &timeline = context->timeline;

                    GstClockTime timestamp = GST_BUFFER_DTS_OR_PTS(GST_PAD_PROBE_INFO_BUFFER(info));

                    if (! GST_CLOCK_TIME_IS_VALID(timestamp)) {
                        return GST_PAD_PROBE_OK;
                    }

                    GstEvent *event = gst_pad_get_sticky_event(pad, GST_EVENT_SEGMENT, 0);

                    if (! event) {
                        return GST_PAD_PROBE_OK;
                    }

                    const GstSegment *segment;
                    gst_event_parse_segment(event, &segment);

                    GstClockTime at = segment->format == GST_FORMAT_TIME
                        ? gst_segment_to_running_time(segment, GST_FORMAT_TIME, timestamp)
                        : GST_CLOCK_TIME_NONE;

                    gst_event_unref(event);

                    if (! GST_CLOCK_TIME_IS_VALID(at)) {
                        return GST_PAD_PROBE_OK;
                    }

                    {
                        std::lock_guard lock(timeline->mutex);

                        bool waitsForVideo = ! context->video && timeline->expectVideo;

                        if (waitsForVideo) {
                            if (! GST_CLOCK_TIME_IS_VALID(timeline->firstAudioAt)) {
                                timeline->firstAudioAt = at;
                            }

                            // Before the first video frame, for at most AUDIO_WAIT_FOR_VIDEO in
                            // case the video never arrives
                            bool beforeVideo = timeline->known
                                ? at < timeline->firstAt
                                : at < timeline->firstAudioAt + AUDIO_WAIT_FOR_VIDEO;

                            if (beforeVideo) {
                                return GST_PAD_PROBE_DROP;
                            }
                        }

                        GstClockTime trim = _liveTrim(at, _clockRunningTime(pad));

                        if (! timeline->known) {
                            timeline->firstAt = at;
                            timeline->trim    = trim;
                            timeline->offset  = -static_cast<gint64>(std::min(at, timeline->base + trim));
                            timeline->known   = true;

                            logger.log(
                                "Rebasing live playlist item by {} ms to continue the playlist timeline ({} ms earlier than its timestamps)",
                                timeline->offset / (gint64) GST_MSECOND,
                                (gint64) (trim / GST_MSECOND)
                            );

                        } else if (trim < timeline->trim) {
                            // This stream arrives closer to its timestamps: move the item later,
                            // or this stream would be late
                            gint64 offset = -static_cast<gint64>(std::min(timeline->firstAt, timeline->base + trim));
                            gint64 delta  = offset - timeline->offset;

                            for (auto *other : timeline->pads) {
                                gst_pad_set_offset(other, gst_pad_get_offset(other) + delta);
                            }

                            DBG(("Live playlist item moved {} ms later for a stream that arrives closer to its timestamps", delta / (gint64) GST_MSECOND));

                            timeline->offset = offset;
                            timeline->trim   = trim;
                        }

                        gst_pad_set_offset(pad, gst_pad_get_offset(pad) + timeline->offset);

                        timeline->pads.push_back(GST_PAD(gst_object_ref(pad)));
                    }

                    return GST_PAD_PROBE_REMOVE;
                },
                new Context { timeline, video },
                [](gpointer data) {
                    delete static_cast<Context *>(data);
                }
            );
        }

        void _link(StreamContext &streamContext) {
            auto &stream = streamContext.stream;

            if (streamContext.linked) {
                DBG(("Skipping internal linking of playlist stream - already linked"));

            } else {
                streamContext.linked = this->useDecoded || this->usePassthrough;

                if (! streamContext.linked) {
                    DBG(("Expected internal link of playlist stream but no PADs were requested - skipping linkinkg"));

                } else {
                    if (this->useDecoded) {
                        DBG(("Internally linking playlist stream (decoded pad) of type: '{}'", this->getStreamParameters().toString()));

                        streamContext.streamDecodedPad = stream->requestPad(false);
                        streamContext.streamDecodedPad->link(
                            this->decoded.requestPad()
                        );
                    }    

                    if (this->usePassthrough) {
                        DBG(("Internally linking playlist stream (passthrough pad) of type: '{}'", this->getStreamParameters().toString()));

                        streamContext.streamPassthroughPad = stream->requestPad(true);
                        streamContext.streamPassthroughPad->link(
                            this->passthrough.requestPad()
                        );
                    }

                    this->_monitorEos(this->useDecoded ? streamContext.streamDecodedPad : streamContext.streamPassthroughPad);

                    if (streamContext.timeline) {
                        bool video = stream->getStreamParameters().hasVideoParameters();

                        for (auto &pad : { streamContext.streamDecodedPad, streamContext.streamPassthroughPad }) {
                            if (pad) {
                                _rebaseLiveTimeline(pad, streamContext.timeline, video);
                            }
                        }
                    }

                    DBG(("Linking original stream '{}' with playlist stream '{}'", stream->getStreamParameters().toString(), this->getStreamParameters().toString()));

                    stream->link();
                }
            }
        }

        void link() override {
            if (! this->_streams.empty()) {
                this->BaseInputStream::link();

                DBG(("[{}] Linking {} streams, expected decoded/passthrough pad: {}/{}", (void *) this, this->_streams.size(), this->useDecoded, this->usePassthrough));

                for (auto &streamContext : _streams) {
                    _link(streamContext);
                }

                if (this->useDecoded) {
                    this->decoded.activate();
                }

                if (this->usePassthrough) {
                    this->passthrough.activate();
                }

                this->eos = false;
            }
        }

        void unlink() override {
            DBG(("Unlinking {} streams, expected decoded/passthrough pad: {}/{}", this->_streams.size(), this->useDecoded, this->usePassthrough));

            if (this->usePassthrough) {
                this->passthrough.deactivate();
            }

            if (this->useDecoded) {
                this->decoded.deactivate();
            }

            this->BaseInputStream::unlink();

            for (auto &streamContext : _streams) {
                _unlink(streamContext);
            }

            this->useDecoded     = false;
            this->usePassthrough = false;
            this->eos            = true;
        }

        bool isLinked() const {
            return this->useDecoded || this->usePassthrough;
        }

        void update(const transcoder::StreamParameters &params) {
            DBG(("Updating stream parameters from '{}' to '{}'", this->getStreamParameters().toString(), params.toString()));

            this->setStreamParameters(params);
        }

        void addInputStream(std::shared_ptr<transcoder::Input> input, std::shared_ptr<transcoder::InputStream> stream, std::shared_ptr<LiveTimeline> timeline) {
            DBG(("Adding input stream of '{}' for input: {}", stream->getStreamParameters().toString(), (void *) input.get()));

            this->_streams.push_back({ stream, timeline });

            _link(*this->_streams.rbegin());
        }

        // Make-before-break, first half: request the item's pads and build its decoders once,
        // but leave the pads unconnected. concat holds back stream-start and segment events of a
        // pad that is not current, so connecting now would stall the item's decoders before the
        // first frame. Returns the pads the caller must hold until the cut.
        std::vector<pipeline::PadPtr> armInputStream(std::shared_ptr<transcoder::InputStream> stream, std::shared_ptr<LiveTimeline> timeline) {
            std::vector<pipeline::PadPtr> ret;

            this->_streams.push_back({ stream, timeline });

            auto &streamContext = *this->_streams.rbegin();

            if (this->useDecoded) {
                streamContext.streamDecodedPad = stream->requestPad(false);
                streamContext.holdQueue        = pipeline::utils::createQueue(this->getBin(), std::string(), 0, 0, HOLD_QUEUE_MAX_TIME);

                streamContext.streamDecodedPad->link(streamContext.holdQueue->getPad("sink"));
                gst_pad_add_probe(
                    (*streamContext.holdQueue->getPad("src"))->get(),
                    GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                    _dropEventsWhileUnlinked, nullptr, nullptr
                );

                streamContext.holdQueue->syncWithParent();

                ret.push_back(streamContext.holdQueue->getPad("src"));
            }

            if (this->usePassthrough) {
                streamContext.streamPassthroughPad = stream->requestPad(true);

                ret.push_back(streamContext.streamPassthroughPad);
            }

            streamContext.armed = ! ret.empty();

            if (streamContext.armed) {
                DBG(("Arming playlist stream '{}'", stream->getStreamParameters().toString()));

                stream->link();
            }

            return ret;
        }

        // Make-before-break, second half: connect armed pads of the stream to concat.
        void connectArmedStream(const std::shared_ptr<transcoder::InputStream> &stream) {
            for (auto &streamContext : this->_streams) {
                if (streamContext.stream == stream && streamContext.armed) {
                    pipeline::PadPtr decodedOut;

                    if (streamContext.streamDecodedPad) {
                        decodedOut = streamContext.holdQueue ? streamContext.holdQueue->getPad("src") : streamContext.streamDecodedPad;

                        decodedOut->link(this->decoded.requestPad());
                    }

                    if (streamContext.streamPassthroughPad) {
                        streamContext.streamPassthroughPad->link(this->passthrough.requestPad());
                    }

                    this->_monitorEos(decodedOut ? decodedOut : streamContext.streamPassthroughPad);

                    streamContext.armed  = false;
                    streamContext.linked = true;
                }
            }

            this->eos = false;
        }

        // concat continues a live item where its own bookkeeping says the previous item ended: one
        // buffer past the end we track, plus any audio/video alignment. That lands the item later
        // than the playlist needs, and the excess would stay as added latency for as long as the
        // item is on air. On the first buffer out of concat, shift the item so it plays at its
        // arrival time plus the residual the playlist really needs (the same for audio and video,
        // so they stay in sync). Setting the offset in the probe applies to that buffer too.
        void alignToArrival(std::function<GstClockTime()> firstArrival, GstClockTime residual) {
            struct State {
                std::function<GstClockTime()> firstArrival;
                GstClockTime residual;
            };

            GstPad *srcPad = (*this->decoded.concat->getPad("src"))->get();

            gst_pad_add_probe(
                srcPad,
                GST_PAD_PROBE_TYPE_BUFFER,
                [](GstPad *srcPad, GstPadProbeInfo *info, gpointer data) -> GstPadProbeReturn {
                    auto &state  = *static_cast<State *>(data);
                    auto *buffer = GST_PAD_PROBE_INFO_BUFFER(info);

                    GstClockTime runningTime = GST_CLOCK_TIME_NONE;

                    if (GST_BUFFER_PTS_IS_VALID(buffer)) {
                        if (GstEvent *event = gst_pad_get_sticky_event(srcPad, GST_EVENT_SEGMENT, 0)) {
                            const GstSegment *segment;
                            gst_event_parse_segment(event, &segment);

                            runningTime = gst_segment_to_running_time(segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer));

                            gst_event_unref(event);
                        }
                    }

                    GstClockTime arrival = state.firstArrival();

                    if (GST_CLOCK_TIME_IS_VALID(runningTime) && GST_CLOCK_TIME_IS_VALID(arrival)) {
                        gint64 output = (gint64) runningTime + gst_pad_get_offset(srcPad);
                        gint64 excess = output - (gint64) (arrival + state.residual);

                        if (excess > 0) {
                            gst_pad_set_offset(srcPad, gst_pad_get_offset(srcPad) - excess);

                            DBG(("Live playlist item started {} ms later than needed - moved back to its arrival latency", excess / (gint64) GST_MSECOND));
                        }
                    }

                    return GST_PAD_PROBE_REMOVE;
                },
                new State { firstArrival, residual },
                [](gpointer data) {
                    delete static_cast<State *>(data);
                }
            );
        }

        // Duration of the last buffer out of concat. concat records a buffer's end before pushing
        // it, so the next item can start up to one buffer past getCurrentTime().
        GstClockTime getLastDuration() const {
            return this->useDecoded ? this->decoded.lastDuration : this->passthrough.lastDuration;
        }

        bool usesDecoded() const {
            return this->useDecoded;
        }

        bool usesPassthrough() const {
            return this->usePassthrough;
        }

        void removeStreamsByInput(std::shared_ptr<transcoder::Input> &input) {
            auto tsDecoded     = this->decoded.getCurrentTime();
            auto tsPassthrough = this->passthrough.getCurrentTime();

            // Synchronize timestamps of passthrough and decoded paths
            if (tsDecoded > tsPassthrough) {
                this->passthrough.applyTimeOffset(tsDecoded - tsPassthrough);

            } else {
                this->decoded.applyTimeOffset(tsPassthrough - tsDecoded);
            }

            for (auto &inputStream : input->getStreams()) {
                auto ctxIt = std::find_if(this->_streams.begin(), this->_streams.end(), [&inputStream](StreamContext &streamContext) {
                    return (streamContext.stream == inputStream);
                });

                if (ctxIt != this->_streams.end()) {
                    DBG(("Removing input stream of '{}' for input: {}", ctxIt->stream->getStreamParameters().toString(), (void *) input.get()));

                    this->_unlink(*ctxIt);

                    this->_streams.erase(ctxIt);
                }
            }
        }

        GstClockTime getTimePosition() {
            GstClockTime ret = GST_CLOCK_TIME_NONE;

            if (this->useDecoded) {
                ret = this->peekPad(false)->getTimePosition();
            }

            if (ret == GST_CLOCK_TIME_NONE) {
                if (this->usePassthrough) {
                    ret = this->peekPad(true)->getTimePosition();
                }
            }

            return ret;
        }
        
        bool isEos() const {
            return this->eos;
        }

        GstClockTime getCurrentTime() const {
            GstClockTime ret;
            
            {
                auto tsDec  = this->decoded.getCurrentTime();
                auto tsPass = this->passthrough.getCurrentTime();

                ret = tsDec;
                if (ret == GST_CLOCK_TIME_NONE) {
                    ret = tsPass;

                } else {
                    if (tsPass != GST_CLOCK_TIME_NONE) {
                        if (tsPass > tsDec) {
                            ret = tsPass;
                        }
                    }
                }
            }

            return ret;
        }

        // Re-anchors this stream onto the channel timeline. The connectors call it just before
        // they make the stream the on-air source, which is exactly the moment a source that was
        // frozen off air has to rejoin: its own timeline stopped where it was paused, while the
        // channel kept running. Shifting the concat output pads by the difference keeps the
        // output monotonic without the source having to know anything about the channel.
        void synchronizeClock(GstClockTime runningTime) override {
            if (! GST_CLOCK_TIME_IS_VALID(runningTime)) {
                DBG(("Requested clock synchronization to an invalid running time - skipping"));

                return;
            }

            auto current = this->getCurrentTime();

            if (! GST_CLOCK_TIME_IS_VALID(current)) {
                DBG(("Playlist stream has not produced any buffer yet - nothing to synchronize"));

                return;
            }

            if (runningTime <= current) {
                // The branch already runs at or ahead of the channel. Shifting backwards would
                // emit decreasing timestamps, so leave it and let the selector drop the overlap.
                DBG(("Playlist stream is not behind the channel ({} >= {}) - skipping synchronization",
                    (guint64) current, (guint64) runningTime
                ));

                return;
            }

            {
                auto offset = runningTime - current;

                DBG(("Advancing playlist stream '{}' by {} ns to rejoin the channel at {} ns",
                    this->getStreamParameters().toString(), (guint64) offset, (guint64) runningTime
                ));

                this->decoded.applyTimeOffset(offset);
                this->passthrough.applyTimeOffset(offset);
            }
        }

        // Shifts this stream's output by a known amount. Unlike synchronizeClock, this asks no
        // questions about where the stream thinks it is: the caller has measured the gap and this
        // applies it. Audio and video are shifted by the same value from the same place, so they
        // cannot diverge the way two independent per-stream decisions can.
        void shiftTimeline(GstClockTime delta) {
            this->decoded.applyTimeOffset(delta);
            this->passthrough.applyTimeOffset(delta);
        }

        // Moves both paths so that the next item starts no earlier than `start` (running time)
        void startNoEarlierThan(GstClockTime start) {
            auto current = this->getCurrentTime();

            if (GST_CLOCK_TIME_IS_VALID(current) && GST_CLOCK_TIME_IS_VALID(start) && start > current) {
                this->decoded.applyTimeOffset(start - current);
                this->passthrough.applyTimeOffset(start - current);
            }
        }

        void synchronize(std::shared_ptr<PlaylistStream> &other) {
            auto tsCurrent = this->getCurrentTime();
            auto tsOther   = other->getCurrentTime();

            if (tsCurrent > tsOther) {
                auto diff = tsCurrent - tsOther;

                other->decoded.applyTimeOffset(diff);
                other->passthrough.applyTimeOffset(diff);

            } else {
                auto diff = tsOther - tsCurrent;

                this->decoded.applyTimeOffset(diff);
                this->passthrough.applyTimeOffset(diff);
            }
        }

    private:
        struct ConcatContext {
            pipeline::ElementPtr concat;
            pipeline::ElementPtr selector;
            pipeline::PadPtr     selectorPadNull;
            pipeline::PadPtr     selectorPadSrc;
            bool                 video;
            GstSegment           segment;
            GstClockTime         lastDuration = GST_CLOCK_TIME_NONE;
            guint                concatProbeId;

            // Offset applied since the last segment seen on the concat output. A pad offset reaches the
            // segment only when the pad pushes again, so an idle path (e.g. passthrough while decoded items
            // play) would otherwise report a stale time and be shifted again at the next item change.
            std::atomic<GstClockTime> unseenOffset { 0 };

            ~ConcatContext() {
                if (this->concat) {
                    if (this->concatProbeId) {
                        this->concat->getPad("src")->removeProbe(this->concatProbeId);
                    }
                }
            }

            void prepare(pipeline::BinPtr bin, bool video, bool passthrough) {
                this->video  = video;
                this->concat = bin->add(pipeline::Element::make(std::string("concat-") + (video ? "video" : "audio") + (passthrough ? "passthrough" : "decoded"), "concat"));
    
                gst_segment_init(&this->segment, GST_FORMAT_TIME);

                this->concatProbeId = this->concat->getPad("src")->addProbe(
                    { 
                        GST_PAD_PROBE_TYPE_BUFFER, 
                        GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                        GST_PAD_PROBE_TYPE_EVENT_FLUSH
                    },
                    [this](GstPadProbeInfo *info) {
                        auto infoType = GST_PAD_PROBE_INFO_TYPE(info);

                        if (infoType & GST_PAD_PROBE_TYPE_BUFFER) {
                            GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);

                            GstClockTime timestamp = GST_BUFFER_TIMESTAMP(buffer);
                            GstClockTime duration  = GST_BUFFER_DURATION(buffer);

                            if (timestamp == GST_CLOCK_TIME_NONE) {
                                timestamp = this->segment.position;
                            }

                            if (duration != GST_CLOCK_TIME_NONE) {
                                timestamp += duration;

                                this->lastDuration = duration;
                            }

                            this->segment.position = timestamp;


                        } else if (infoType & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
                            GstEvent    *event     = GST_PAD_PROBE_INFO_EVENT(info);
                            GstEventType eventType = GST_EVENT_TYPE(event);

                            if (eventType == GST_EVENT_SEGMENT) {
                                // The pad offset is already applied to this segment
                                gst_event_copy_segment(event, &this->segment);

                                this->unseenOffset = 0;

                            } else {
                                // TODO: handle FLUSH
                            }
                        }

                        return true;
                    }
                );

                // Concat sends an EOS event when no sink pad exists. We should filter it to prevent the pipeline from stopping.
                this->concat->getPad("src")->event(true, [video](GstEvent *event, bool &discard) {
                    if (GST_EVENT_TYPE(event) == GST_EVENT_EOS) {
                        DBG(("Received EOS on concant element (video = {}) - ignoring and filtering it", video));
    
                        discard = true;
                    }
    
                    return true;
                });
    
                {
                    auto nullSink = bin->add(pipeline::Element::make("fakesink"));
    
                    nullSink->setProperty<gboolean>("async", false);
    
                    this->selector = bin->add(pipeline::Element::make("output-selector"));
    
#if INFO_OVERLAY
                    if (video && ! passthrough) {
                        auto urlOverlay = bin->add(pipeline::Element::make("textoverlay", "textoverlay"));
    
                        if (urlOverlay) {
                            urlOverlay->setProperty<const gchar*>("font-desc", "Sans, 20");
                            urlOverlay->setProperty<gdouble>("xpos", 0.1);
                            urlOverlay->setProperty<gdouble>("ypos", 0.1);
                            urlOverlay->setProperty<gboolean>("shaded-background", true);

                            this->concat->link(urlOverlay);
                            urlOverlay->link(this->selector);

                        } else {
                            WARN(("'textoverlay' plugin is not available"));

                            this->concat->link(this->selector);
                        }
                    }
#else
                    this->concat->link(this->selector);
#endif
    
                    this->selectorPadNull = this->selector->requestPad("src_%u");
                    this->selectorPadSrc  = this->selector->requestPad("src_%u");
                    
                    this->selectorPadNull->link(nullSink->getPad("sink"));
                }
            }

            pipeline::PadPtr getSrcPad() {
                return this->selectorPadSrc;
            }

            pipeline::PadPtr requestPad() {
                return this->concat->requestPad("sink_%u");
            }

            void releasePad(pipeline::GstPadPtr pad) {
                this->concat->releasePad(pad);
            }

            void activate() {
                this->selector->setProperty<GstPad *>("active-pad", (*this->selectorPadSrc)->get());
            }

            void deactivate() {
                this->selector->setProperty<GstPad *>("active-pad", (*this->selectorPadNull)->get());
            }

            GstClockTime getCurrentTime() const {
                GstClockTime ret = gst_segment_to_running_time(&this->segment, GST_FORMAT_TIME, this->segment.position);

                if (GST_CLOCK_TIME_IS_VALID(ret)) {
                    ret += this->unseenOffset;
                }

                return ret;
            }

            void applyTimeOffset(GstClockTime offset) {
                auto pad = this->concat->getPad("src");

                this->unseenOffset += offset;

                pad->setTimeOffset(
                    pad->getTimeOffset() + offset
                );
            }
        };

        ConcatContext passthrough;
        ConcatContext decoded;

        bool useDecoded;
        bool usePassthrough;
        bool eos;

        std::function<void()> onEosCallback;

        std::vector<StreamContext> _streams;
};

/*
############################### Make-before-break switching ####################################

A forced switch used to stop the current item first and only then build the next item's decoders,
so the output repeated its last frame until the next item decoded, and a live item was rebased on
audio it had queued since it connected. The pending item is now armed instead: its decoders run
while the current item stays on air, and each of its output pads holds its first buffer. Once the
first video frame (the key) is held, the cut stops the current item, rebases the pending one on
that frame and releases the held buffers, so concat moves from one frame to the next without a
gap. Buffers older than the key on the other pads are dropped.
*/

static constexpr guint ARM_TIMEOUT_MS = 3000;

// Shared with the pad probes, which can outlive the Arm while an aborted item is torn down.
struct ArmGate {
    std::mutex              mutex;
    std::condition_variable cv;
    bool                    released = false;
    bool                    aborted  = false;
    bool                    keySeen  = false;
    GstClockTime            keyRt    = GST_CLOCK_TIME_NONE;

    // Arrival running time of the first buffer each stream released at the cut (audio, video)
    GstClockTime            releasedRt[2] = { GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE };

    // Trim (see _liveTrim) of each stream's first decoded buffer that plays, measured as it
    // leaves the decoder, before it waits in the hold queue (audio, video)
    GstClockTime            trim[2]    = { GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE };
    GstClockTime            decodedRt[2] = { GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE };

    // How much earlier than its timestamps the item plays: the tighter stream wins. Locked.
    GstClockTime itemTrim() const {
        GstClockTime ret = GST_CLOCK_TIME_NONE;

        for (auto value : this->trim) {
            if (GST_CLOCK_TIME_IS_VALID(value) && (! GST_CLOCK_TIME_IS_VALID(ret) || value < ret)) {
                ret = value;
            }
        }

        return GST_CLOCK_TIME_IS_VALID(ret) ? ret : 0;
    }

    // Schedules the cut. Called once, from the streaming thread that delivered the key.
    std::function<void()> onKey;
};

struct transcoder::PlaylistInput::Arm {
    PlaylistInput *owner = nullptr;

    std::shared_ptr<ArmGate>      gate;
    std::shared_ptr<LiveTimeline> timeline;

    std::shared_ptr<InputStream> audioStream;
    std::shared_ptr<InputStream> videoStream;

    std::vector<pipeline::PadPtr> pads;

    guint timeoutId = 0;

    // When the cut was first held back for the current item to reach the key (monotonic, µs)
    gint64 holdingSince = 0;

    // How much earlier than its timestamps the item plays, fixed at the cut
    GstClockTime trim = 0;

    // When the cut started waiting for the first audio that plays (monotonic, µs)
    gint64 audioWaitSince = 0;
};

// How much earlier than its timestamps the armed item plays. The held first frame has waited since
// it was decoded, so the margin is also kept against the clock now.
static GstClockTime _armTrim(const std::shared_ptr<ArmGate> &gate, const std::vector<pipeline::PadPtr> &pads, GstClockTime keyRt) {
    GstClockTime trim;

    {
        std::lock_guard gateLock(gate->mutex);

        trim = gate->itemTrim();
    }

    if (! pads.empty()) {
        trim = std::min(trim, _liveTrim(keyRt, _clockRunningTime((*pads.front())->get())));
    }

    return trim;
}

// The longest the current item is kept on air for the playlist to reach a live item's key
static constexpr gint64 CUT_HOLD_MAX_US = 1000 * G_TIME_SPAN_MILLISECOND;
static constexpr guint  CUT_HOLD_POLL_MS = 5;

// The longest the cut waits for a live item's first audio, to know how early the item can play
static constexpr gint64 CUT_AUDIO_WAIT_US = 300 * G_TIME_SPAN_MILLISECOND;

static bool _makeBeforeBreakEnabled() {
    const char *value = std::getenv("UTPL_PLAYLIST_MAKE_BEFORE_BREAK");

    return ! value || std::string(value) != "0";
}

static GstClockTime _bufferRunningTime(GstPad *pad, GstBuffer *buffer) {
    GstClockTime ret       = GST_CLOCK_TIME_NONE;
    GstClockTime timestamp = GST_BUFFER_PTS_IS_VALID(buffer) ? GST_BUFFER_PTS(buffer) : GST_BUFFER_DTS(buffer);

    if (GST_CLOCK_TIME_IS_VALID(timestamp)) {
        GstEvent *event = gst_pad_get_sticky_event(pad, GST_EVENT_SEGMENT, 0);

        if (event) {
            const GstSegment *segment;
            gst_event_parse_segment(event, &segment);

            if (segment->format == GST_FORMAT_TIME) {
                ret = gst_segment_to_running_time(segment, GST_FORMAT_TIME, timestamp);
            }

            gst_event_unref(event);
        }
    }

    return ret;
}

// A held streaming thread must give way when its item is being shut down: deactivating the
// decoder behind the pad needs the stream lock this thread holds. Element deactivation flags
// source pads as flushing before it waits for that lock, so walk from the pad to its targets.
static bool _padChainFlushing(GstPad *pad) {
    GstPad *current = GST_PAD(gst_object_ref(pad));

    for (int depth = 0; current && depth < 8; depth++) {
        if (GST_PAD_IS_FLUSHING(current)) {
            gst_object_unref(current);

            return true;
        }

        GstPad *next = GST_IS_GHOST_PAD(current) ? gst_ghost_pad_get_target(GST_GHOST_PAD(current)) : nullptr;

        gst_object_unref(current);

        current = next;
    }

    if (current) {
        gst_object_unref(current);
    }

    return false;
}

// On the hold queue's sink: how far the armed item's first played buffers arrive ahead of the clock
static void _measureArrival(const pipeline::PadPtr &holdPad, std::shared_ptr<ArmGate> gate, bool key, bool video) {
    struct Context {
        std::shared_ptr<ArmGate> gate;
        bool                     key;
        bool                     video;
    };

    GstElement *queue = gst_pad_get_parent_element((*holdPad)->get());

    if (! queue) {
        return;
    }

    GstPad *sinkPad = gst_element_get_static_pad(queue, "sink");

    gst_object_unref(queue);

    if (! sinkPad) {
        return;
    }

    gst_pad_add_probe(
        sinkPad,
        GST_PAD_PROBE_TYPE_BUFFER,
        [](GstPad *pad, GstPadProbeInfo *info, gpointer data) -> GstPadProbeReturn {
            auto *context = static_cast<Context *>(data);
            auto &gate    = *context->gate;

            GstClockTime runningTime = _bufferRunningTime(pad, GST_PAD_PROBE_INFO_BUFFER(info));

            if (! GST_CLOCK_TIME_IS_VALID(runningTime)) {
                return GST_PAD_PROBE_OK;
            }

            GstClockTime now = _clockRunningTime(pad);

            std::lock_guard lock(gate.mutex);

            if (! context->key) {
                // Only buffers the cut plays: from the key's first frame on
                GstClockTime keyRt = gate.decodedRt[! context->video];

                if (! GST_CLOCK_TIME_IS_VALID(keyRt) || runningTime < keyRt) {
                    return GST_PAD_PROBE_OK;
                }
            }

            gate.decodedRt[context->video] = runningTime;
            gate.trim[context->video]      = _liveTrim(runningTime, now);

            return GST_PAD_PROBE_REMOVE;
        },
        new Context { gate, key, video },
        [](gpointer data) {
            delete static_cast<Context *>(data);
        }
    );

    gst_object_unref(sinkPad);
}

static void _holdUntilCut(const pipeline::PadPtr &pad, std::shared_ptr<ArmGate> gate, bool key, bool video) {
    struct Context {
        std::shared_ptr<ArmGate> gate;
        bool                     key;
        bool                     video;
    };

    gst_pad_add_probe(
        (*pad)->get(),
        GST_PAD_PROBE_TYPE_BUFFER,
        [](GstPad *pad, GstPadProbeInfo *info, gpointer data) -> GstPadProbeReturn {
            auto *context = static_cast<Context *>(data);
            auto &gate    = *context->gate;

            GstClockTime runningTime = _bufferRunningTime(pad, GST_PAD_PROBE_INFO_BUFFER(info));

            std::function<void()> onKey;

            {
                std::lock_guard lock(gate.mutex);

                if (context->key && ! gate.keySeen) {
                    gate.keySeen = true;
                    gate.keyRt   = runningTime;
                    onKey        = gate.onKey;

                    gate.cv.notify_all();
                }
            }

            if (onKey) {
                onKey();
            }

            std::unique_lock lock(gate.mutex);

            while (true) {
                if (gate.aborted) {
                    return GST_PAD_PROBE_DROP;
                }

                if (
                    ! context->key && gate.keySeen &&
                    GST_CLOCK_TIME_IS_VALID(gate.keyRt) && GST_CLOCK_TIME_IS_VALID(runningTime) &&
                    runningTime < gate.keyRt
                ) {
                    // Queued before the first frame: playing it would put this pad ahead of video
                    return GST_PAD_PROBE_DROP;
                }

                if (gate.released) {
                    if (! GST_CLOCK_TIME_IS_VALID(gate.releasedRt[context->video])) {
                        gate.releasedRt[context->video] = runningTime;
                    }

                    return GST_PAD_PROBE_REMOVE;
                }

                if (_padChainFlushing(pad)) {
                    return GST_PAD_PROBE_DROP;
                }

                gate.cv.wait_for(lock, std::chrono::milliseconds(10));
            }
        },
        new Context { gate, key, video },
        [](gpointer data) {
            delete static_cast<Context *>(data);
        }
    );
}

/*
############################### PlaylistInput::PlaylistItem ####################################
*/

transcoder::PlaylistInput::PlaylistItem::~PlaylistItem() {
    if (this->positionWatchId > 0) {
        g_source_remove(this->positionWatchId);
    }

    if (this->input) {
        auto node = this->input->getNode();

        if (node) {
            auto bin = this->parent->_bin;

            if (bin->contains(node->getName(), false)) {
                node->lockState(true);

                node->null([bin, node](bool) {
                    bin->remove(node);

                    node->lockState(false);
                });
            }
        }

        this->input->cleanup();
        this->input = nullptr;
    }
}

void transcoder::PlaylistInput::PlaylistItem::probe(Input::ProbeCallback probeCallback) {
    auto node = this->input->getNode();

    node->lockState(true);
    
    this->parent->_bin->add(node);
    
    this->input->probe([this, node, probeCallback](bool success) {
        if (success) {
            node->lockState(false);

            this->duration        = this->input->getDuration();
            // Twice a second, not once: at one-second granularity the check can land up to a
            // second late, which was a meaningful part of the old two-second warning.
            this->positionWatchId = g_timeout_add(500, G_SOURCE_FUNC(onPositionUpdate), this);
            this->ready           = true;
        }

        probeCallback(success);
    });
}

void transcoder::PlaylistInput::PlaylistItem::cleanup() {
    auto node = this->input->getNode();

    if (this->parent->_bin->contains(node->getName(), false)) {
        node->lockState(true);

        node->null();
	
        this->parent->_bin->remove(node);

        node->lockState(false);
    }
}

bool transcoder::PlaylistInput::PlaylistItem::onPositionUpdate(PlaylistItem *self) {
    if (self->duration == GST_CLOCK_TIME_NONE) {
        WARN(("Current input duration is unknown!"));

        return true;
    }

    for (auto &stream : self->parent->_streams) {
        auto playlistStream = std::reinterpret_pointer_cast<PlaylistStream>(stream);

        auto streamPosition = playlistStream->getTimePosition();

        if (streamPosition != GST_CLOCK_TIME_NONE) {
            if (self->duration - streamPosition < SWITCH_THRESHOLD) {
                self->positionWatchId = 0;

                self->aboutToFinishCallback();

                return false;
            }
        }
    }

    return true;
}

transcoder::PlaylistInput::PlaylistInput() {
}

transcoder::PlaylistInput::~PlaylistInput() {
    TRACE(("CALL"));

    this->cleanup();
}


void transcoder::PlaylistInput::prepare(const InputParameters &parameters) {
    this->cleanup();

    this->_inputParameters = parameters;
    this->_bin             = pipeline::Bin::create("playlist-input-" + std::to_string(utils::AutoGeneratedId().getInt()));
}

void transcoder::PlaylistInput::probe(ProbeCallback callback) {
    bool probed = true;

    const auto &plsUri = this->_inputParameters.getUris()[0];

    {
        utpl::playlist::Parser parser;

        this->_asyncSwitchToNextIsForced = false;

        {
            Protocol subprotocol;
            std::string path;

            utpl::parseProtocolAndPath(plsUri, subprotocol, path);

            switch (subprotocol) {
                case Protocol::FILE:
                case Protocol::NONE:
                    {
                        std::ifstream fileStream(path);

                        if (fileStream.is_open()) {                          
                            this->_playlistItemsInputParameters = parser.parse(fileStream);

                        } else {
                            logger.error("Unable to open PLS file at '{}'", plsUri);

                            probed = false;
                        }
                    }
                    break;

                default:
                    {
                        logger.error("Only file procol is currently supported! '{}'", plsUri);

                        probed = false;
                    }
                    break;
            }
        }
    }

    // Validate all streams
    if (probed) {
        size_t validItems   = 0;
        size_t itemPosition = 0;

        for (auto &p : this->_playlistItemsInputParameters) {
            const auto &uri = p.getUris()[0];

            {
                Protocol subprotocol;

                if (utpl::parseProtocol(uri, subprotocol) == Protocol::UNKNOWN) {
                    logger.warning("Playlist item at position {} is not valid. URI: '{}'", itemPosition, uri);

                } else {
                    // TODO: Filter not supported protocols
                    validItems++;
                }
            }

            this->_playlistItemIds.push_back(utils::AutoGeneratedId().getString());

            itemPosition++;
        }

        if (validItems == 0) {
            logger.error("Playlist '{}' does not contain any valid playback items", plsUri);

            probed = false;
        }
    }

    if (probed) {
        this->_audioStream = std::make_shared<PlaylistStream>(this->_bin, false, [this]() {
            DBG(("Audio EOS"));

            this->_onEos();
        });

        this->_videoStream = std::make_shared<PlaylistStream>(this->_bin, true, [this]() {
            DBG(("Video EOS"));

            this->_onEos();
        });

        // Kept through the unlink and link of a change of item: one flow for the whole playlist.
        this->_audioStream->setMxlTap(this->_inputParameters.getMxlTap(), true);
        this->_videoStream->setMxlTap(this->_inputParameters.getMxlTap(), true);

        this->_streams.push_back(std::reinterpret_pointer_cast<InputStream>(this->_audioStream));
        this->_streams.push_back(std::reinterpret_pointer_cast<InputStream>(this->_videoStream));

        this->_playlistItemIndex = -1;

        this->_bin->onPlay([this]() {
            this->_switchToNextItemAsync();
    
            return false;
        });
    }

    callback(probed);
}

void transcoder::PlaylistInput::_asyncSwitchNext(PlaylistInput *self) {
    bool forced = self->_asyncSwitchToNextIsForced;

    self->_asyncSwitchToNextIsForced = false;

    self->_switchToNextItem(forced);
}

void transcoder::PlaylistInput::_asyncCleanup(PlaylistInput *self) {
    std::unique_lock lock(self->_mutex);

    for (auto input : self->_toCleanup) {
        input->getNode()->null();
        input->cleanup();
    }

    self->_toCleanup.clear();
}

void transcoder::PlaylistInput::_onEos() {
    if (
        (
            this->_audioStream->isEos() ||
            this->_audioStream->getStreamParameters().getCodec() == Codec::UNKNOWN ||
            ! this->_audioStream->isLinked()
        ) && 
        (
            this->_videoStream->isEos() ||
            this->_videoStream->getStreamParameters().getCodec() == Codec::UNKNOWN ||
            ! this->_videoStream->isLinked()
        )
    ) {
        this->_currentItem->eos = true;

        if (this->_arm) {
            DBG(("Have an EOS on both streams while the next item is armed - the cut will replace it"));

            return;
        }

        DBG(("Have an EOS on both streams - switching to a next position on playlist"));

        this->_switchToNextItem();
    }
}

void transcoder::PlaylistInput::cleanup() {
    // A frozen input holds a locked state, so the pipeline going to NULL would leave it behind
    // and it would be disposed while still PAUSED. Release it before anything else is torn down.
    if (! this->_playbackActive) {
        this->_playbackActive = true;

        this->_suspendPositionWatch();

        if (this->_bin) {
            this->_bin->lockState(false);
            this->_bin->null();
        }
    }

    if (this->_arm) {
        if (this->_arm->timeoutId) {
            g_source_remove(this->_arm->timeoutId);
        }

        {
            std::lock_guard lock(this->_arm->gate->mutex);

            this->_arm->gate->aborted = true;
        }

        this->_arm->gate->cv.notify_all();

        this->_arm = nullptr;
    }

    this->_probeCallback   = nullptr;
    this->_inputParameters = {};

    this->_playlistItemsInputParameters.clear();
    this->_streams.clear();

    this->_audioStream = nullptr;
    this->_videoStream = nullptr;
}

const std::vector<std::shared_ptr<transcoder::InputStream>> &transcoder::PlaylistInput::getStreams() const {
    return this->_streams;
}

const transcoder::InputParameters &transcoder::PlaylistInput::getInputParameters() const {
    return this->_inputParameters;
}

void transcoder::PlaylistInput::onScteData(ScteDataCallback callback) {
    std::unique_lock lock(this->_mutex);

    this->_scteCallback = callback;
}

void transcoder::PlaylistInput::onStateChanged(StateChangedCallback callback) {
    std::unique_lock lock(this->_mutex);

    this->_stateChangeCallback = callback;
}

void transcoder::PlaylistInput::onStreamsChanged(StreamsChangedCallback callback) {
    this->_streamsChangedCallback = callback;
}

void transcoder::PlaylistInput::onError(ErrorCallback callback) {
    // TODO:
}

pipeline::ElementPtr transcoder::PlaylistInput::getNode() {
    return this->_bin;
}

bool transcoder::PlaylistInput::isLive() const {
    // Cannot determine this, as the HTTP protocol may indicate either a VOD file or HLS stream. Assumed to be VOD by default.
    return false;
}

const std::string &transcoder::PlaylistInput::getId() const {
    return this->_id.getString();
}

transcoder::InputStatus transcoder::PlaylistInput::getStatus() {
    transcoder::InputStatus ret;
    
    {
        std::unique_lock lock(this->_mutex);

        for (int i = 0; i < this->_playlistItemsInputParameters.size(); i++) {
            const auto &id = this->_playlistItemIds[i];

            {
                auto state = InputState::UNKNOWN;

                if (this->_currentItem && this->_currentItem->playlistItemIndex == i) {
                    auto  inputStatus     = this->_currentItem->input->getStatus();
                    auto &transportStatus = inputStatus.getTransportsStatus();

                    if (! transportStatus.empty()) {
                        state = transportStatus[0].getState();
                    }

                } else if (this->_pendingItem && this->_pendingItem->playlistItemIndex == i) {
                    auto  inputStatus     = this->_pendingItem->input->getStatus();
                    auto &transportStatus = inputStatus.getTransportsStatus();

                    if (! transportStatus.empty()) {
                        state = transportStatus[0].getState();
                    }
                }

                ret.addTransport(
                    id,
                    this->_playlistItemsInputParameters[i].getUris()[0],
                    state
                );
            }

            if (i == this->_playlistItemIndex) {
                ret.setCurrentTransportId(id);
            }
        }
    }

    return ret;
}

Error transcoder::PlaylistInput::setCurrentStream(const std::string &streamId) {
    Error ret = Error::NO_ERROR;

    if (streamId.empty()) {
        logger.warning("Empty stream ID provided - ignoring the request");
        ret = Error::INVALID_ARGUMENT;

    } else {
        std::unique_lock lock(this->_mutex);

        {
            size_t targetIndex = 0;
            bool found = false;
            
            for (const auto &id : this->_playlistItemIds) {
                if (id == streamId) {
                    found = true;
                    break;
                }
                targetIndex++;
            }

            if (! found) {
                logger.warning("Stream with ID {} not found", streamId);

                ret = Error::INVALID_ARGUMENT;

            } else if (targetIndex == this->_playlistItemIndex) {
                // The index only moves at the cut, so a request for the item on air can arrive
                // while a switch away from it is still connecting or armed: cancel that switch.
                if (this->_pendingItem && this->_currentItem && ! this->_currentItem->eos && ! this->_currentItem->error) {
                    logger.log("Stream {} is already active - cancelling the pending switch", streamId);

                    if (this->_arm) {
                        this->_abortArm("the item on air was requested again");

                    } else {
                        this->_pendingItem = nullptr;
                    }

                } else {
                    logger.log("Stream {} is already active", streamId);
                }

            } else {
                logger.log("Switching to stream {}", streamId);

                this->_switchToItem(targetIndex, true);
            }
        }
    }

    return ret;
}

Error transcoder::PlaylistInput::applyPlaylistSnapshot(
    const std::vector<PlaylistSnapshotItem> &items,
    uint64_t revision
) {
    if (items.empty()) {
        logger.warning("Refusing an empty managed playlist snapshot");
        return Error::INVALID_STATE;
    }

    std::vector<InputParameters> parsed;
    std::vector<std::string> ids;
    std::set<std::string> seen;

    for (const auto &item : items) {
        if (item.id.empty() || item.uri.empty() || ! seen.insert(item.id).second) {
            logger.warning("Managed playlist snapshot contains an empty or duplicate item ID");
            return Error::INVALID_ARGUMENT;
        }

        // Nested playlists are intentionally not queue entries. Parse every other entry by
        // the same M3U8 parser used for the configured seed playlist.
        if (item.uri.find(".m3u") != std::string::npos || item.uri.rfind("pls+", 0) == 0) {
            logger.warning("Refusing nested playlist URI '{}'", item.uri);
            return Error::INVALID_ARGUMENT;
        }

        try {
            std::ostringstream playlist;
            playlist << "#EXTM3U\n#EXTINF:-1," << item.attributes << "\n" << item.uri << "\n";
            std::istringstream input(playlist.str());
            utpl::playlist::Parser parser;
            auto entries = parser.parse(input);
            if (entries.size() != 1 || entries[0].getUris().empty()) {
                return Error::INVALID_ARGUMENT;
            }

            Protocol protocol;
            if (utpl::parseProtocol(entries[0].getUris()[0], protocol) == Protocol::UNKNOWN) {
                return Error::INVALID_ARGUMENT;
            }

            parsed.push_back(std::move(entries[0]));
            ids.push_back(item.id);
        } catch (const std::exception &error) {
            logger.warning("Invalid managed playlist item '{}': {}", item.uri, error.what());
            return Error::INVALID_ARGUMENT;
        }
    }

    std::unique_lock lock(this->_mutex);
    if (revision < this->_playlistRevision) {
        return Error::INVALID_STATE;
    }

    const bool allowInitialUriRebind = this->_playlistRevision == 0;
    auto findTarget = [&ids, &parsed, allowInitialUriRebind](
        const std::string &id, const std::string &uri
    ) -> int {
        for (size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] == id && parsed[i].getUris()[0] == uri) {
                return static_cast<int>(i);
            }
        }
        // Initial managed takeover rebinds seed M3U8 identities by URI. This only happens
        // before the managed IDs have reached the engine.
        if (allowInitialUriRebind) {
            for (size_t i = 0; i < parsed.size(); ++i) {
                if (parsed[i].getUris()[0] == uri) {
                    return static_cast<int>(i);
                }
            }
        }
        return -1;
    };

    int currentTarget = -1;
    if (this->_currentItem) {
        const auto currentIndex = this->_currentItem->playlistItemIndex;
        if (currentIndex >= this->_playlistItemsInputParameters.size()) {
            return Error::INVALID_STATE;
        }
        currentTarget = findTarget(
            this->_playlistItemIds[currentIndex],
            this->_playlistItemsInputParameters[currentIndex].getUris()[0]
        );
        if (currentTarget < 0) {
            logger.warning("Refusing to remove the item currently on air");
            return Error::INVALID_STATE;
        }
    }

    int pendingTarget = -1;
    if (this->_pendingItem) {
        const auto pendingIndex = this->_pendingItem->playlistItemIndex;
        if (pendingIndex < this->_playlistItemsInputParameters.size()) {
            pendingTarget = findTarget(
                this->_playlistItemIds[pendingIndex],
                this->_playlistItemsInputParameters[pendingIndex].getUris()[0]
            );
        }
    }

    this->_playlistItemsInputParameters = std::move(parsed);
    this->_playlistItemIds = std::move(ids);
    this->_playlistRevision = revision;

    if (this->_currentItem) {
        this->_currentItem->playlistItemIndex = static_cast<size_t>(currentTarget);
        this->_playlistItemIndex = static_cast<size_t>(currentTarget);
    }

    // A removed or moved preloaded item must never be allowed to air. Tear it down and
    // prepare the new next entry on the GLib loop after releasing this mutation.
    const int expectedPending = currentTarget >= 0
        ? (currentTarget + 1) % static_cast<int>(this->_playlistItemsInputParameters.size())
        : -1;
    if (this->_pendingItem && pendingTarget != expectedPending) {
        if (this->_arm) {
            this->_abortArm("the managed playlist moved or removed it");

        } else {
            this->_pendingItem->cleanup();
            this->_pendingItem = nullptr;
        }

        this->_switchToNextItemAsync(false);
    } else if (this->_pendingItem) {
        this->_pendingItem->playlistItemIndex = static_cast<size_t>(pendingTarget);
    }

    return Error::NO_ERROR;
}

void transcoder::PlaylistInput::_suspendPositionWatch() {
    if (this->_currentItem && this->_currentItem->positionWatchId > 0) {
        g_source_remove(this->_currentItem->positionWatchId);

        this->_currentItem->positionWatchId = 0;
    }
}

void transcoder::PlaylistInput::_resumePositionWatch() {
    if (this->_currentItem && this->_currentItem->positionWatchId == 0) {
        // Same cadence as the watch armed when an item is prepared: an input coming back on air
        // may already be inside the switch window, and this is what notices.
        this->_currentItem->positionWatchId = g_timeout_add(
            500, G_SOURCE_FUNC(PlaylistItem::onPositionUpdate), this->_currentItem.get()
        );
    }
}

GstClockTime transcoder::PlaylistInput::_runningTime() const {
    GstClockTime ret = GST_CLOCK_TIME_NONE;

    if (this->_bin) {
        auto element = GST_ELEMENT_CAST((*this->_bin)->get());
        auto clock   = gst_element_get_clock(element);

        if (clock) {
            GstClockTime now      = gst_clock_get_time(clock);
            GstClockTime baseTime = gst_element_get_base_time(element);

            ret = now > baseTime ? now - baseTime : 0;

            gst_object_unref(clock);
        }
    }

    return ret;
}

Error transcoder::PlaylistInput::setPlaybackActive(bool active) {
    std::unique_lock lock(this->_mutex);

    if (! this->_bin) {
        logger.warning("Playback state change requested before the input was prepared");

        return Error::INVALID_STATE;
    }

    if (this->_playbackActive == active) {
        return Error::NO_ERROR;
    }

    // A switch that is halfway through committing owns the graph. Freezing it here would leave
    // the pending branch half-linked, so refuse and let the caller retry.
    if (this->_pendingItem && this->_pendingItem->ready && ! active) {
        logger.warning("Refusing to freeze the playlist while a pending item is being committed");

        return Error::INVALID_STATE;
    }

    this->_playbackActive = active;

    if (active) {
        logger.log("Resuming playlist input '{}'", this->_bin->getName());

        // Unlock first: the bin has to follow the pipeline again, otherwise it would stay behind
        // on the next global state change, including shutdown. The current item is unlocked the
        // same way, since freezing locked it explicitly.
        if (this->_currentItem && this->_currentItem->input) {
            auto node = this->_currentItem->input->getNode();

            if (node) {
                node->lockState(false);
            }
        }

        this->_bin->lockState(false);
        this->_bin->syncWithParent();

        if (this->_currentItem && this->_currentItem->input) {
            auto node = this->_currentItem->input->getNode();

            if (node) {
                node->syncWithParent();
            }
        }

        // Rejoin the channel timeline before anything downstream looks at this branch.
        //
        // The connectors also call synchronizeClock() when they put a stream on air, but that
        // decides per stream by comparing the stream's own last timestamp against the channel.
        // Once a pad offset has been applied, that comparison stops meaning "is this branch
        // behind": a branch frozen for 15 s was measured as 0.07 s *ahead* and skipped, while its
        // audio was shifted by 13.45 s. The result was a break with sound and no picture, because
        // the video was left on a timeline the compositor had already passed.
        //
        // The freeze duration has none of that ambiguity, and both streams take the same value.
        {
            auto now = this->_runningTime();

            if (GST_CLOCK_TIME_IS_VALID(now) && GST_CLOCK_TIME_IS_VALID(this->_frozenAtRunningTime)
                    && now > this->_frozenAtRunningTime) {
                auto frozenFor = now - this->_frozenAtRunningTime;

                logger.log("Playlist input '{}' was frozen for {} ms - shifting its timeline to rejoin the channel",
                    this->_bin->getName(), (long) (frozenFor / GST_MSECOND)
                );

                if (this->_videoStream) {
                    this->_videoStream->shiftTimeline(frozenFor);
                }

                if (this->_audioStream) {
                    this->_audioStream->shiftTimeline(frozenFor);
                }
            }

            this->_frozenAtRunningTime = GST_CLOCK_TIME_NONE;
        }

        this->_resumePositionWatch();

        {
            GstState binState   = GST_STATE_VOID_PENDING;
            GstState binPending = GST_STATE_VOID_PENDING;
            GstState itemState  = GST_STATE_VOID_PENDING;

            gst_element_get_state((*this->_bin)->get(), &binState, &binPending, 0);

            if (this->_currentItem && this->_currentItem->input) {
                auto node = this->_currentItem->input->getNode();

                if (node) {
                    gst_element_get_state((*node)->get(), &itemState, nullptr, 0);
                }
            }

            // videoPos is reported for continuity with earlier logs, but it is not evidence:
            // gst_pad_query_position returns NONE on this pad in several states and prints as a
            // huge number. The element states are the reliable part of this line.
            logger.log("Playlist input '{}' resumed: bin {} (pending {}), item {}, videoPos {} ms",
                this->_bin->getName(),
                gst_element_state_get_name(binState), gst_element_state_get_name(binPending),
                gst_element_state_get_name(itemState),
                this->_videoStream ? (long) (this->_videoStream->getTimePosition() / GST_MSECOND) : -1L
            );
        }

    } else {
        logger.log("Freezing playlist input '{}'", this->_bin->getName());

        // Taken before the bin is paused, while the branch and the channel still share a running
        // time. The difference against the value read on resume is how far this branch has fallen
        // behind, which is what its output has to be shifted by to rejoin the channel.
        this->_frozenAtRunningTime = this->_runningTime();

        // The position watch measures how close the current item is to its end. A frozen item
        // makes no progress, so leave the timer off until playback resumes.
        this->_suspendPositionWatch();

        this->_bin->lockState(true);
        this->_bin->pause();

        // The current item is a bin of its own inside this one, and it was added with its state
        // locked while it was probed. A child whose state is still locked ignores the parent's
        // transition, so pause it explicitly rather than trusting propagation.
        if (this->_currentItem && this->_currentItem->input) {
            auto node = this->_currentItem->input->getNode();

            if (node) {
                node->lockState(true);
                node->pause();
            }
        }

        {
            GstState state     = GST_STATE_VOID_PENDING;
            GstState pending   = GST_STATE_VOID_PENDING;
            GstState itemState = GST_STATE_VOID_PENDING;

            gst_element_get_state((*this->_bin)->get(), &state, &pending, 0);

            if (this->_currentItem && this->_currentItem->input) {
                auto node = this->_currentItem->input->getNode();

                if (node) {
                    gst_element_get_state((*node)->get(), &itemState, nullptr, 0);
                }
            }

            logger.log("Playlist input '{}' froze: bin {} (pending {}), item {}, videoPos {} ms",
                this->_bin->getName(),
                gst_element_state_get_name(state), gst_element_state_get_name(pending),
                gst_element_state_get_name(itemState),
                this->_videoStream ? (long) (this->_videoStream->getTimePosition() / GST_MSECOND) : -1L
            );
        }
    }

    return Error::NO_ERROR;
}

Protocol utpl::transcoder::PlaylistInput::getProtocol() const {
    return Protocol::PLAYLIST;
}

std::string transcoder::PlaylistInput::getName() const {
    if (this->_bin) {
        return this->_bin->getName();
    }

    return std::string();
}

GstClockTime transcoder::PlaylistInput::getDuration() const {
    return GST_CLOCK_TIME_NONE;
}

void transcoder::PlaylistInput::_switchToNextItemAsync(bool force) {
    this->_asyncSwitchToNextIsForced = force;

    g_idle_add(G_SOURCE_FUNC(_asyncSwitchNext), this);
}

void transcoder::PlaylistInput::_switchToNextItem(bool force) {
    std::unique_lock lock(this->_mutex);

    auto nextIndex = this->_playlistItemIndex;
    if (nextIndex < 0) {
        nextIndex = 0;

    } else {
        nextIndex = (nextIndex + 1) % this->_playlistItemsInputParameters.size();
    }

    this->_switchToItem(nextIndex, force);
}

void transcoder::PlaylistInput::_switchToItem(size_t itemNo, bool force) {
    std::unique_lock lock(this->_mutex);

    if (
        // just a one item in playlist
        itemNo != this->_playlistItemIndex ||
        (
            (itemNo == 0) || 
            (this->_playlistItemsInputParameters.size() == 1)
        ) ||
        // 2 streams and pending has error
        (
            this->_playlistItemsInputParameters.size() == 2 &&
            this->_pendingItem && 
            this->_pendingItem->error
        )
    ) {
        DBG(("CALL, switching from item {} to item {}", this->_playlistItemIndex, itemNo));

        if (this->_pendingItem) {
            if (this->_pendingItem->error) {
                DBG(("Releasing the pending input due to an error"));

                if (this->_arm) {
                    this->_abortArm("the item reported an error");

                } else {
                    this->_pendingItem->cleanup();

                    this->_pendingItem = nullptr;
                }
            }
        }

        if (this->_currentItem) {
            if (this->_currentItem->eos || this->_currentItem->error) {
                auto input = this->_currentItem->input; 
                auto node  = input->getNode();

                DBG(("Current input is EOF - disposing the input"));

                this->_audioStream->removeStreamsByInput(input);
                this->_videoStream->removeStreamsByInput(input);

                this->_bin->remove(node);

                {
                    this->_toCleanup.push_back(input);

                    g_idle_add(G_SOURCE_FUNC(_asyncCleanup), this);

                    this->_currentItem->input = nullptr;
                }

                this->_currentItem = nullptr;
            }
        }

        if (this->_pendingItem) {
            auto pendingIndex = this->_pendingItem->playlistItemIndex;

            DBG(("Pending element is set for index {}", pendingIndex));

            if (pendingIndex == itemNo) {
                DBG(("Pending element has the same playlist index as desired, skipping creating new item"));

                if (this->_arm) {
                    DBG(("Pending element is already armed - the cut follows its first frame"));

                    return;
                }

                // Probe completion can happen before the current clip reaches
                // EOS. In that case _commitPending deliberately postpones the
                // switch. When EOS (or a forced Skip) later selects this same
                // pending item, commit the already-ready branch instead of
                // leaving it detached while only advancing status identity.
                this->_pendingItem->forced = this->_pendingItem->forced || force;
                if (this->_pendingItem->ready &&
                        (this->_pendingItem->forced || !this->_currentItem || this->_currentItem->eos)) {
                    this->_commitPending(this->_pendingItem->forced);
                    return;
                }

            } else {
                DBG(("Current pending element has different playlist index - destroying it"));

                if (this->_arm) {
                    this->_abortArm("another item was requested");

                } else {
                    this->_pendingItem = nullptr;
                }
            }
        }

        if (! this->_pendingItem) {
            auto inputName = this->_bin->getName() + "-src-" + std::to_string(itemNo);
            auto input     = std::make_shared<SingleInput>(true, inputName);
            
            {
                InputParameters params = this->_inputParameters;

                params.clearUris();

                // The tap belongs to the playlist's own streams, which carry every item in turn.
                // An item's stream would take the flow for as long as it lasted.
                params.setMxlTap(MxlTapParameters());

                for (auto &u : this->_playlistItemsInputParameters[itemNo].getUris()) {
                    params.addUri(u);
                }
                
                for (auto &p : this->_playlistItemsInputParameters[itemNo].getAllTransportParameters()) {
                    params.mergeTransportParameters(*p.second);
                }

                input->prepare(params);

                input->onScteData([input, this](const Scte35Data &scteData) {
                    std::unique_lock lock(this->_mutex);

                    if (this->_currentItem && (this->_currentItem->input == input)) {
                        if (this->_scteCallback) {
                            this->_scteCallback(scteData);
                        }
                    }
                });

                input->onError([input, this](const std::string &message) {
                    WARN(("Received error from playlist input: '{}'", message));

                    if (this->_pendingItem && input == this->_pendingItem->input) {
                        if (! this->_pendingItem->error) {
                            this->_pendingItem->error = true;
                            this->_playlistItemIndex  = this->_pendingItem->playlistItemIndex;

                            this->_switchToNextItemAsync(this->_pendingItem->forced);
                        }

                    } else if (this->_currentItem && input == this->_currentItem->input) {
                        if (! this->_currentItem->error) {
                            this->_currentItem->error = true;

                            this->_switchToNextItemAsync(true);
                        }
                    }

                    return true;
                });

                if (input->isLive()) {
                    // When stopping the transcoder while playing a live stream, switchToNextItem is executed, 
                    // which causes an internal deadlock.
                    input->getNode()->onPause([this, input]() {
                        std::unique_lock lock(this->_mutex);

                        if (this->_currentItem && (this->_currentItem->input == input)) {
                            this->_currentItem->paused = true;

                        } else if (this->_pendingItem && (this->_pendingItem->input == input)) {
                            this->_pendingItem->paused = true;
                        }

                        return true;
                    });

                    input->onStateChanged([this, input](InputState state) {
                        std::unique_lock lock(this->_mutex);
                        
                        if (this->_currentItem && (this->_currentItem->input == input)) {
                            // An armed next item replaces the starving one at its first frame
                            if (state == InputState::BUFFERING && ! this->_currentItem->paused && ! this->_arm) {
                                logger.log("Buffer underrun, switching to next stream");

                                this->_switchToNextItem(true);
                            }
                        }
                    });
                }

                switch (input->getProtocol()) {
                    case Protocol::UDP:
                    case Protocol::SRT:
                    case Protocol::RTMP:
                    case Protocol::FILE:
                        input->getNode()->setProperty<gboolean>("async-handling", true);
                        break;
        
                    default:
                        break;
                }
            }
            
            this->_pendingItem = std::make_unique<PlaylistItem>(
                this, 
                input,
                itemNo,
                [this]() {
                    DBG(("Current playlist item is about to finish"));
                }
            );

            this->_pendingItem->forced = force;

            this->_pendingItem->probe([this](bool success) {
                DBG(("Probe callback for input '{}' returned {}", this->_pendingItem->input->getName(), success));

                if (success) {
                    this->_commitPending(this->_pendingItem->forced);
                    
                } else {
                    this->_playlistItemIndex = this->_pendingItem->playlistItemIndex;

                    this->_switchToNextItem();
                }
            });
        }

    } else {
        WARN(("Requested switch to same index {} - ignoring", this->_playlistItemIndex));
    }
}

void transcoder::PlaylistInput::_commitPending(bool force) {
    std::unique_lock lock(this->_mutex);

    DBG(("Commiting pending element!"));

    if (this->_arm) {
        DBG(("Pending element is already armed - the cut follows its first frame"));

        return;
    }

    {
        bool doCommit = true;

        if (! force && this->_currentItem) {
            if (! this->_currentItem->eos) {
                DBG(("Postponing commiting pending element - it should happen when current will reach EOF"));

                doCommit = false;
            }        
        }
        
        if (doCommit && force) {
            std::shared_ptr<InputStream> newAudioStream;
            std::shared_ptr<InputStream> newVideoStream;

            for (auto &stream : this->_pendingItem->input->getStreams()) {
                if (! newAudioStream && stream->getStreamParameters().hasAudioParameters()) {
                    newAudioStream = stream;
                }

                if (! newVideoStream && stream->getStreamParameters().hasVideoParameters()) {
                    newVideoStream = stream;
                }
            }

            if (this->_canArmPending(newAudioStream, newVideoStream)) {
                this->_armPending(newAudioStream, newVideoStream);

                doCommit = false;
            }
        }

        if (doCommit) {
            std::shared_ptr<InputStream> newAudioStream;
            std::shared_ptr<InputStream> newVideoStream;

            // Stop current
            if (this->_currentItem) {
                auto &item = this->_currentItem;
                auto  node = item->input->getNode();

                item->input->onError({});
                item->input->onStateChanged({});
                item->input->onStreamsChanged({});
                item->input->onScteData({});

                node->null();
            }

            this->_audioStream->synchronize(this->_videoStream);

            // Add new
            {
                auto &input = this->_pendingItem->input;

                for (auto &stream : input->getStreams()) {
                    if (! newAudioStream && stream->getStreamParameters().hasAudioParameters()) {
                        newAudioStream = stream;
                    }
                    
                    if (! newVideoStream && stream->getStreamParameters().hasVideoParameters()) {
                        newVideoStream = stream;
                    }
                }

                auto timeline = input->isLive() ? std::make_shared<LiveTimeline>() : nullptr;

                if (timeline) {
                    // Audio and video were aligned by synchronize() above; either gives the base.
                    GstClockTime base = this->_videoStream->getCurrentTime();

                    if (! GST_CLOCK_TIME_IS_VALID(base)) {
                        base = this->_audioStream->getCurrentTime();
                    }

                    timeline->base        = GST_CLOCK_TIME_IS_VALID(base) ? base : 0;
                    timeline->expectVideo = newVideoStream != nullptr;
                }

                if (newAudioStream) {
                    this->_audioStream->addInputStream(input, newAudioStream, timeline);
                }

                if (newVideoStream) {
                    this->_videoStream->addInputStream(input, newVideoStream, timeline);
                }
            }

            this->_audioStream->unlink();
            this->_videoStream->unlink();

            // Update stream parameters
            this->_audioStream->update(newAudioStream ? newAudioStream->getStreamParameters() : StreamParameters());
            this->_videoStream->update(newVideoStream ? newVideoStream->getStreamParameters() : StreamParameters());

            if (this->_streamsChangedCallback) {
                this->_streamsChangedCallback(this->_streams);
            }

            this->_audioStream->link();
            this->_videoStream->link();

            // Remove old
            if (this->_currentItem) {
                auto &input = this->_currentItem->input;

                this->_audioStream->removeStreamsByInput(input);
                this->_videoStream->removeStreamsByInput(input);

                this->_bin->remove(input->getNode());
            }

            // The legacy commit keeps the output on the connectors' filler while the channel relinks and the
            // new item starts, and the filler runs on the clock. On a clock-paced (live) output the playlist
            // position is then behind the clock, so the new item, stamped from the previous item's end, would
            // overlap what the filler already played. Start it after the filler instead (one frame of margin).
            if (! this->_pendingItem->input->isLive()) {
                GstClockTime now = GST_CLOCK_TIME_NONE;

                if (GstClock *clock = gst_element_get_clock((*this->_bin)->get())) {
                    GstClockTime time = gst_clock_get_time(clock);
                    GstClockTime base = gst_element_get_base_time((*this->_bin)->get());

                    if (GST_CLOCK_TIME_IS_VALID(time) && GST_CLOCK_TIME_IS_VALID(base) && time >= base) {
                        now = time - base;
                    }

                    gst_object_unref(clock);
                }

                GstClockTime position = this->_videoStream->getCurrentTime();

                if (! GST_CLOCK_TIME_IS_VALID(position)) {
                    position = this->_audioStream->getCurrentTime();
                }

                if (GST_CLOCK_TIME_IS_VALID(now) && GST_CLOCK_TIME_IS_VALID(position) && now + PLAYLIST_FILLER_MARGIN > position) {
                    GstClockTime start = now + PLAYLIST_FILLER_MARGIN;

                    logger.log("Playlist item starts {} ms after the previous item's end, after the filler played during the change",
                        (gint64) ((start - position) / GST_MSECOND));

                    this->_audioStream->startNoEarlierThan(start);
                    this->_videoStream->startNoEarlierThan(start);
                }
            }

            this->_currentItem = std::move(this->_pendingItem);
            this->_pendingItem = nullptr;

            this->_currentItem->input->getNode()->syncWithParent();
            this->_playlistItemIndex = this->_currentItem->playlistItemIndex;

            {
                // A commit that lands while the input is frozen syncs the new item to a PAUSED
                // parent. Whether it then reaches PLAYING on resume decides whether the next ad
                // break has a picture at all, so record the state that was actually reached
                // rather than the state that was intended.
                GstState itemState = GST_STATE_VOID_PENDING;
                GstState itemPending = GST_STATE_VOID_PENDING;

                auto node = this->_currentItem->input->getNode();

                if (node) {
                    gst_element_get_state((*node)->get(), &itemState, &itemPending, 0);
                }

                logger.log(
                    "Committed playlist item {} at index {} to the media graph (item {}, pending {}, playback {})",
                    this->_playlistItemIds[this->_playlistItemIndex],
                    this->_playlistItemIndex,
                    gst_element_state_get_name(itemState),
                    gst_element_state_get_name(itemPending),
                    this->_playbackActive ? "active" : "frozen"
                );
            }


#if INFO_OVERLAY
            {
                auto textOverlay = this->_bin->get("textoverlay");

                if (textOverlay) {
                    std::string text = "[" + std::to_string(this->_currentItem->playlistItemIndex) + "] " + this->_currentItem->input->getInputParameters().getUris()[0];
                    
                    textOverlay->setProperty<const gchar*>("text", text.c_str());
                }
            }
#endif
        }
    }
}

bool transcoder::PlaylistInput::_canArmPending(
    const std::shared_ptr<InputStream> &newAudioStream,
    const std::shared_ptr<InputStream> &newVideoStream
) const {
    if (! _makeBeforeBreakEnabled()) {
        return false;
    }

    // Arming only helps while something is on air; after an EOS the legacy commit has no gap to close
    if (! this->_currentItem || this->_currentItem->eos || this->_currentItem->error) {
        return false;
    }

    // The channel picks passthrough processors by codec, so a passthrough stream needs the
    // streams-changed rebuild that only the legacy commit performs.
    if (this->_audioStream->usesPassthrough() || this->_videoStream->usesPassthrough()) {
        return false;
    }

    // Same media types on both sides: the connectors keep the playlist streams they already have
    bool hadAudio = this->_audioStream->getStreamParameters().getCodec() != Codec::UNKNOWN;
    bool hadVideo = this->_videoStream->getStreamParameters().getCodec() != Codec::UNKNOWN;

    if (hadAudio != (newAudioStream != nullptr) || hadVideo != (newVideoStream != nullptr)) {
        return false;
    }

    return (newVideoStream && this->_videoStream->usesDecoded()) || (newAudioStream && this->_audioStream->usesDecoded());
}

void transcoder::PlaylistInput::_armPending(
    const std::shared_ptr<InputStream> &newAudioStream,
    const std::shared_ptr<InputStream> &newVideoStream
) {
    std::unique_lock lock(this->_mutex);

    auto &input = this->_pendingItem->input;
    auto  arm   = std::make_shared<Arm>();

    arm->owner       = this;
    arm->gate        = std::make_shared<ArmGate>();
    arm->timeline    = input->isLive() ? std::make_shared<LiveTimeline>() : nullptr;
    arm->audioStream = newAudioStream;
    arm->videoStream = newVideoStream;

    {
        std::weak_ptr<Arm> weakArm = arm;

        arm->gate->onKey = [weakArm]() {
            _scheduleCut(weakArm, 0);
        };

        arm->timeoutId = g_timeout_add_full(
            G_PRIORITY_DEFAULT,
            ARM_TIMEOUT_MS,
            [](gpointer data) -> gboolean {
                if (auto arm = static_cast<std::weak_ptr<Arm> *>(data)->lock()) {
                    arm->timeoutId = 0;

                    std::unique_lock lock(arm->owner->_mutex);

                    if (arm->owner->_arm == arm) {
                        arm->owner->_abortArm("no first frame within " + std::to_string(ARM_TIMEOUT_MS) + " ms");
                    }
                }

                return G_SOURCE_REMOVE;
            },
            new std::weak_ptr<Arm>(weakArm),
            [](gpointer data) {
                delete static_cast<std::weak_ptr<Arm> *>(data);
            }
        );
    }

    std::vector<pipeline::PadPtr> videoPads;
    std::vector<pipeline::PadPtr> audioPads;

    if (newVideoStream && this->_videoStream->usesDecoded()) {
        videoPads = this->_videoStream->armInputStream(newVideoStream, arm->timeline);
    }

    if (newAudioStream && this->_audioStream->usesDecoded()) {
        audioPads = this->_audioStream->armInputStream(newAudioStream, arm->timeline);
    }

    // The first decoded video frame is the key; an item without video is keyed on its audio
    bool keyIsVideo = ! videoPads.empty();

    for (auto &pad : videoPads) {
        if (arm->timeline) {
            _measureArrival(pad, arm->gate, true, true);
        }

        _holdUntilCut(pad, arm->gate, true, true);

        arm->pads.push_back(pad);
    }

    for (auto &pad : audioPads) {
        if (arm->timeline) {
            _measureArrival(pad, arm->gate, ! keyIsVideo, false);
        }

        _holdUntilCut(pad, arm->gate, ! keyIsVideo, false);

        arm->pads.push_back(pad);
    }

    this->_arm = arm;

    logger.log(
        "Arming playlist item {} at index {}: decoding it while the current item stays on air",
        this->_playlistItemIds[this->_pendingItem->playlistItemIndex],
        this->_pendingItem->playlistItemIndex
    );

    // PLAYING releases the item's decoders; their output waits in the probes above
    input->getNode()->syncWithParent();
}

void transcoder::PlaylistInput::_scheduleCut(const std::weak_ptr<Arm> &arm, guint delayMs) {
    auto callback = [](gpointer data) -> gboolean {
        if (auto arm = static_cast<std::weak_ptr<Arm> *>(data)->lock()) {
            arm->owner->_cutOver(arm);
        }

        return G_SOURCE_REMOVE;
    };

    auto destroy = [](gpointer data) {
        delete static_cast<std::weak_ptr<Arm> *>(data);
    };

    if (delayMs) {
        g_timeout_add_full(G_PRIORITY_DEFAULT, delayMs, callback, new std::weak_ptr<Arm>(arm), destroy);

    } else {
        g_idle_add_full(G_PRIORITY_DEFAULT, callback, new std::weak_ptr<Arm>(arm), destroy);
    }
}

void transcoder::PlaylistInput::_cutOver(const std::shared_ptr<Arm> &arm) {
    std::unique_lock lock(this->_mutex);

    if (this->_arm != arm || ! this->_pendingItem) {
        DBG(("Ignoring a stale cut-over request"));

        return;
    }

    // A live item plays as early as its tighter stream allows, so wait briefly for the first
    // audio that plays to leave the decoder; without it the item keeps its timestamps.
    if (arm->timeline && arm->audioStream && arm->videoStream) {
        bool audioKnown;

        {
            std::lock_guard gateLock(arm->gate->mutex);

            audioKnown = GST_CLOCK_TIME_IS_VALID(arm->gate->trim[0]);
        }

        if (! audioKnown) {
            gint64 now = g_get_monotonic_time();

            if (! arm->audioWaitSince) {
                arm->audioWaitSince = now;
            }

            if (now - arm->audioWaitSince < CUT_AUDIO_WAIT_US) {
                _scheduleCut(arm, CUT_HOLD_POLL_MS);

                return;
            }

            std::lock_guard gateLock(arm->gate->mutex);

            arm->gate->trim[0] = 0;
        }
    }

    // The current item runs behind real time by the downstream latency (clocksync releases its
    // buffers at running time + latency), while a live key is stamped on arrival. Cutting now
    // would leave a gap from the current position to the key, shown as black. Keep the current
    // item on air until it reaches the key; the live item then continues at its arrival latency.
    if (arm->timeline && this->_currentItem && ! this->_currentItem->eos && ! this->_currentItem->error) {
        GstClockTime keyRt;

        {
            std::lock_guard gateLock(arm->gate->mutex);

            keyRt = arm->gate->keyRt;
        }

        // Where the item's first frame plays when cut right away. Fixed by its measured arrival,
        // not by the clock now: the margin moves with the clock, which the current item, trailing
        // the clock by the downstream latency, would never reach.
        if (GST_CLOCK_TIME_IS_VALID(keyRt)) {
            std::lock_guard gateLock(arm->gate->mutex);

            keyRt -= std::min(keyRt, arm->gate->itemTrim());
        }

        GstClockTime position = this->_videoStream->getCurrentTime();
        GstClockTime step     = this->_videoStream->getLastDuration();

        if (! GST_CLOCK_TIME_IS_VALID(position)) {
            position = this->_audioStream->getCurrentTime();
            step     = this->_audioStream->getLastDuration();
        }

        // Where the live item would start if the cut happened now
        if (GST_CLOCK_TIME_IS_VALID(position) && GST_CLOCK_TIME_IS_VALID(step)) {
            position += step;
        }

        if (GST_CLOCK_TIME_IS_VALID(keyRt) && GST_CLOCK_TIME_IS_VALID(position) && position < keyRt) {
            gint64 now = g_get_monotonic_time();

            if (! arm->holdingSince) {
                arm->holdingSince = now;

                DBG(("Holding the cut for the playlist to reach the live item's first frame ({} ms ahead)", (gint64) ((keyRt - position) / GST_MSECOND)));
            }

            if (now - arm->holdingSince < CUT_HOLD_MAX_US) {
                _scheduleCut(arm, CUT_HOLD_POLL_MS);

                return;
            }

            logger.warning("Playlist did not reach the live item's first frame within {} ms - cutting with a gap", CUT_HOLD_MAX_US / G_TIME_SPAN_MILLISECOND);
        }
    }

    this->_arm = nullptr;

    if (arm->timeoutId) {
        g_source_remove(arm->timeoutId);

        arm->timeoutId = 0;
    }

    // Stop current. Nothing reaches concat from here until the held frames are released below.
    if (this->_currentItem) {
        auto &item = this->_currentItem;

        item->input->onError({});
        item->input->onStateChanged({});
        item->input->onStreamsChanged({});
        item->input->onScteData({});

        item->input->getNode()->null();
    }

    DBG((
        "Cut-over positions: video {} ms, audio {} ms",
        GST_CLOCK_TIME_IS_VALID(this->_videoStream->getCurrentTime()) ? (gint64) (this->_videoStream->getCurrentTime() / GST_MSECOND) : -1,
        GST_CLOCK_TIME_IS_VALID(this->_audioStream->getCurrentTime()) ? (gint64) (this->_audioStream->getCurrentTime() / GST_MSECOND) : -1
    ));

    // Where the current item's audio and video end, before synchronize() aligns them
    GstClockTime videoEnd = this->_videoStream->getCurrentTime();
    GstClockTime audioEnd = this->_audioStream->getCurrentTime();

    this->_audioStream->synchronize(this->_videoStream);

    if (arm->timeline) {
        GstClockTime base = this->_videoStream->getCurrentTime();

        if (! GST_CLOCK_TIME_IS_VALID(base)) {
            base = this->_audioStream->getCurrentTime();
        }

        arm->timeline->base = GST_CLOCK_TIME_IS_VALID(base) ? base : 0;

        GstClockTime keyRt;

        {
            std::lock_guard gateLock(arm->gate->mutex);

            keyRt = arm->gate->keyRt;
        }

        if (GST_CLOCK_TIME_IS_VALID(keyRt)) {
            // Keyed on the held first frame rather than the oldest queued buffer, so the item
            // continues the playlist at its arrival latency. Setting the offset re-sends the
            // sticky segment, which the held buffers pick up on release.
            GstClockTime trim = _armTrim(arm->gate, arm->pads, keyRt);

            arm->trim = trim;

            {
                std::lock_guard gateLock(arm->gate->mutex);

                auto ms = [](GstClockTime value) { return GST_CLOCK_TIME_IS_VALID(value) ? (gint64) (value / GST_MSECOND) : (gint64) -1; };

                DBG((
                    "Live item arrival: video can play {} ms early (first at {} ms), audio {} ms (first at {} ms), now {} ms",
                    ms(arm->gate->trim[1]), ms(arm->gate->decodedRt[1]), ms(arm->gate->trim[0]), ms(arm->gate->decodedRt[0]),
                    ms(_clockRunningTime((*arm->pads.front())->get()))
                ));
            }

            std::lock_guard timelineLock(arm->timeline->mutex);

            arm->timeline->offset  = -static_cast<gint64>(std::min(keyRt, arm->timeline->base + trim));
            arm->timeline->known   = true;
            arm->timeline->firstAt = keyRt;
            arm->timeline->trim    = trim;

            for (auto &pad : arm->pads) {
                gst_pad_set_offset((*pad)->get(), gst_pad_get_offset((*pad)->get()) + arm->timeline->offset);
            }

            logger.log(
                "Rebasing live playlist item by {} ms to continue the playlist timeline (first frame at {} ms, {} ms earlier than its timestamps, playlist at {} ms, cut held {} ms)",
                arm->timeline->offset / (gint64) GST_MSECOND,
                (gint64) (keyRt / GST_MSECOND),
                (gint64) (trim / GST_MSECOND),
                (gint64) (arm->timeline->base / GST_MSECOND),
                arm->holdingSince ? (g_get_monotonic_time() - arm->holdingSince) / G_TIME_SPAN_MILLISECOND : 0
            );

        } else {
            for (auto &pad : arm->pads) {
                PlaylistStream::_rebaseLiveTimeline(pad, arm->timeline, false);
            }
        }
    }

    // Connect the new item before releasing the old one: concat makes the next pad current as
    // soon as the current one is released.
    if (arm->videoStream) {
        this->_videoStream->connectArmedStream(arm->videoStream);
    }

    if (arm->audioStream) {
        this->_audioStream->connectArmedStream(arm->audioStream);
    }

    if (arm->timeline) {
        auto gate = arm->gate;

        // The live item must not start before the current item's last audio or video ends
        GstClockTime keyRt;

        {
            std::lock_guard gateLock(gate->mutex);

            keyRt = gate->keyRt;
        }

        GstClockTime trim = arm->trim;

        if (GST_CLOCK_TIME_IS_VALID(keyRt)) {
            keyRt -= std::min(keyRt, trim);
        }

        GstClockTime residual = 0;

        for (auto end : { videoEnd, audioEnd }) {
            if (GST_CLOCK_TIME_IS_VALID(end) && GST_CLOCK_TIME_IS_VALID(keyRt) && end > keyRt) {
                residual = std::max(residual, end - keyRt);
            }
        }

        for (int video = 0; video < 2; video++) {
            auto &stream = video ? this->_videoStream : this->_audioStream;

            if ((video ? arm->videoStream : arm->audioStream) && stream->usesDecoded()) {
                // Each stream against its own first buffer, with one residual: arrival times
                // share the item's clock, so the streams keep their alignment
                stream->alignToArrival([gate, video, trim]() {
                    std::lock_guard gateLock(gate->mutex);

                    GstClockTime arrival = gate->releasedRt[video];

                    return GST_CLOCK_TIME_IS_VALID(arrival) ? arrival - std::min(arrival, trim) : arrival;
                }, residual);
            }
        }
    }

    // Release the held buffers before tearing the current item down: they wait in concat until
    // its pads are released below, so the teardown is not added to the gap.
    {
        std::lock_guard gateLock(arm->gate->mutex);

        arm->gate->released = true;
    }

    arm->gate->cv.notify_all();

    // Remove old
    if (this->_currentItem) {
        auto &input = this->_currentItem->input;

        this->_audioStream->removeStreamsByInput(input);
        this->_videoStream->removeStreamsByInput(input);

        this->_bin->remove(input->getNode());
    }

    // Same media types as before (_canArmPending), so the channel keeps its connectors
    this->_audioStream->update(arm->audioStream ? arm->audioStream->getStreamParameters() : StreamParameters());
    this->_videoStream->update(arm->videoStream ? arm->videoStream->getStreamParameters() : StreamParameters());

    this->_currentItem = std::move(this->_pendingItem);
    this->_pendingItem = nullptr;

    this->_playlistItemIndex = this->_currentItem->playlistItemIndex;

    logger.log(
        "Committed playlist item {} at index {} to the media graph (make-before-break)",
        this->_playlistItemIds[this->_playlistItemIndex],
        this->_playlistItemIndex
    );
}

void transcoder::PlaylistInput::_abortArm(const std::string &reason) {
    std::unique_lock lock(this->_mutex);

    auto arm = this->_arm;

    if (! arm) {
        return;
    }

    this->_arm = nullptr;

    if (arm->timeoutId) {
        g_source_remove(arm->timeoutId);

        arm->timeoutId = 0;
    }

    {
        std::lock_guard gateLock(arm->gate->mutex);

        arm->gate->aborted = true;
    }

    arm->gate->cv.notify_all();

    if (this->_pendingItem) {
        auto input = this->_pendingItem->input;

        logger.warning(
            "Abandoning the switch to playlist item {}: {} - the current item stays on air",
            this->_playlistItemIds[this->_pendingItem->playlistItemIndex],
            reason
        );

        this->_audioStream->removeStreamsByInput(input);
        this->_videoStream->removeStreamsByInput(input);

        this->_pendingItem->cleanup();
        this->_pendingItem = nullptr;
    }
}
