#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <numeric>

#include <boost/url/url.hpp>
#include <boost/url/parse.hpp>

#include <gst/mpegts/gst-scte-section.h>

#include <sstream>

#include "utpl/common/types/exception.h"
#include "utpl/common/utils/gstreamer.h"
#include "utpl/common/debug.h"

#include "utpl/pipeline/utils.h"

#include "utpl/logger.h"

#include "utpl/transcoder/input/single.h"

#include "../event/scheduler/scte.h"
#include "dash/emsg.h"

using namespace utpl;

namespace urls = boost::urls;

#define NODE_NAME_SRC     "src"
#define NODE_NAME_DMX_DEC "demuxer-decoder"
#define NODE_NAME_QUEUE   "queue"

constexpr const uint32_t MAX_BACKOFF_TIME = 60 * 10; // 10 minutes
// An encoder that reconnects must not sit behind a multi-minute backoff
constexpr const uint32_t RTMP_MAX_BACKOFF_TIME = 5;
// A writer that restarts is back within seconds
constexpr const uint32_t MXL_MAX_BACKOFF_TIME = 5;
// A reader that has seen no data for this long is reattached to its flow
constexpr const guint    MXL_INPUT_IDLE_SECONDS = 3;
// Raw frames are large: the queue behind an MXL reader holds a second at most, which is also about what the
// reader can fall behind before the flow's ring has been overwritten
constexpr const guint64  MXL_STREAM_QUEUE_MAX_TIME = 1 * GST_SECOND;
// The flows carry the time at which their pictures and sound were made. This much is added to the time at which
// the first one arrives (see MxlWatch::Timeline), which is the room the channel gives the flows for the jitter of
// their writer and for the writer lagging more on one flow than on the other
constexpr const GstClockTime MXL_INPUT_DELAY = 300 * GST_MSECOND;
// mxlsrc delivers audio in 1 ms buffers; the rest of the engine works on longer ones
constexpr const char    *MXL_AUDIO_CHUNK = "1/50";
constexpr const guint    RTMP_IDLE_TIMEOUT = 5; // seconds without packets before the source errors
constexpr const guint64 RTMP_SOURCE_QUEUE_MAX_TIME = 1 * GST_SECOND;
// Must exceed the publisher's keyframe interval, see the demuxer queues in the RTMP branch
constexpr const guint64 RTMP_DEMUX_QUEUE_MAX_TIME = 10 * GST_SECOND;
// How long to wait for the demuxer to announce a second stream before releasing what it has
constexpr const guint RTMP_STREAM_DISCOVERY_TIMEOUT = 3; // seconds
constexpr const guint64 SRT_SOURCE_QUEUE_MAX_TIME = 250 * GST_MSECOND;
constexpr const guint SRT_STREAM_QUEUE_MAX_BUFFERS = 150;
// Demuxer queue limits: time is the effective one, bytes/buffers only cap pathological streams
constexpr const guint64 DEMUXER_QUEUE_MAX_TIME    = 2 * GST_SECOND;
constexpr const guint   DEMUXER_QUEUE_MAX_BYTES   = 64 * 1024 * 1024;
constexpr const guint   DEMUXER_QUEUE_MAX_BUFFERS = 1000;

static auto &logger = Logger::getLogger(UTPL_LOGGER_NAME_INPUT);


static std::string _padStreamId(const pipeline::PadPtr &pad) {
    std::string ret;

    gchar *id = gst_pad_get_stream_id((*pad)->get());
    if (id) {
        ret = id;

        g_free(id);
    }

    return ret;
}


// An MPD served under another name needs an explicit transport.dash block
static bool _isDashManifestPath(const std::string &path) {
    static const std::string suffix = ".mpd";

    auto lower = utils::toLower(path);

    return lower.size() >= suffix.size() && lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0;
}

namespace {
    // Per-track 'emsg' scanning state, owned by the probe on that track's qtdemux sink pad
    struct DashEmsgTrack {
        transcoder::SingleInput      *input;
        transcoder::dash::EmsgScanner scanner;
        GstClockTime                  segmentStart = GST_CLOCK_TIME_NONE;
    };

    // Recently injected 'emsg' ids; an event is repeated in every segment of its announcement window
    constexpr const std::size_t DASH_EMSG_ID_HISTORY = 64;
}

static std::string _getNextId(std::atomic<int> &a) {
    return std::to_string(a.fetch_add(1, std::memory_order::memory_order_relaxed));
}

transcoder::SingleInput::SingleInputStream::SingleInputStream(pipeline::PadPtr &passthroughPeekPad, pipeline::PadPtr &passthroughPad, pipeline::BinPtr bin) : BaseInputStream(bin) {
    this->_passthroughPad     = passthroughPad;
    this->_passthroughPeekPad = passthroughPeekPad;
    
    this->_linked    = false;
    this->_streaming = false;
    this->_streamId  = _padStreamId(passthroughPeekPad);

    this->setStreamParameters(StreamParameters::fromCaps(passthroughPeekPad->getCaps()));

    this->block({ this->_passthroughPad });
}

const std::string &transcoder::SingleInput::SingleInputStream::getStreamId() const {
    return this->_streamId;
}

void transcoder::SingleInput::SingleInputStream::replacePads(pipeline::PadPtr &passthroughPeekPad, pipeline::PadPtr &passthroughPad) {
    std::lock_guard lock(this->_blockedPadsMutex);

    bool linked = this->_linked;

    // A linked stream is already attached to the running channel: the start-up pad blocks (released when the
    // input first plays) must not be applied to the rebuilt chain
    if (linked) {
        this->_streaming = true;
    }

    logger.log("Input stream {} changed format: {} -> {}", this->_streamId,
        this->_passthroughPeekPad->getCaps().toString(), passthroughPeekPad->getCaps().toString()
    );

    // The old pads are gone with the old demuxer group; tear the chain down and build it again on the new pads
    this->unlink();

    this->_blockedPads.clear();

    this->_passthroughPad     = passthroughPad;
    this->_passthroughPeekPad = passthroughPeekPad;
    this->_streamId           = _padStreamId(passthroughPeekPad);

    this->setStreamParameters(StreamParameters::fromCaps(passthroughPeekPad->getCaps()));

    if (linked) {
        this->link();
    }

    if (! this->_streaming) {
        this->block({ this->_passthroughPad });
    }
}

static gint _sortDlbLo(GValue *a, GValue *b) {
    auto *aName = GST_ELEMENT_NAME((GstElementFactory *) g_value_get_object(a));
    auto *bName = GST_ELEMENT_NAME((GstElementFactory *) g_value_get_object(b));

    if (strcmp(aName, bName) != 0) {
        if (strcmp(aName, "dlbpatdec") == 0) {
            return 1;

        } else if (strcmp(bName, "dlbpatdec") == 0) {
            return -1;
        }
    }

    return 0;
}

GValueArray *transcoder::SingleInput::SingleInputStream::_decoderSortCallback(GstElement *bin, GstPad *pad, GstCaps *caps, GValueArray *factories, SingleInputStream *self) {
    GValueArray *ret = g_value_array_copy(factories);

    g_value_array_sort(ret, (GCompareFunc)_sortDlbLo);

    return ret;
}

pipeline::PadPtr transcoder::SingleInput::SingleInputStream::getSrcPad(bool passthrough) {
    if (passthrough) {
        this->_linkPassthrough = true;

    } else {
        this->_linkDecoded = true;
    }

    return nullptr;
}

pipeline::PadPtr transcoder::SingleInput::SingleInputStream::peekPad(bool passthrough) {
    if (passthrough) {
        return this->_passthroughPeekPad;
    }

    return this->BaseInputStream::peekPad(passthrough);
}

std::string transcoder::SingleInput::SingleInputStream::_getSplitterName() {
    std::string ret;

    if (this->getStreamParameters().hasAudioParameters()) {
        ret = "audio";

    } else if (this->getStreamParameters().hasVideoParameters()) {
        ret = "video";

    } else if (this->getStreamParameters().hasCaptionParameters()) {
        ret = "caption";
    }

    return ret + "-passthrough-splitter";
}

std::string transcoder::SingleInput::SingleInputStream::_getDecoderName() {
    std::string ret = "";

    if (this->getStreamParameters().hasAudioParameters()) {
        ret += "audio";

    } else if (this->getStreamParameters().hasVideoParameters()) {
        ret += "video";

    } else if (this->getStreamParameters().hasCaptionParameters()) {
        ret += "caption";
    }

    return ret + "-decoder";
}

void transcoder::SingleInput::SingleInputStream::link() {
    if (! this->_linked) {
        this->_linkDecoded     = false;
        this->_linkPassthrough = false;
    
        this->BaseInputStream::link();

        pipeline::ElementPtr decoder;
        pipeline::ElementPtr splitter;
    
        if (this->_linkDecoded) {
            std::vector<pipeline::PadPtr> newBlocked;
    
            pipeline::PadPtr decoderPad     = this->_passthroughPad;
            pipeline::PadPtr passthroughPad = this->_passthroughPad;
    
            if (this->_linkPassthrough) {
                auto bin = this->getBin();
    
                splitter = bin->add(pipeline::Element::make(this->_getSplitterName(), "tee"));
    
                splitter->syncWithParent();
    
                // passthrough & decoded
                {
                    auto q = pipeline::utils::createQueue(bin, std::string(), 64, 0, 0);
        
                    splitter->requestPad("src_%u")->link(
                        q->getPad("sink")
                    );
        
                    q->syncWithParent();
    
                    decoderPad = q->getPad("src");
                }
    
                {
                    auto q = pipeline::utils::createQueue(bin, std::string(), 64, 0, 0);
        
                    splitter->requestPad("src_%u")->link(
                        q->getPad("sink")
                    );
        
                    q->syncWithParent();
    
                    passthroughPad = q->getPad("src");
                }
    
                // Link demux with splitter
                this->_passthroughPad->link(
                    splitter->getPad("sink")
                );
                
                // Link to passthrough
                this->linkSrcPad(passthroughPad, true);
    
                newBlocked.push_back(passthroughPad);
            }
    
            // Create decoder
            {
                decoder = this->getBin()->add(pipeline::Element::make(this->_getDecoderName(), "decodebin"));
                
                decoder->setProperty<guint>   ("max-size-buffers", 2);
                decoder->setProperty<guint>   ("max-size-bytes",   0);
                decoder->setProperty<guint64> ("max-size-time",    0);
    
                decoder->onSignal("autoplug-sort", G_CALLBACK(_decoderSortCallback), this);
    
                decoder->onPadAdded([this](pipeline::PadPtr &pad) {
                    std::lock_guard lock(this->_blockedPadsMutex);

                    // A chain rebuilt while playing (replacePads) must flow right away
                    if (! this->_streaming) {
                        this->_blockedPads[pad] = pad->block(true);
                    }

                    // Link to decoded
                    this->linkSrcPad(pad, false);
    
                    return false;
                });
    
                // Link to decoder
                decoderPad->link(
                    decoder->getPad("sink")
                );
            }
    
            this->block(this->_streaming ? std::vector<pipeline::PadPtr>() : newBlocked);
    
        } else {
            // passthrough only
            this->linkSrcPad(this->_passthroughPad, true);
        }
    
        if (decoder) {
            decoder->syncWithParent();
        }
    
        if (splitter) {
            splitter->syncWithParent();
        }

        this->_linked = true;
    }
}

void transcoder::SingleInput::SingleInputStream::unlink() {
    if (this->_linked) {
        this->BaseInputStream::unlink();

        {
            auto splitter = this->getBin()->get(_getSplitterName());
            if (splitter) {
                splitter->null();
            }

            auto decoder = this->getBin()->get(_getDecoderName());
            if (decoder) {
                decoder->null();
            }

            if (splitter) {
                this->getBin()->remove(splitter);
            }

            if (decoder) {
                this->getBin()->remove(decoder);
            }
        }

        this->_linked = false;
    }
}

void transcoder::SingleInput::SingleInputStream::block(const std::vector<pipeline::PadPtr> &newBlocked) {
    std::lock_guard lock(this->_blockedPadsMutex);

    std::unordered_map<pipeline::PadPtr, guint> newBlockedMap;

    for (const auto &pad : newBlocked) {
        newBlockedMap[pad] = pad->block(true);
    }

    for (const auto &pad : this->_blockedPads) {
        pad.first->unblock(pad.second);
    }

    this->_blockedPads = newBlockedMap;
}

void transcoder::SingleInput::SingleInputStream::unblock() {
    std::lock_guard lock(this->_blockedPadsMutex);

    this->_streaming = true;

    this->block({});
}

transcoder::SingleInput::SingleInput(bool useBuffering, const std::string &name) : _name(name) {
    TRACE(("CALL, buffering: {}, name: {}", useBuffering, name));

    this->_isLive              = false;
    this->_srcHasAllStreams    = false;
    this->_srcDemuxersCount    = 0;
    this->_srcStreamsCount     = 0;
    this->_srcExpectedStreams  = 0;
    this->_state               = InputState::CREATED;
    this->_hlsScteEventId      = 0;
    this->_protocol            = Protocol::UNKNOWN;
    this->_duration            = GST_CLOCK_TIME_NONE;
    this->_useBuffering        = useBuffering;
    this->_srcRetryAttempt     = 0;
    this->_srcRestartTimerId   = 0;
    this->_srcErrorSeq         = -1;
    this->_mxlWatchId          = 0;

    if (this->_name.empty()) {
        this->_name = "input-single-" + std::to_string(this->_id.getInt());
    }
}

transcoder::SingleInput::~SingleInput() {
    this->cleanup();

    TRACE(("CALL"));
}

void transcoder::SingleInput::prepare(const InputParameters &parameters) {
    pipeline::ElementPtr nodeDecoder;
    pipeline::ElementPtr nodeSrc;

    // Always cleanup 
    this->cleanup();

    this->_inputParameters = parameters;

    this->_bin = pipeline::Bin::create(this->_name);

    this->_bin->onMessage([this](GstMessage *msg, bool &handled) {
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
            std::string elementName = GST_MESSAGE_SRC_NAME(msg);

            ERR(("Received an error message on input '{}', element: '{}'", this->getName(), elementName));

            GstObject *parent = GST_OBJECT(msg->src);
            while (parent != NULL) {
                if (GST_OBJECT_NAME(parent) == this->_bin->getName()) {
                    break;
                }
                
                parent = gst_object_get_parent(parent);
            }

            if (! parent) {
                WARN(("Seems that the element '{}' is not a child of this input '{}'", elementName, this->getName()));

            } else {
                guint32 msgSeq = gst_message_get_seqnum(msg);

                if (msgSeq == this->_srcErrorSeq) {
                    WARN(("Received repeated error message for local object - ignoring"));

                } else {
                    gchar *debug  = nullptr;

                    gst_message_parse_error(msg, nullptr, &debug);

                    this->_srcErrorSeq = msgSeq;

                    if (this->_errorCallback) {
                        handled = this->_errorCallback(std::string(debug));
                        
                    } else {
                        DBG(("Input '{}' has not attached any error handler - error will go further", this->getName()));
                    }

                    if (this->isLive() && ! handled) {
                        auto backoffDelay = this->_calculateBackoffDelay(this->_srcRetryAttempt++);

                        DBG(("Retrying input {} for message seq {} in {} seconds", this->getName(), this->_srcErrorSeq, backoffDelay));

                        this->_srcRestartTimerId = g_timeout_add_seconds(backoffDelay, G_SOURCE_FUNC(_restartSrc), this);

                        handled = true;
                    }

                    if (! handled) {
                        DBG(("Input '{}' error handler has not handled error - error will go further", this->getName()));
                    }

                    if (debug) {
                        g_free (debug);
                    }
                }
            }
        }

        return true;
    });
    
    {
        pipeline::BinPtr srcBin = pipeline::Bin::create(NODE_NAME_SRC);

        this->_bin->add(srcBin);

        {
            const auto &uris = parameters.getUris();

            this->_isLive   = false;
            this->_protocol = Protocol::UNKNOWN;

            {
                const auto &uriString = uris[0];

                auto url = urls::parse_uri(uriString);
        
                if (
                    url->scheme_id() == urls::scheme::file ||
                    url->scheme_id() == urls::scheme::none
                ) {
                    nodeSrc     = srcBin->add(pipeline::Element::make("filesrc"));
                    nodeDecoder = this->_createDemuxer();

                    nodeSrc->setProperty<const gchar *>("location", url->path().c_str());
        
                    this->_srcStreamsCount  = 1;
                    this->_srcHasAllStreams = true;
                    this->_protocol         = Protocol::FILE;

                } else if (url->scheme() == "ndi") {
                    const auto *ndiParameters = reinterpret_cast<const NdiInputTransportParameters *>(parameters.getTransportParameters(Protocol::NDI));

                    nodeSrc     = srcBin->add(pipeline::Element::make("ndisrc"));
                    nodeDecoder = this->_createDemuxer();

                    nodeSrc->setProperty<const gchar *>("url-address", ndiParameters->getUrlAddress().c_str());
                    nodeSrc->setProperty<const gchar *>("ndi-name", ndiParameters->getNdiName().c_str());

                    auto ndiSrcPad = nodeSrc->getPad("src");

                    this->_srcStreamsCount  = 1;
                    this->_srcHasAllStreams = true;
                    this->_protocol         = Protocol::NDI;
                    
                } else if (url->scheme() == "mxl") {
                    const auto *mxlParameters = reinterpret_cast<const MxlInputTransportParameters *>(parameters.getTransportParameters(Protocol::MXL));

                    if (mxlParameters == nullptr) {
                        throw InvalidArgumentException("An MXL input needs a transport.mxl block that names its flows: '" + uriString + "'");
                    }

                    this->_createMxlFlows(srcBin, std::string(url->path()), *mxlParameters);

                    this->_isLive   = true;
                    this->_protocol = Protocol::MXL;

                } else if (url->scheme() == "udp") {
                    auto src   = srcBin->add(pipeline::Element::make("udpsrc"));
                    auto queue = srcBin->add(pipeline::Element::make("queue"));

                    // src
                    src->setProperty<const gchar *>("address", url->host_ipv4_address().to_string().c_str());
                    src->setProperty<gint>("port", url->port_number());

                    // queue
                    queue->setProperty<gboolean>("silent", true);

                    {
                        const auto *udpParameters = reinterpret_cast<const UdpInputTransportParameters *>(parameters.getTransportParameters(Protocol::UDP));

                        if (udpParameters) {
                            const auto &mcastParameters = udpParameters->getMulticastParameters();

                            {
                                std::string ifaces = std::accumulate(
                                    mcastParameters.getInterfaces().begin(), 
                                    mcastParameters.getInterfaces().end(), 
                                    std::string(),
                                    [](std::string &ss, const std::string &s) {
                                        return ss.empty() ? s : ss + "," + s;
                                    }
                                );

                                if (! ifaces.empty()) {
                                    src->setProperty<const gchar *>("multicast-iface", ifaces.c_str());
                                }
                            }
                        }
                    }

                    src->link(queue);

                    nodeSrc     = queue;
                    nodeDecoder = this->_createDemuxer();

                    this->_isLive = true;
        
                    this->_srcStreamsCount  = 1;
                    this->_srcHasAllStreams = true;
                    this->_protocol         = Protocol::UDP;

                } else if (url->scheme() == "srt") {
                    std::string targetUri;
                    
                    {
                        const auto *srtParameters = reinterpret_cast<const SrtInputTransportParameters *>(parameters.getTransportParameters(Protocol::SRT));
                        if (srtParameters == nullptr) {
                            targetUri = uriString;
        
                        } else {
                            urls::url mutableUrl = url.value();
        
                            // Parameters from extra section takes precedense over parameters specified in URL
                            {
                                auto params = mutableUrl.params();
        
                                if (srtParameters->getLatency() > 0) {
                                    params.set("latency", std::to_string(srtParameters->getLatency()), urls::ignore_case);
                                }
                                
                                {
                                    std::string modeValue;
        
                                    switch (srtParameters->getMode()) {
                                        case SrtProtocolMode::CALLER:
                                            modeValue = "caller";
                                            break;
        
                                        case SrtProtocolMode::LISTENER:
                                            modeValue = "listener";
                                            break;
        
                                        case SrtProtocolMode::UNKNOWN:
                                            break;
                                    }
        
                                    if (! modeValue.empty()) {
                                        params.set("mode", modeValue);
                                    }
                                }
        
                                if (srtParameters->getBufferSize() > 0) {
                                    params.set("rcvbuf", std::to_string(srtParameters->getBufferSize()));
                                }
                            }                    
        
                            targetUri = mutableUrl.buffer();
                        }
                    }
        
                    auto src   = srcBin->add(pipeline::Element::make("srtsrc"));
                    auto queue = srcBin->add(pipeline::Element::make("queue"));

                    // src
                    src->setProperty<const gchar *>("uri", targetUri.c_str());

                    // queue
                    queue->setProperty<gboolean>("silent", true);
                    queue->setProperty<guint>   ("max-size-buffers", 0);
                    queue->setProperty<guint>   ("max-size-bytes",   0);
                    queue->setProperty<guint64> ("max-size-time",    SRT_SOURCE_QUEUE_MAX_TIME);

                    src->link(queue);

                    nodeSrc     = queue;
                    nodeDecoder = this->_createDemuxer();

                    this->_isLive = true;      

                    this->_srcStreamsCount  = 1;
                    this->_srcHasAllStreams = true;
                    this->_protocol         = Protocol::SRT;

                } else if (url->scheme() == "rtmp" || url->scheme() == "rtmps") {
                    auto src   = srcBin->add(pipeline::Element::make("rtmp2src"));
                    auto flv   = srcBin->add(pipeline::Element::make("flvdemux"));
                    auto mux   = srcBin->add(pipeline::Element::make("mpegtsmux"));
                    auto queue = srcBin->add(pipeline::Element::make("queue"));

                    // src
                    src->setProperty<const gchar *>("location", uriString.c_str());
                    src->setProperty<guint>        ("idle-timeout",    RTMP_IDLE_TIMEOUT);
                    src->setProperty<gboolean>     ("no-eof-is-error", true);

                    // A failed connect (the encoder is not publishing yet) must not reach the pipeline as an
                    // ERROR message. The input bin is usually still waiting for decodebin to preroll at that
                    // point, and GstBin marks a state change that sees a child error as failed: once the
                    // stream does arrive the input never commits PAUSED/PLAYING and its streams stay
                    // blocked. So rtmp2src gets its own bus: errors become a reconnect, everything else is
                    // forwarded to the source bin as usual.
                    {
                        // Only the connection is restarted. The muxer below keeps running, so downstream sees
                        // one continuous transport stream with a gap, not a new one (the generic input restart
                        // would reset the muxer: new PAT/PMT, continuity counters and PCR).
                        struct Reconnect {
                            std::mutex             mutex;
                            bool                   pending = false;
                            SingleInput           *input;
                            GWeakRef               source;
                            GWeakRef               demuxer;
                            std::function<void()>  schedule;

                            ~Reconnect() {
                                g_weak_ref_clear(&this->source);
                                g_weak_ref_clear(&this->demuxer);
                            }
                        };

                        auto reconnect = std::make_shared<Reconnect>();

                        reconnect->input = this;

                        g_weak_ref_init(&reconnect->source,  (*src)->get());
                        g_weak_ref_init(&reconnect->demuxer, (*flv)->get());

                        reconnect->schedule = [weak = std::weak_ptr<Reconnect>(reconnect)]() {
                            auto self = weak.lock();

                            if (! self) {
                                return;
                            }

                            {
                                std::lock_guard lock(self->mutex);

                                if (self->pending) {
                                    return;
                                }

                                self->pending = true;
                            }

                            auto delay = self->input->_calculateBackoffDelay(self->input->_srcRetryAttempt++);

                            g_timeout_add_seconds_full(
                                G_PRIORITY_DEFAULT,
                                delay,
                                [](gpointer data) -> gboolean {
                                    auto &self = *static_cast<std::shared_ptr<Reconnect> *>(data);

                                    {
                                        std::lock_guard lock(self->mutex);

                                        self->pending = false;
                                    }

                                    // The input may have been torn down while the timer was pending.
                                    auto *source  = GST_ELEMENT(g_weak_ref_get(&self->source));
                                    auto *demuxer = GST_ELEMENT(g_weak_ref_get(&self->demuxer));

                                    if (source && demuxer) {
                                        gst_element_set_state(source,  GST_STATE_NULL);
                                        gst_element_set_state(demuxer, GST_STATE_NULL);

                                        gst_element_sync_state_with_parent(demuxer);
                                        gst_element_sync_state_with_parent(source);
                                    }

                                    if (source) {
                                        gst_object_unref(source);
                                    }

                                    if (demuxer) {
                                        gst_object_unref(demuxer);
                                    }

                                    return G_SOURCE_REMOVE;
                                },
                                new std::shared_ptr<Reconnect>(self),
                                [](gpointer data) {
                                    delete static_cast<std::shared_ptr<Reconnect> *>(data);
                                }
                            );
                        };

                        struct BusContext {
                            std::shared_ptr<Reconnect> reconnect;
                            GstBus                    *parentBus;
                            std::string                inputName;
                        };

                        GstBus *bus = gst_bus_new();

                        gst_bus_set_sync_handler(
                            bus,
                            [](GstBus *, GstMessage *msg, gpointer data) -> GstBusSyncReply {
                                auto *context = static_cast<BusContext *>(data);

                                if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                                    GError *error = nullptr;

                                    gst_message_parse_error(msg, &error, nullptr);

                                    WARN(("RTMP input '{}' is not receiving ({}) - reconnecting", context->inputName, error ? error->message : "unknown error"));

                                    if (error) {
                                        g_error_free(error);
                                    }

                                    context->reconnect->schedule();

                                } else {
                                    gst_bus_post(context->parentBus, gst_message_ref(msg));
                                }

                                return GST_BUS_DROP;
                            },
                            new BusContext { reconnect, GST_BIN((*srcBin)->get())->child_bus, this->getName() },
                            [](gpointer data) {
                                delete static_cast<BusContext *>(data);
                            }
                        );

                        gst_element_set_bus((*src)->get(), bus);
                        gst_object_unref(bus);

                        // A publisher that unpublishes cleanly sends EOS, which would end the channel.
                        src->getPad("src")->event(true, [reconnect](GstEvent *event, bool &discard) {
                            if (GST_EVENT_TYPE(event) == GST_EVENT_EOS) {
                                discard = true;

                                WARN(("RTMP publisher ended the stream - reconnecting"));

                                reconnect->schedule();
                            }

                            return true;
                        });
                    }

                    src->link(flv);

                    // mpegtsmux announces only the streams it has at its first output, and decodebin then
                    // treats that programme as complete: a muxer started on audio alone would leave the
                    // channel without video. Hold every demuxed stream until flvdemux has announced all of
                    // them, then release them together.
                    struct StreamGate {
                        std::mutex                                          mutex;
                        bool                                                open = false;
                        std::vector<std::pair<pipeline::PadPtr, guint>>     held;
                    };

                    auto gate    = std::make_shared<StreamGate>();
                    auto release = [gate]() {
                        std::lock_guard lock(gate->mutex);

                        if (! gate->open) {
                            gate->open = true;

                            for (auto &held : gate->held) {
                                held.first->unblock(held.second);
                            }

                            gate->held.clear();
                        }
                    };

                    flv->onAllPads([release]() {
                        release();

                        return false;
                    });

                    // FLV carries its header once per connection, so the shared decodebin cannot survive
                    // a source restart the way it does for UDP/SRT (a second FLV header is parsed as
                    // garbage). Re-mux to MPEG-TS inside this bin instead: restarting the bin resets every
                    // stateful element, and decodebin only ever sees a TS stream.
                    // Every (re)connection restarts the source bin, and the new session's timestamps start
                    // over from whatever the publisher sends. Downstream sees one continuous programme, so
                    // shift each session onto the pipeline clock: the session's first buffer lands at the
                    // current running time. One offset per session, shared by all its streams, keeps A/V
                    // sync; flvdemux drops its pads when the session ends, which starts a new one.
                    struct Timeline {
                        std::mutex mutex;
                        bool       known  = false;
                        gint64     offset = 0;
                    };

                    auto timeline = std::make_shared<Timeline>();

                    flv->onPadRemoved([timeline](pipeline::PadPtr &) {
                        std::lock_guard lock(timeline->mutex);

                        timeline->known = false;

                        return true;
                    });

                    flv->onPadAdded([srcBin, mux, gate, release, timeline](pipeline::PadPtr &pad) {
                        const std::string caps = pad->getCaps().toString();

                        std::string media;
                        std::string parserFactory;

                        if (caps.find("video/x-h264") != std::string::npos) {
                            media         = "video";
                            parserFactory = "h264parse";

                        } else if (caps.find("mpegversion=(int)4") != std::string::npos) {
                            media         = "audio";
                            parserFactory = "aacparse";

                        } else if (caps.find("mpegversion=(int)1") != std::string::npos) {
                            media         = "audio";
                            parserFactory = "mpegaudioparse";

                        } else {
                            WARN(("RTMP input carries an unsupported stream ({}) - only H.264 with AAC or MP3 is supported", caps));

                            return true;
                        }

                        // The bin is restarted on reconnect and this callback fires again: reuse the
                        // queue, parser and mux pad rather than piling up new ones.
                        auto queue = srcBin->get("rtmp-" + media + "-queue");

                        if (! queue) {
                            // The queue decouples the demuxer's single streaming thread from the muxer.
                            // The muxer holds audio until the first keyframe arrives, and a publisher that
                            // joins mid-GOP is up to a keyframe interval away from one: a queue that fills
                            // sooner blocks the demuxer on audio, so it never parses that keyframe.
                            queue = srcBin->add(pipeline::Element::make("rtmp-" + media + "-queue", "queue"));

                            auto parser  = srcBin->add(pipeline::Element::make("rtmp-" + media + "-parse", parserFactory));
                            auto muxNode = mux;

                            queue->setProperty<gboolean>("silent", true);
                            queue->setProperty<guint>   ("max-size-buffers", 0);
                            queue->setProperty<guint>   ("max-size-bytes",   0);
                            queue->setProperty<guint64> ("max-size-time",    RTMP_DEMUX_QUEUE_MAX_TIME);

                            queue->link(parser);
                            parser->link(muxNode);

                            queue->syncWithParent();
                            parser->syncWithParent();

                            std::lock_guard lock(gate->mutex);

                            if (! gate->open) {
                                auto queueSrc = queue->getPad("src");

                                gate->held.emplace_back(queueSrc, queueSrc->block(true));

                                // A publisher with a single stream never announces a second one.
                                if (gate->held.size() == 1) {
                                    g_timeout_add_seconds_full(
                                        G_PRIORITY_DEFAULT,
                                        RTMP_STREAM_DISCOVERY_TIMEOUT,
                                        [](gpointer data) -> gboolean {
                                            (*static_cast<std::function<void()> *>(data))();

                                            return G_SOURCE_REMOVE;
                                        },
                                        new std::function<void()>(release),
                                        [](gpointer data) {
                                            delete static_cast<std::function<void()> *>(data);
                                        }
                                    );
                                }
                            }
                        }

                        pad->link(queue->getPad("sink"));

                        // The offset goes on the queue's output, which this session's first buffer has not
                        // reached yet when it is computed here.
                        {
                            struct PadTimeline {
                                std::shared_ptr<Timeline> timeline;
                                GstPad                   *target;
                                GstSegment                segment;
                                bool                      haveSegment = false;
                            };

                            auto *context = new PadTimeline { timeline, (*queue->getPad("src"))->get() };

                            gst_segment_init(&context->segment, GST_FORMAT_UNDEFINED);

                            gst_pad_add_probe(
                                (*pad)->get(),
                                (GstPadProbeType) (GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
                                [](GstPad *srcPad, GstPadProbeInfo *info, gpointer data) -> GstPadProbeReturn {
                                    auto *context = static_cast<PadTimeline *>(data);

                                    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
                                        GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);

                                        if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT) {
                                            gst_event_copy_segment(event, &context->segment);

                                            context->haveSegment = context->segment.format == GST_FORMAT_TIME;
                                        }

                                        return GST_PAD_PROBE_OK;
                                    }

                                    GstBuffer   *buffer    = GST_PAD_PROBE_INFO_BUFFER(info);
                                    GstClockTime timestamp = GST_BUFFER_DTS_OR_PTS(buffer);

                                    if (! context->haveSegment || ! GST_CLOCK_TIME_IS_VALID(timestamp)) {
                                        return GST_PAD_PROBE_OK;
                                    }

                                    {
                                        std::lock_guard lock(context->timeline->mutex);

                                        if (! context->timeline->known) {
                                            GstElement  *demuxer = GST_PAD_PARENT(srcPad);
                                            GstClockTime now     = gst_element_get_current_running_time(demuxer);
                                            GstClockTime at      = gst_segment_to_running_time(&context->segment, GST_FORMAT_TIME, timestamp);

                                            if (! GST_CLOCK_TIME_IS_VALID(now) || ! GST_CLOCK_TIME_IS_VALID(at)) {
                                                return GST_PAD_PROBE_OK;
                                            }

                                            context->timeline->offset = GST_CLOCK_DIFF(at, now);
                                            context->timeline->known  = true;
                                        }

                                        gst_pad_set_offset(context->target, context->timeline->offset);
                                    }

                                    return GST_PAD_PROBE_REMOVE;
                                },
                                context,
                                [](gpointer data) {
                                    delete static_cast<PadTimeline *>(data);
                                }
                            );
                        }

                        return true;
                    });

                    mux->link(queue);

                    // queue
                    queue->setProperty<gboolean>("silent", true);
                    queue->setProperty<guint>   ("max-size-buffers", 0);
                    queue->setProperty<guint>   ("max-size-bytes",   0);
                    queue->setProperty<guint64> ("max-size-time",    RTMP_SOURCE_QUEUE_MAX_TIME);

                    // Present the remuxed stream exactly as udpsrc and srtsrc present theirs: TS buffers
                    // stamped with their arrival time. tsdemux derives its clock mapping from those stamps,
                    // which is what lets it ride over the PCR jump at a reconnect ("reset skew") instead of
                    // stalling the programme.
                    queue->getPad("src")->addProbe({ GST_PAD_PROBE_TYPE_BUFFER, GST_PAD_PROBE_TYPE_BUFFER_LIST }, [rawQueue = (*queue)->get()](GstPadProbeInfo *info) {
                        GstClockTime now = gst_element_get_current_running_time(rawQueue);

                        if (! GST_CLOCK_TIME_IS_VALID(now)) {
                            return true;
                        }

                        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER_LIST) {
                            GstBufferList *list = gst_buffer_list_make_writable(GST_PAD_PROBE_INFO_BUFFER_LIST(info));

                            gst_buffer_list_foreach(list, [](GstBuffer **buffer, guint, gpointer data) -> gboolean {
                                *buffer = gst_buffer_make_writable(*buffer);

                                GST_BUFFER_PTS(*buffer) = *static_cast<GstClockTime *>(data);
                                GST_BUFFER_DTS(*buffer) = GST_CLOCK_TIME_NONE;

                                return TRUE;
                            }, &now);

                            GST_PAD_PROBE_INFO_DATA(info) = list;

                        } else {
                            GstBuffer *buffer = gst_buffer_make_writable(GST_PAD_PROBE_INFO_BUFFER(info));

                            GST_BUFFER_PTS(buffer) = now;
                            GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;

                            GST_PAD_PROBE_INFO_DATA(info) = buffer;
                        }

                        return true;
                    });

                    // Arrival stamps make this bin a live source, but rtmp2src answers the latency query
                    // as non-live, and the live elements downstream do not count latency from a non-live
                    // branch (tsdemux's 700 ms included). Decoding would then get no time at all: every
                    // video frame reaches the live fallbackswitch after its deadline and the programme
                    // shows the black fallback.
                    queue->getPad("src")->addProbe({ GST_PAD_PROBE_TYPE_QUERY_UPSTREAM }, [](GstPadProbeInfo *info) {
                        GstQuery *query = GST_PAD_PROBE_INFO_QUERY(info);

                        if ((GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_PULL) && GST_QUERY_TYPE(query) == GST_QUERY_LATENCY) {
                            GstClockTime minLatency;
                            GstClockTime maxLatency;

                            gst_query_parse_latency(query, nullptr, &minLatency, &maxLatency);
                            gst_query_set_latency(query, TRUE, minLatency, maxLatency);
                        }

                        return true;
                    });

                    nodeSrc     = queue;
                    nodeDecoder = this->_createDemuxer();

                    this->_isLive = true;

                    this->_srcStreamsCount  = 1;
                    this->_srcHasAllStreams = true;
                    this->_protocol         = Protocol::RTMP;

                } else if (
                    (url->scheme_id() == urls::scheme::http || url->scheme_id() == urls::scheme::https) &&
                    (parameters.getTransportParameters(Protocol::DASH) != nullptr || _isDashManifestPath(url->path()))
                ) {
                    static const DashInputTransportParameters defaultDashParameters;

                    const auto *dashParameters = reinterpret_cast<const DashInputTransportParameters *>(parameters.getTransportParameters(Protocol::DASH));

                    if (dashParameters == nullptr) {
                        dashParameters = &defaultDashParameters;
                    }

                    {
                        auto http = srcBin->add(pipeline::Element::make("souphttpsrc"));

                        http->setProperty<gboolean>("is-live", true);
                        http->setProperty<const gchar *>("location", uriString.c_str());
                        http->setProperty<gint>("retries", -1);

                        // dashdemux2 posts a stream collection and refuses to start ("requires a streams-aware
                        // context") unless its parent declares it will handle one. Nothing here sends a
                        // select-streams event, so the demuxer keeps its default selection: one video and one
                        // audio track, the MPD's main/default ones where marked.
                        GST_OBJECT_FLAG_SET((*srcBin)->get(), GST_BIN_FLAG_STREAMS_AWARE);

                        nodeSrc = srcBin->add(pipeline::Element::make("dashdemux2"));

                        // A fixed connection bitrate replaces the throughput estimate, so the representation is
                        // chosen once (the highest one within the bitrate) and never switched mid-stream. A switch
                        // would change resolution or codec under a passthrough output.
                        nodeSrc->setProperty<guint>("connection-bitrate", (guint) dashParameters->getBandwidth());
                        nodeSrc->setProperty<gfloat>("bandwidth-target-ratio", 1.0f);

                        if (dashParameters->getPresentationDelay() > 0) {
                            auto delay = std::to_string(dashParameters->getPresentationDelay()) + "ms";

                            nodeSrc->setProperty<const gchar *>("presentation-delay", delay.c_str());
                        }

                        this->_linkAdaptiveDemuxer(srcBin, nodeSrc, false, true);

                        if (dashParameters->getScteParameters().getSource() == ScteSource::SEGMENTS) {
                            this->_adaptiveDemuxer = nodeSrc;

                            srcBin->onSignal("deep-element-added", G_CALLBACK(_onDashElementAdded), this);
                        }

                        http->link(nodeSrc);
                    }

                    this->_isLive   = true;
                    this->_protocol = Protocol::DASH;

                } else if (
                    url->scheme_id() == urls::scheme::http ||
                    url->scheme_id() == urls::scheme::https
                ) {
                    Credentials creds;
                    std::size_t bandwith = HlsInputTransportParameters::MAX_BANDWITH;
        
                    const auto *hlsParameters = reinterpret_cast<const HlsInputTransportParameters *>(parameters.getTransportParameters(Protocol::HLS));

                    if (hlsParameters != nullptr) {
                        creds = hlsParameters->getCredentials();
        
                        if (
                            hlsParameters->getBandwith() > HlsInputTransportParameters::MIN_BANDWITH &&
                            hlsParameters->getBandwith() <= HlsInputTransportParameters::MAX_BANDWITH
                        ) {
                            bandwith = hlsParameters->getBandwith();
                        }
                    }
        
                    // Convert to kb/s
                    bandwith = bandwith / 1000;
                    
                    {
                        pipeline::ElementPtr http;
        
                        {
                            http = srcBin->add(pipeline::Element::make("souphttpsrc"));
                            
                            http->setProperty<gboolean>("is-live", true);
                            http->setProperty<const gchar *>("location", uriString.c_str());
                            http->setProperty<gint>("retries", -1);
                            
                            if (! creds.empty()) {
                                http->setProperty<const gchar *>("user-id", creds.getUsername().c_str());
                                http->setProperty<const gchar *>("user-pw", creds.getPassword().c_str());
                            }
                        }
                        
                        {
                            bool filterScteFromSegments;
                            
                            {
                                ScteSource source;

                                if (hlsParameters) {
                                    source = hlsParameters->getScteParameters().getSource();

                                } else {
                                    source = HlsInputTransportParameters().getScteParameters().getSource();
                                }

                                filterScteFromSegments = source != ScteSource::SEGMENTS;
                            }
                            
                            nodeSrc = srcBin->add(pipeline::Element::make("hlsdemux"));
                           
                            nodeSrc->setProperty<guint>("connection-speed", bandwith);
        
                            this->_linkAdaptiveDemuxer(srcBin, nodeSrc, filterScteFromSegments);

                            if (filterScteFromSegments) {
                                nodeSrc->onSignal("update-variant-manifest", G_CALLBACK(_onPlaylistUpdate), this);
                            }
                        }
                        
                        http->link(nodeSrc);
                    }
        
                    this->_isLive   = true;
                    this->_protocol = Protocol::HTTP;
            
                } else {
                    throw InvalidArgumentException("It is not possible to determine protocol for URI: '" + uriString + "'");
                }
            }
        }

        
        if (this->_isLive) {
            this->_buffer = this->_bin->add(pipeline::Element::make("multiqueue"));

            this->_buffer->setProperty<guint>("max-size-bytes", 0);

            // SRT already has a latency-controlled retransmission/jitter buffer. Waiting for the
            // generic three-second live-input prebuffer adds startup and recovery delay without
            // adding useful SRT resilience. Keep a small multiqueue to decouple discovered A/V
            // streams, but let the first complete buffers flow immediately.
            //
            // DASH likewise: dashdemux2 already holds the presentation delay (10 s by default) and
            // delivers whole segments in bursts, which made the three-second level flap between
            // underrun and overrun every segment (measured: every 4 s with 2 s segments).
            //
            // MXL likewise: the flows are written in real time, so there is nothing to prebuffer, and waiting
            // would only add delay. The queue is bounded by time, not by buffers, because the buffers differ
            // in size by a factor of thousands (a 1 ms audio chunk and a raw frame).
            if (this->_protocol == Protocol::MXL) {
                this->_buffer->setProperty<guint64> ("max-size-time",    MXL_STREAM_QUEUE_MAX_TIME);
                this->_buffer->setProperty<guint>   ("max-size-buffers", 0);

            } else if (this->_useBuffering && this->_protocol != Protocol::SRT && this->_protocol != Protocol::DASH) {
                this->_buffer->setProperty<guint64> ("max-size-time",    3 * GST_SECOND);
                this->_buffer->setProperty<guint>   ("max-size-buffers", 0);

                this->_buffer->onSignal("underrun", G_CALLBACK(_onBufferUnderrun), this);
                this->_buffer->onSignal("overrun",  G_CALLBACK(_onBufferOverrun),  this);

            } else {
                this->_buffer->setProperty<guint64> ("max-size-time",    0);
                this->_buffer->setProperty<guint>   ("max-size-buffers", SRT_STREAM_QUEUE_MAX_BUFFERS);
            }
        }
    }

    if (nodeDecoder) {
        
        nodeSrc->link(nodeDecoder);

        for (auto &pad : nodeSrc->getPads(true)) {
            pad->addProbe(
                { GST_PAD_PROBE_TYPE_BUFFER },
                [this](GstPadProbeInfo *info) {
                    this->_clearRetry();

                    return false;
                }
            );
        }
    }

    this->_bin->onPlay([this]() {
        
        for (auto &stream : this->_streams) {
            std::static_pointer_cast<SingleInputStream>(stream)->unblock();
        }

        return false;
    });

    this->_bin->onNull([]() {
        return false;
    });
}

void transcoder::SingleInput::_onBufferUnderrun(GstElement *buffer, SingleInput *input) {
    std::lock_guard lock(input->_bufferMutex);
    
    logger.log("Input is running out of data, buffering required - blocking data flow until sufficient buffer is available.");

    input->_blockBuffer(true);

    if (input->_state != InputState::BUFFERING) {
        input->_state = InputState::BUFFERING;

        if (input->_stateChangeCallback) {
            input->_stateChangeCallback(input->_state);
        }
    }
}

void transcoder::SingleInput::_onBufferOverrun(GstElement *buffer, SingleInput *input) {
    std::lock_guard lock(input->_bufferMutex);

    if (input->_state != InputState::BUFFERED) {
        logger.log("Input fully buffered, resuming data flow.");

        input->_state = InputState::BUFFERED;

        if (input->_stateChangeCallback) {
            input->_stateChangeCallback(input->_state);
        }
    }

    input->_blockBuffer(false);
}

void transcoder::SingleInput::_blockBuffer(bool block) {
    std::lock_guard lock(this->_bufferMutex);

    if (this->_buffer) {
        for (const auto &pad : this->_buffer->getPads(true)) {
            auto it = this->_bufferBlockId.find(pad.get());

            if (block) {
                if (it == this->_bufferBlockId.end()) {
                    this->_bufferBlockId[pad.get()] = pad->block(true);
                }

            } else {
                if (it != this->_bufferBlockId.end()) {
                    pad->unblock(it->second);

                    this->_bufferBlockId.erase(it);
                }
            }
        }
    }
}

pipeline::ElementPtr transcoder::SingleInput::_createDemuxer(bool filterAllScteEvents) {
    auto decoder = this->_bin->add(pipeline::Element::make("demuxer-" + std::to_string(this->_id.getInt()) + "-" + std::to_string(utils::AutoGeneratedId().getInt()), "decodebin"));

    // The demuxer's queues must hold one interleaving chunk of the heaviest stream. decodebin's automatic limits
    // (2 MB, 5 buffers) are smaller than e.g. 0.4 s of 50 Mbit/s video in an MP4, so the demuxer blocks on video
    // before it reaches the matching audio; on a clock-paced output that audio then arrives too late.
    decoder->setProperty<guint64>("max-size-time",    DEMUXER_QUEUE_MAX_TIME);
    decoder->setProperty<guint>  ("max-size-bytes",   DEMUXER_QUEUE_MAX_BYTES);
    decoder->setProperty<guint>  ("max-size-buffers", DEMUXER_QUEUE_MAX_BUFFERS);
    
    
    decoder->onSignal("autoplug-continue", G_CALLBACK(_autoPlugContinueCallback), this);
    decoder->onSignal("autoplug-select",   G_CALLBACK(_autoPlugSelectCallback),   this);
    decoder->onSignal("element-added",     G_CALLBACK(_demuxerElementAdded),      this);
    
    decoder->onPadAdded([this, decoder](pipeline::PadPtr &pad) {
        auto caps = pad->getCaps();

        DBG(("Added pad {} caps: {}", pad->getName(), caps.toString()));

        // Create stream
        {
            auto srcPad = pad;

            if (this->_buffer) {
                std::lock_guard lock(this->_bufferMutex);

                auto sinkPad = this->_buffer->requestPad("sink_%u");

                pad->link(sinkPad);

                srcPad = this->_buffer->getPad("src" + sinkPad->getName().substr(4));

                if (this->_state == InputState::BUFFERING) {
                    this->_blockBuffer(true);
                }
            }

            {
                std::unique_lock lock(this->_streamsGuard);

                // A program update that changes a stream type makes the demuxer expose a new group of pads for
                // the same streams (same PID, so the same stream id). Keep the stream the channel is attached to.
                std::shared_ptr<SingleInputStream> existing;

                auto streamId = _padStreamId(pad);
                if (! streamId.empty()) {
                    for (auto &stream : this->_streams) {
                        auto single = std::static_pointer_cast<SingleInputStream>(stream);

                        if (single->getStreamId() == streamId) {
                            existing = single;
                            break;
                        }
                    }
                }

                if (existing) {
                    existing->replacePads(pad, srcPad);

                } else {
                    auto stream = std::make_shared<SingleInputStream>(pad, srcPad, this->_bin);

                    stream->setMxlTap(this->_inputParameters.getMxlTap());

                    this->_streams.push_back(stream);
                }
            }            
        }
        
        return true;
    });

    // This callback is executed when the element is fully configured/prerolled. In the case of a decodebin element,
    // all parsers and demuxers are allocated at this point, and their pads are exposed via ghost pads.
    //
    // The MPEG-TS demultiplexer does not report SCTE sections downstream by default. We need to enable this feature
    // directly on the demuxer. By default, an SCTE-35 section is pushed to all demultiplexer pads, so we need to 
    // attach an event handler to only one pad to avoid repeated events.
    decoder->onAllPads([this, decoder, filterAllScteEvents]() {
        {
            gboolean done = false;

            {
                GstQuery *query = gst_query_new_duration(GST_FORMAT_TIME);    
                if (gst_element_query((*decoder)->get(), query)) {
                    gint64 duration;
                    gst_query_parse_duration(query, nullptr, &duration);
                    
                    if (duration != -1) {
                        this->_duration = duration;
                        logger.log("Stream duration from demuxer: {} seconds", duration / GST_SECOND);
                    }
                }

                gst_query_unref(query);
            }

            GstIterator *it = gst_bin_iterate_elements(GST_BIN((*decoder)->get()));
            while (! done) {
                GValue item = G_VALUE_INIT;

                switch (gst_iterator_next (it, &item)) {
                    case GST_ITERATOR_OK:
                        {
                            auto *element = GST_ELEMENT(g_value_get_object(&item));
                            auto *factory = gst_element_get_factory(element);

                            if (strcmp(GST_ELEMENT_NAME(factory), "tsdemux") == 0) {
                                pipeline::Element demux(pipeline::GstElementPtr::take(element));

                                bool callbackAssigned = false;

                                // FIXME: In case of HLS there might be 1 demuxer per media type -> separated audio and video playlists
                                //
                                // All source pads should be filtered from SCTE events.
                                // In other case events will be forwarded by pipeline to the muxer.
                                for (auto &stream : this->_streams) {
                                    if (
                                        filterAllScteEvents ||
                                        callbackAssigned ||
                                        (
                                            ! stream->getStreamParameters().hasVideoParameters() &&
                                            ! stream->getStreamParameters().hasAudioParameters()
                                        )
                                    ) {
                                        stream->peekPad(true)->event(true, [this](GstEvent *event, bool &discard) {
                                            if (GST_EVENT_TYPE(event) == GST_EVENT_CUSTOM_DOWNSTREAM) {
                                                const auto *structure = gst_event_get_structure(event);

                                                if (strcmp(gst_structure_get_name(structure), "scte-sit") == 0) {
                                                    discard = true;
                                                }
                                            }

                                            return true;
                                        });

                                    } else {
                                        stream->peekPad(true)->event(true, [this, element](GstEvent *event, bool &discard) {
                                            if (GST_EVENT_TYPE(event) == GST_EVENT_CUSTOM_DOWNSTREAM) {
                                                const auto *structure = gst_event_get_structure(event);

                                                if (strcmp(gst_structure_get_name(structure), "scte-sit") == 0) {
                                                    discard = true;

                                                    if (this->_scteDataCallback) {
                                                        GstStructure     *rtimeMap = nullptr;
                                                        GstMpegtsSection *section  = gst_event_parse_mpegts_section(event);

                                                        gst_structure_get(structure, "running-time-map", GST_TYPE_STRUCTURE, &rtimeMap, NULL);

                                                        {
                                                            Scte35Data data;

                                                            data.setSection(section);
                                                            data.setTimingMap(rtimeMap);
                                                            data.setRunningTime(gst_element_get_current_running_time(element));

                                                            // The demuxer's running-time-map does not always carry the key
                                                            // getSpliceTime() needs for this command type. Without it the event
                                                            // resolves to GST_CLOCK_TIME_NONE and is dropped silently further
                                                            // down, so synthesise the map from the acquisition running time.
                                                            // Only a fallback: an already-usable map is left untouched.
                                                            if (data.getSpliceTime() == GST_CLOCK_TIME_NONE) {
                                                                WARN((
                                                                    "SCTE-35 section carried no usable splice time; "
                                                                    "falling back to the acquisition running time"
                                                                ));

                                                                data.setTimingMap(
                                                                    scheduler::buildRunningTimeMap(section, data.getRunningTime())
                                                                );
                                                            }

                                                            DBG(("Acquired SCTE-35 section: {}", toBase64(data)));

                                                            this->_scteDataCallback(data);
                                                        }

                                                        if (section) {
                                                            gst_mpegts_section_unref(section);
                                                        }
                                                    }
                                                }
                                            }

                                            return true;
                                        });

                                        callbackAssigned = true;
                                    }
                                }

                                // Enable SCTE events forwarding trough pads
                                demux.setProperty<gboolean>("send-scte35-events", true);
                            }

                            g_value_reset(&item);
                        }
                        break;

                    case GST_ITERATOR_RESYNC:
                        gst_iterator_resync (it);
                        done = false;
                        break;

                    case GST_ITERATOR_DONE:
                    case GST_ITERATOR_ERROR:
                    default:
                        done = true;
                        break;
                }

                g_value_unset(&item);
            }

            gst_iterator_free (it);
        }

        {
            std::lock_guard lock(this->_srcStreamsGuard);

            this->_srcDemuxersCount++;

            if (this->_srcHasAllStreams && (this->_srcStreamsCount == this->_srcDemuxersCount)) {
                DBG(("All sources collected as well as all decoders configured ({}/{})", this->_srcStreamsCount, this->_srcDemuxersCount));

                if (this->_probeCallback) {
                    this->_probeCallback(true);
                }
            }
        }
        
        // An MXL input has a queue (to decouple its readers) but no prebuffering, so it is ready as soon as its
        // flows are attached
        if (! this->_buffer || this->_protocol == Protocol::MXL) {
            if (this->_protocol == Protocol::MXL) {
                this->_state = InputState::BUFFERED;
            }

            if (this->_stateChangeCallback) {
                this->_stateChangeCallback(InputState::BUFFERED);
            }
        }

        return false;
    });

    return decoder;
}

struct transcoder::SingleInput::MxlWatch {
    // Where the flows' time falls on the channel's. A flow's buffers are stamped with the time of the picture or
    // the sound, as the writer took it, and the writer's own pipeline has delayed them since (a programme taken
    // behind a second of processing arrives a second old). A channel works on live time, which is the time at
    // which things arrive, and would take such a buffer for late and drop it. So the first buffer to arrive sets
    // one shift, which is the same for every flow of the input and so keeps them in step with each other, and
    // puts it on live time plus a margin. It holds until the writer has been gone and comes back.
    struct Timeline {
        std::mutex          guard;
        std::atomic<bool>   known { false };
        std::atomic<int>    generation { 0 };
        std::atomic<gint64> shift { 0 };
    };

    struct Flow {
        ~Flow() {
            if (this->target) {
                gst_object_unref(this->target);
            }
        }

        std::string                kind;
        pipeline::ElementPtr       src;
        GstPad                    *target = nullptr;       // where the shift is applied: the output of the queue behind the reader
        std::shared_ptr<Timeline>  timeline;
        GstSegment                 segment;
        bool                       haveSegment = false;
        int                        applied     = 0;        // the generation of the timeline last applied to the target

        std::atomic<gint64>        lastData { 0 };         // g_get_monotonic_time() of the last buffer, in microseconds
        gint64                     reattachedAt = 0;       // of the last reattach
        int                        reattached   = 0;       // reattaches since the last buffer
    };

    std::mutex                         guard;
    SingleInput                       *input = nullptr;       // emptied by _stopMxlWatch() before the input goes
    std::shared_ptr<Timeline>          timeline = std::make_shared<Timeline>();
    std::vector<std::shared_ptr<Flow>> flows;
};

void transcoder::SingleInput::_createMxlFlows(pipeline::BinPtr &srcBin, const std::string &domain, const MxlInputTransportParameters &parameters) {
    this->_mxlWatch        = std::make_shared<MxlWatch>();
    this->_mxlWatch->input = this;

    auto addFlow = [&](const char *kind, const char *property, const std::string &flowId, bool video) {
        auto src = srcBin->add(pipeline::Element::make(std::string("mxl-") + kind + "-src", "mxlsrc"));

        src->setProperty<const gchar *>("domain", domain.c_str());
        src->setProperty<const gchar *>(property, flowId.c_str());

        // mxlsrc holds its state lock while it pushes the caps of its flow downstream. The elements behind it
        // answer that with queries to what is further on, and the pads there are blocked until the input plays;
        // the change to playing asks mxlsrc for its latency, which needs the same lock. So the reader would wait
        // for the state change and the state change for the reader (measured: the channel never starts). The
        // queue ends the reader's thread at once. It drops the oldest buffers rather than block the reader, which
        // must keep up with the ring of its flow in any case.
        auto queue = srcBin->add(pipeline::Element::make("queue"));

        queue->setProperty<gboolean>("silent",           true);
        queue->setProperty<guint>   ("max-size-bytes",   0);
        queue->setProperty<guint>   ("max-size-buffers", video ? 3 : 0);
        queue->setProperty<guint64> ("max-size-time",    video ? 0 : MXL_STREAM_QUEUE_MAX_TIME);

        gst_util_set_object_arg(G_OBJECT((*queue)->get()), "leaky", "downstream");

        src->link(queue);

        pipeline::ElementPtr last = queue;

        if (video) {
            // The flow is v210 (4:2:2, 10 bits). The channel's canvas is BGRA, and v210 to BGRA costs ten times
            // what I420 to BGRA does (7.7 ms against 0.8 + 0.6 ms for a 1080p frame), which is what a decoded
            // H.264 source arrives as. So the picture is brought to I420 here, once.
            auto convert = srcBin->add(pipeline::Element::make("videoconvert"));
            auto filter  = srcBin->add(pipeline::Element::make("capsfilter"));

            {
                GstCaps *caps = gst_caps_from_string("video/x-raw,format=I420");

                filter->setProperty<GstCaps *>("caps", caps);

                gst_caps_unref(caps);
            }

            last->link(convert);
            convert->link(filter);

            last = filter;

        } else {
            auto split = srcBin->add(pipeline::Element::make("audiobuffersplit"));

            gst_util_set_object_arg(G_OBJECT((*split)->get()), "output-buffer-duration", MXL_AUDIO_CHUNK);

            last->link(split);

            last = split;
        }

        auto demuxer = this->_createDemuxer();

        last->link(demuxer);

        auto flow = std::make_shared<MxlWatch::Flow>();

        flow->kind      = kind;
        flow->src       = src;
        flow->target    = GST_PAD((*queue->getPad("src"))->get());
        flow->timeline  = this->_mxlWatch->timeline;
        flow->lastData  = g_get_monotonic_time();

        gst_object_ref(flow->target);
        gst_segment_init(&flow->segment, GST_FORMAT_UNDEFINED);

        gst_pad_add_probe(
            (*src->getPad("src"))->get(),
            (GstPadProbeType) (GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
            +[](GstPad *srcPad, GstPadProbeInfo *info, gpointer data) -> GstPadProbeReturn {
                auto &flow = **static_cast<std::shared_ptr<MxlWatch::Flow> *>(data);

                if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
                    GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);

                    if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT) {
                        gst_event_copy_segment(event, &flow.segment);

                        flow.haveSegment = flow.segment.format == GST_FORMAT_TIME;
                    }

                    return GST_PAD_PROBE_OK;
                }

                flow.lastData = g_get_monotonic_time();

                auto &timeline = *flow.timeline;

                if (! timeline.known) {
                    GstClockTime pts = GST_BUFFER_PTS(GST_PAD_PROBE_INFO_BUFFER(info));

                    if (! flow.haveSegment || ! GST_CLOCK_TIME_IS_VALID(pts)) {
                        return GST_PAD_PROBE_OK;
                    }

                    std::lock_guard lock(timeline.guard);

                    if (! timeline.known) {
                        GstClockTime now = gst_element_get_current_running_time(GST_ELEMENT(GST_PAD_PARENT(srcPad)));
                        GstClockTime at  = gst_segment_to_running_time(&flow.segment, GST_FORMAT_TIME, pts);

                        if (! GST_CLOCK_TIME_IS_VALID(now) || ! GST_CLOCK_TIME_IS_VALID(at)) {
                            return GST_PAD_PROBE_OK;
                        }

                        timeline.shift = GST_CLOCK_DIFF(at, now) + (gint64) MXL_INPUT_DELAY;
                        timeline.generation++;
                        timeline.known = true;
                    }
                }

                if (flow.applied != timeline.generation) {
                    flow.applied = timeline.generation;

                    gst_pad_set_offset(flow.target, timeline.shift);
                }

                return GST_PAD_PROBE_OK;
            },
            new std::shared_ptr<MxlWatch::Flow>(flow),
            +[](gpointer data) {
                delete static_cast<std::shared_ptr<MxlWatch::Flow> *>(data);
            }
        );

        src->getPad("src")->addProbe({ GST_PAD_PROBE_TYPE_BUFFER }, [this](GstPadProbeInfo *info) {
            this->_clearRetry();

            return false;
        });

        // The same lock is held while mxlsrc negotiates with the elements behind it, with an ALLOCATION query that
        // would wait in the same way. A reader has no use for a buffer pool, so the query is not sent on.
        gst_pad_add_probe(
            (*src->getPad("src"))->get(),
            GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM,
            +[](GstPad *, GstPadProbeInfo *info, gpointer) -> GstPadProbeReturn {
                if (GST_QUERY_TYPE(GST_PAD_PROBE_INFO_QUERY(info)) == GST_QUERY_ALLOCATION) {
                    return GST_PAD_PROBE_DROP;
                }

                return GST_PAD_PROBE_OK;
            },
            nullptr,
            nullptr
        );

        this->_mxlWatch->flows.push_back(flow);
    };

    int flows = 0;

    if (! parameters.getVideoFlowId().empty()) {
        addFlow("video", "video-flow-id", parameters.getVideoFlowId(), true);

        flows++;
    }

    if (! parameters.getAudioFlowId().empty()) {
        addFlow("audio", "audio-flow-id", parameters.getAudioFlowId(), false);

        flows++;
    }

    this->_srcStreamsCount  = flows;
    this->_srcHasAllStreams = true;

    // A reader keeps waiting on a flow whose writer has gone: it does not end, fail, or notice that a new
    // writer has taken the flow's id. So a reader that has seen no data for a while is stopped and started
    // again, which attaches it to whatever the flow is now. This also covers a flow that is not there yet.
    this->_mxlWatchId = g_timeout_add_seconds_full(G_PRIORITY_DEFAULT, 1, +[](gpointer data) -> gboolean {
        auto &watch = *static_cast<std::shared_ptr<MxlWatch> *>(data);

        std::lock_guard lock(watch->guard);

        if (! watch->input) {
            return G_SOURCE_CONTINUE;
        }

        const gint64 now     = g_get_monotonic_time();
        const gint64 limit   = MXL_INPUT_IDLE_SECONDS * G_USEC_PER_SEC;
        const bool   playing = watch->input->_bin && GST_STATE((*watch->input->_bin)->get()) == GST_STATE_PLAYING;

        bool allQuiet = playing;

        for (auto &flow : watch->flows) {
            if (now - std::max<gint64>(flow->lastData, flow->reattachedAt) <= limit) {
                allQuiet = false;
            }
        }

        // With every flow silent the writer is gone, and the one that comes back may lag differently: its first
        // buffer sets the shift again
        if (allQuiet) {
            watch->timeline->known = false;
        }

        for (auto &flow : watch->flows) {
            // A reader that is not running (an input that is not on air yet) is not idle
            if (! playing) {
                flow->lastData = now;

                continue;
            }

            if (flow->lastData > flow->reattachedAt) {
                flow->reattached = 0;
            }

            if (now - std::max<gint64>(flow->lastData, flow->reattachedAt) > limit) {
                if (flow->reattached++ == 0) {
                    logger.log("MXL input '{}': no data on the {} flow for {} s; reattaching it", watch->input->getName(), flow->kind, MXL_INPUT_IDLE_SECONDS);

                } else {
                    DBG(("MXL input '{}': still no data on the {} flow; reattaching it again", watch->input->getName(), flow->kind));
                }

                flow->reattachedAt = now;

                GstElement *element = (*flow->src)->get();

                gst_element_set_state(element, GST_STATE_NULL);
                gst_element_sync_state_with_parent(element);
            }
        }

        return G_SOURCE_CONTINUE;
    }, new std::shared_ptr<MxlWatch>(this->_mxlWatch), +[](gpointer data) {
        delete static_cast<std::shared_ptr<MxlWatch> *>(data);
    });
}

void transcoder::SingleInput::_stopMxlWatch() {
    if (this->_mxlWatch) {
        {
            std::lock_guard lock(this->_mxlWatch->guard);

            this->_mxlWatch->input = nullptr;
        }

        this->_mxlWatch = nullptr;
    }

    if (this->_mxlWatchId) {
        g_source_remove(this->_mxlWatchId);

        this->_mxlWatchId = 0;
    }
}

void utpl::transcoder::SingleInput::_onPlaylistUpdate(GstElement *element, const gchar *buffer, gint64 currentSequence, GstClockTime currentPosition, SingleInput *input) {
    if (! input->_scteDataCallback || currentSequence < 0) {
        return;
    }

    
    {
        std::vector<HlsTag> segments;
        input->_getHlsTags(buffer, segments);

        double cueOffset = 0.0;
        GstClockTime currentSegmentDuration = 0;

        // clear previous tags with lower sequence number
        for (auto it = input->_hlsScteTags.begin(); it != input->_hlsScteTags.end();) {
            if (it->first < currentSequence) {
                it = input->_hlsScteTags.erase(it);
            } else {
                ++it;
            }
        }

        for (auto &segment : segments) {            
            // skip if the segment is in the past
            if (segment.sequence < currentSequence) {
                continue;
            }

            if (segment.sequence == currentSequence) {
                currentSegmentDuration = segment.duration * GST_SECOND;
            }
            
            if (segment.tag == HlsTagType::CUE_OUT || segment.tag == HlsTagType::CUE_IN) {
                // skip if we already processed this cue tag
                if (input->_hlsScteTags.find(segment.sequence) != input->_hlsScteTags.end()) {
                    continue;
                }

                input->_hlsScteTags[segment.sequence] = segment.tag;

                {
                    GstClockTime prerollTime = cueOffset * GST_SECOND;
                    GstClockTime duration    = segment.duration * GST_SECOND;
        
                    if (segment.tag == HlsTagType::CUE_OUT) {
                        input->_hlsScteEventId++;
                    }    

                    input->_processHlsScteEvent(prerollTime, duration, segment.tag == HlsTagType::CUE_OUT, currentPosition, currentSegmentDuration);
                }

            } else {
                if (segment.sequence > currentSequence) {
                    cueOffset += segment.duration;
                }
            }
        }
    }
}

void utpl::transcoder::SingleInput::_getHlsTags(const gchar *buffer, std::vector<HlsTag> &tags)
{
    std::istringstream stream(buffer);
    std::string line;
    int sequence = 0;
    bool foundFirstSegment = false;

    while (utils::getline(stream, line)) {
        // Get the first segment sequence number
        if (line.find("#EXT-X-MEDIA-SEQUENCE:") == 0) {
            sequence = std::stoll(line.substr(strlen("#EXT-X-MEDIA-SEQUENCE:")));
        }

        // Look for #EXTINF to get segment duration
        if (line.find("#EXTINF:") != std::string::npos) {
            if (foundFirstSegment) {
                sequence++;
            }

            foundFirstSegment = true;

            {
                size_t colonPos = line.find(":");
                size_t commaPos = line.find(",");
                if (colonPos != std::string::npos) {
                    double duration = std::stod(line.substr(colonPos + 1, commaPos - colonPos - 1));

                    tags.push_back({ sequence, duration, HlsTagType::EXTINF });
                }
            }
        }

        if (line.find("#EXT-X-CUE-OUT:") != std::string::npos) {
            double cueOutDuration = 0.0;
            // foundScteCue = true;

            // #EXT-X-CUE-OUT:DURATION=60.000
            // #EXT-X-CUE-OUT:DURATION="60.000"
            // #EXT-X-CUE-OUT:60.000
            // #EXT-X-CUE-OUT:"60.000"
            
            // Check if the line contains "DURATION="
            size_t durationPos = line.find("DURATION=");
            if (durationPos != std::string::npos) {
                // Extract the substring after "DURATION="
                size_t start = durationPos + strlen("DURATION=");
                size_t end = line.find_first_of(",\"", start); // Find the end of the number
                
                if (end == std::string::npos) {
                    end = line.length();
                }

                cueOutDuration = std::stod(line.substr(start, end - start));

            } else {
                // Handle formats without "DURATION="
                size_t colonPos = line.find(":");

                if (colonPos != std::string::npos) {
                    size_t start = colonPos + 1;
                    size_t end = line.find_first_of(",\"", start); // Find the end of the number
                
                    if (end == std::string::npos) {
                        end = line.length();
                    }
                
                    cueOutDuration = std::stod(line.substr(start, end - start));
                }
            }
            
            // FIXME: get rid of +1
            tags.push_back({ sequence + 1, cueOutDuration, HlsTagType::CUE_OUT });
        }

        if (line.find("#EXT-X-CUE-IN") != std::string::npos) {
            tags.push_back({ sequence + 1, 0.0, HlsTagType::CUE_IN });
        }
    }
}

void transcoder::SingleInput::_processHlsScteEvent(
    GstClockTime prerollTime,
    GstClockTime duration,
    GstClockTime currentPosition,
    GstClockTime currentSegmentDuration,
    bool cueOut
) {
    std::shared_ptr<utpl::transcoder::InputStream> targetStream;
    for (auto &stream : this->_streams) {
        if (
            stream->getStreamParameters().hasVideoParameters() ||
            stream->getStreamParameters().hasAudioParameters()
        ) {
            targetStream = stream;
            break;
        }
    }

    if (targetStream) {
        targetStream->peekPad(true)->probeCurrentPts([this, prerollTime, duration, cueOut, currentPosition, currentSegmentDuration](GstClockTime pts, GstClockTime gst) {
            Scte35Data data;
            GstMpegtsSection *gstSection = nullptr;

            GstClockTime positionInSegment = pts - currentPosition;
            GstClockTime offset = prerollTime + (currentSegmentDuration - positionInSegment);

            data.setRunningTime(gst);
            
            {
                auto *rtimeMap = gst_structure_new_empty("running-time-map");
                auto *sit = gst_mpegts_scte_sit_new();
                
                sit->splice_command_type = GST_MTS_SCTE_SPLICE_COMMAND_INSERT;
                
                {
                    GstMpegtsSCTESpliceEvent *event = gst_mpegts_scte_splice_event_new();
                    
                    event->splice_event_id          = this->_hlsScteEventId;
                    event->out_of_network_indicator = cueOut;
                    event->insert_event             = true;
                    
                    event->program_splice_time = GST_UTILS_GSTTIME_TO_MPEGTIME(pts + offset);
                    
                    if (event->program_splice_time) {
                        event->program_splice_time_specified = true;
                        event->program_splice_flag           = true;
                    }
                    
                    event->break_duration = cueOut ? GST_UTILS_GSTTIME_TO_MPEGTIME(duration) : 0;
                    if (event->break_duration) {
                        event->break_duration_auto_return = true;
                        event->duration_flag              = true;

                    } else {
                        event->break_duration_auto_return = false;
                        event->duration_flag              = false;
                    }
                    
                    // This field provides an identification for a specific avail within one
                    // unique_program_id. This value is expected to increment with each new avail within a viewing event. This
                    // value is expected to reset to one for the first avail in a new viewing event. This field is expected to
                    // increment for each new avail. It may optionally carry a zero value to indicate its non-usage.
                    event->avail_num = 0;

                    {
                        auto fieldName = "event-" + std::to_string(event->splice_event_id) + "-splice-time";
                        gst_structure_set(rtimeMap, fieldName.c_str(), G_TYPE_UINT64, gst + offset, nullptr);
                    }

                    g_ptr_array_add(sit->splices, event);
                }

                // get rid of is_running_time flag
                {
                    auto *tmpSection = gst_mpegts_section_from_scte_sit(sit, 0);

                    {
                        gsize data_size = 0;
                        gst_mpegts_section_packetize(tmpSection, &data_size);

                        auto *gbytes = gst_mpegts_section_get_data(tmpSection);
                        if (gbytes) {
                            auto *data = (guint8 *)g_bytes_unref_to_data(gbytes, &data_size);

                            gstSection = gst_mpegts_section_new(0, data, data_size);
                        }
                    }

                    gst_mpegts_section_unref(tmpSection);
                }

                if (rtimeMap) {
                    data.setTimingMap(rtimeMap);
                }
            }

            if (gstSection) {
                data.setSection(gstSection);

                this->_scteDataCallback(data);

                gst_mpegts_section_unref(gstSection);
            }
        });
    }
}

bool utpl::transcoder::SingleInput::_restartSrc(SingleInput *self) {
    auto srcBin = self->_bin->get(NODE_NAME_SRC);
    
    if (srcBin) {
        DBG(("Restarting source, attempt: {}", self->_srcRetryAttempt));

        srcBin->null([self, srcBin](bool success) {
            if (success) {
                self->_srcRestartTimerId = 0;
                self->_srcStreamsCount   = 0;

                srcBin->play();
            }
        });
    }

    return false;
}

void transcoder::SingleInput::_linkAdaptiveDemuxer(pipeline::BinPtr &srcBin, pipeline::ElementPtr &demuxer, bool filterScteFromSegments, bool streamsAware) {
    demuxer->onPadAdded([this, srcBin, filterScteFromSegments, streamsAware](pipeline::PadPtr &pad) {
        pipeline::ElementPtr nodeDemuxer;

        // No subtitle or caption track type exists downstream. Terminate the pad so the demuxer's
        // flow combiner does not see NOT_LINKED, and leave it out of the stream count.
        if (streamsAware && pad->getName().rfind("subtitle_", 0) == 0) {
            auto sink = srcBin->add(pipeline::Element::make("fakesink"));

            sink->setProperty<gboolean>("sync",  false);
            sink->setProperty<gboolean>("async", false);

            pad->link(sink->getPad("sink"));
            sink->syncWithParent();

            logger.log("Input '{}' ignores DASH subtitle track '{}'", this->getName(), pad->getName());

            return true;
        }

        std::lock_guard lock(this->_srcStreamsGuard);

        this->_srcStreamsCount++;

        if (! this->_srcHasAllStreams) {                                     
            nodeDemuxer = this->_createDemuxer(filterScteFromSegments);

            srcBin->addPad(pad->getName(), pad)->link(
                nodeDemuxer->getPad("sink")
            );

            this->_demuxers.push_back(nodeDemuxer);

            // dashdemux2 pushes as soon as the pad exists, possibly before streams-selected is seen
            // here; a demuxer still in NULL would answer FLUSHING and stop that track
            if (streamsAware) {
                nodeDemuxer->syncWithParent();
            }

        } else {
            if (this->_demuxers.size() >= this->_srcStreamsCount) {
                nodeDemuxer = this->_demuxers[this->_srcStreamsCount - 1];

                auto ghostPad = std::static_pointer_cast<pipeline::GhostPad>(srcBin->getPad(pad->getName()));

                ghostPad->setTarget(pad);
            }
        }

        pad->addProbe(
            { GST_PAD_PROBE_TYPE_BUFFER },
            [this](GstPadProbeInfo *info) {
                this->_clearRetry();

                return false;
            }
        );

        this->_startAdaptiveStreams();

        return true;
    });

    demuxer->onAllPads([this]() {
        std::lock_guard lock(this->_srcStreamsGuard);

        this->_srcExpectedStreams = this->_srcStreamsCount;
        this->_startAdaptiveStreams();

        return false;
    });

    // dashdemux2 never emits no-more-pads. Once it has exposed the selected tracks it posts
    // streams-selected, which says how many pads to wait for. The message reaches us through the
    // bus, so the pads may already be there or (in principle) still be coming.
    if (streamsAware) {
        auto *element = (*demuxer)->get();

        demuxer->onMessage([this, element](GstMessage *msg, bool &handled) {
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_STREAMS_SELECTED && GST_MESSAGE_SRC(msg) == GST_OBJECT(element)) {
                int expected = 0;

                for (guint i = 0; i < gst_message_streams_selected_get_size(msg); i++) {
                    auto *stream = gst_message_streams_selected_get_stream(msg, i);

                    if (gst_stream_get_stream_type(stream) & (GST_STREAM_TYPE_AUDIO | GST_STREAM_TYPE_VIDEO)) {
                        expected++;
                    }

                    gst_object_unref(stream);
                }

                std::lock_guard lock(this->_srcStreamsGuard);

                DBG(("Input '{}' demuxer selected {} audio/video tracks, {} exposed", this->getName(), expected, this->_srcStreamsCount));

                this->_srcExpectedStreams = expected;
                this->_startAdaptiveStreams();
            }

            return true;
        });
    }
}

// Called with _srcStreamsGuard held
void transcoder::SingleInput::_startAdaptiveStreams() {
    if (this->_srcHasAllStreams || this->_srcExpectedStreams <= 0 || this->_srcStreamsCount < this->_srcExpectedStreams) {
        return;
    }

    this->_srcHasAllStreams = true;

    for (auto &demux : this->_demuxers) {
        demux->syncWithParent();
    }

    // Started demuxers may already have configured all their pads (see _createDemuxer)
    if (this->_srcStreamsCount == this->_srcDemuxersCount) {
        DBG(("All sources collected as well as all decoders configured ({}/{})", this->_srcStreamsCount, this->_srcDemuxersCount));

        if (this->_probeCallback) {
            this->_probeCallback(true);
        }
    }
}

void transcoder::SingleInput::_onDashElementAdded(GstBin *bin, GstBin *subBin, GstElement *element, SingleInput *self) {
    auto *factory = gst_element_get_factory(element);

    // dashdemux2 parses each track with its own parsebin; the fMP4 demuxer there drops 'emsg' boxes
    // ("Unknown fourcc"), so read them from the segment bytes before it does
    if (! factory || strcmp(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)), "qtdemux") != 0) {
        return;
    }

    auto *sinkPad = gst_element_get_static_pad(element, "sink");

    if (sinkPad) {
        gst_pad_add_probe(
            sinkPad,
            (GstPadProbeType) (GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST),
            _onDashSegmentData,
            new DashEmsgTrack{ self },
            [](gpointer data) { delete static_cast<DashEmsgTrack *>(data); }
        );

        gst_object_unref(sinkPad);
    }
}

GstPadProbeReturn transcoder::SingleInput::_onDashSegmentData(GstPad *pad, GstPadProbeInfo *info, gpointer data) {
    auto *track = static_cast<DashEmsgTrack *>(data);

    std::vector<dash::EmsgBox> boxes;

    auto scan = [track, &boxes](GstBuffer *buffer) {
        // Each download (init or media segment) restarts at offset 0, which is also a box boundary.
        // Its first buffer carries the segment's start time, the base of a version 0 'emsg'.
        if (GST_BUFFER_OFFSET(buffer) == 0 || GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT)) {
            track->scanner.reset();
            track->segmentStart = GST_BUFFER_PTS(buffer);
        }

        GstMapInfo map;

        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            track->scanner.push(map.data, map.size, boxes);

            gst_buffer_unmap(buffer, &map);
        }
    };

    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER_LIST) {
        auto *list = GST_PAD_PROBE_INFO_BUFFER_LIST(info);

        for (guint i = 0; i < gst_buffer_list_length(list); i++) {
            scan(gst_buffer_list_get(list, i));
        }

    } else {
        scan(GST_PAD_PROBE_INFO_BUFFER(info));
    }

    for (const auto &box : boxes) {
        if (! dash::isScte35Scheme(box.schemeIdUri) || box.timescale == 0) {
            continue;
        }

        // The 'emsg' timeline is the one dashdemux2 timestamps its output with (verified against the
        // DASH-IF live simulator: presentation_time / timescale equals the output PTS of the splice frame)
        GstClockTime presentationTime = GST_CLOCK_TIME_NONE;

        if (box.version == 1) {
            presentationTime = gst_util_uint64_scale(box.presentationTime, GST_SECOND, box.timescale);

        } else if (GST_CLOCK_TIME_IS_VALID(track->segmentStart)) {
            presentationTime = track->segmentStart + gst_util_uint64_scale(box.presentationTime, GST_SECOND, box.timescale);
        }

        track->input->_injectDashScte35(box.id, presentationTime, box.messageData);
    }

    return GST_PAD_PROBE_OK;
}

void transcoder::SingleInput::_injectDashScte35(uint32_t eventId, GstClockTime presentationTime, const std::vector<uint8_t> &section) {
    if (! this->_scteDataCallback) {
        return;
    }

    // message_data of the SCTE 214-1 scheme is a complete splice_info_section (table_id 0xFC)
    if (section.size() < 3 || section[0] != 0xFC) {
        WARN(("DASH SCTE-35 event {} does not carry a splice_info_section - ignoring", eventId));
        return;
    }

    {
        std::lock_guard lock(this->_dashEmsgGuard);

        if (std::find(this->_dashEmsgIds.begin(), this->_dashEmsgIds.end(), eventId) != this->_dashEmsgIds.end()) {
            return;
        }

        this->_dashEmsgIds.push_back(eventId);

        if (this->_dashEmsgIds.size() > DASH_EMSG_ID_HISTORY) {
            this->_dashEmsgIds.pop_front();
        }
    }

    GstMpegtsSCTESpliceCommandType command;

    {
        auto *parsed = gst_mpegts_section_new(0, (guint8 *) g_memdup2(section.data(), section.size()), section.size());

        if (! parsed) {
            WARN(("DASH SCTE-35 event {} carries an invalid section - ignoring", eventId));
            return;
        }

        const auto *sit = gst_mpegts_section_get_scte_sit(parsed);

        command = sit ? sit->splice_command_type : GST_MTS_SCTE_SPLICE_COMMAND_NULL;

        gst_mpegts_section_unref(parsed);

        if (command != GST_MTS_SCTE_SPLICE_COMMAND_INSERT && command != GST_MTS_SCTE_SPLICE_COMMAND_TIME) {
            DBG(("DASH SCTE-35 event {} is neither splice_insert nor time_signal - ignoring", eventId));
            return;
        }
    }

    {
        GstClockTime runningTime       = GST_CLOCK_TIME_NONE;
        GstClockTime spliceRunningTime = GST_CLOCK_TIME_NONE;

        if (this->_adaptiveDemuxer) {
            auto *demuxer = (*this->_adaptiveDemuxer)->get();

            runningTime = gst_element_get_current_running_time(demuxer);

            if (GST_CLOCK_TIME_IS_VALID(presentationTime)) {
                GstIterator *it   = gst_element_iterate_src_pads(demuxer);
                GValue       item = G_VALUE_INIT;

                while (! GST_CLOCK_TIME_IS_VALID(spliceRunningTime) && gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
                    auto *srcPad = GST_PAD(g_value_get_object(&item));
                    auto *event  = gst_pad_get_sticky_event(srcPad, GST_EVENT_SEGMENT, 0);

                    if (event) {
                        const GstSegment *segment;

                        gst_event_parse_segment(event, &segment);

                        spliceRunningTime = gst_segment_to_running_time(segment, GST_FORMAT_TIME, presentationTime);

                        gst_event_unref(event);
                    }

                    g_value_reset(&item);
                }

                g_value_unset(&item);
                gst_iterator_free(it);
            }
        }

        // No output segment yet, or a time before it: act on receipt, as the TS path does
        if (! GST_CLOCK_TIME_IS_VALID(spliceRunningTime)) {
            spliceRunningTime = runningTime;
        }

        if (! GST_CLOCK_TIME_IS_VALID(spliceRunningTime)) {
            WARN(("DASH SCTE-35 event {} arrived before the input has a running time - ignoring", eventId));
            return;
        }

        // The authored splice time is on the source's media timeline (for live DASH, seconds since
        // availabilityStartTime). The muxer adds its own PTS offset to pts_adjustment and expects the
        // section to land on the running-time timeline, so shift it there as the scheduler does with
        // an operator-supplied section. An immediate splice is passed through unshifted.
        auto *gstSection = scheduler::sectionFromBase64(section, spliceRunningTime, command);

        if (! gstSection) {
            WARN(("DASH SCTE-35 event {} could not be re-timed - ignoring", eventId));
            return;
        }

        Scte35Data data;

        data.setSection(gstSection);
        data.setRunningTime(runningTime);
        data.setTimingMap(scheduler::buildRunningTimeMap(gstSection, spliceRunningTime));

        logger.log(
            "DASH SCTE-35 event {} acquired, splice at running time {} ms (now {} ms)",
            eventId,
            GST_CLOCK_TIME_IS_VALID(spliceRunningTime) ? (int64_t) (spliceRunningTime / GST_MSECOND) : -1,
            GST_CLOCK_TIME_IS_VALID(runningTime)       ? (int64_t) (runningTime       / GST_MSECOND) : -1
        );

        DBG(("Acquired DASH SCTE-35 section: {}", toBase64(data)));

        this->_scteDataCallback(data);

        gst_mpegts_section_unref(gstSection);
    }
}

uint32_t utpl::transcoder::SingleInput::_calculateBackoffDelay(uint32_t attempt) {
    uint32_t delay = 0;
    
    if (attempt < 32) {
        delay = 1 << attempt;
    }
    
    // Cap at maximum delay
    switch (this->_protocol) {
        case Protocol::RTMP:
            return std::min(delay, RTMP_MAX_BACKOFF_TIME);

        case Protocol::MXL:
            return std::min(delay, MXL_MAX_BACKOFF_TIME);

        default:
            return std::min(delay, MAX_BACKOFF_TIME);
    }
}

void utpl::transcoder::SingleInput::_clearRetry() {
    this->_srcRestartTimerId = 0;
    this->_srcRetryAttempt   = 0;
}

void transcoder::SingleInput::probe(ProbeCallback callback) {
    this->_probeCallback = callback;

    this->_bin->pause([this](bool success) {
        if (success) {
            this->_bin->get(NODE_NAME_SRC)->play();
        }
    });
}

void transcoder::SingleInput::cleanup() {
    this->_stopMxlWatch();

    this->_buffer           = nullptr;
    this->_bin              = nullptr;
    this->_isLive           = false;
    this->_srcHasAllStreams = false;
    this->_srcDemuxersCount = 0;
    this->_srcStreamsCount  = 0;
    this->_srcExpectedStreams = 0;
    this->_state            = InputState::CREATED;
    this->_hlsScteEventId   = 0;

    this->_clearRetry();
    this->_streams.clear();
    this->_hlsScteTags.clear();
    this->_adaptiveDemuxer = nullptr;

    {
        std::lock_guard lock(this->_dashEmsgGuard);

        this->_dashEmsgIds.clear();
    }
    this->_demuxers.clear();
    this->_bufferBlockId.clear();
}

const std::vector<std::shared_ptr<transcoder::InputStream>> &transcoder::SingleInput::getStreams() const {
    return this->_streams;
}

const transcoder::InputParameters &transcoder::SingleInput::getInputParameters() const {
    return this->_inputParameters;
}

void transcoder::SingleInput::onScteData(ScteDataCallback callback) {
    this->_scteDataCallback = callback;
}

void transcoder::SingleInput::onStateChanged(StateChangedCallback callback) {
    this->_stateChangeCallback = callback;
}

void utpl::transcoder::SingleInput::onStreamsChanged(StreamsChangedCallback callback) {
}

void utpl::transcoder::SingleInput::onError(ErrorCallback callback) {
    this->_errorCallback = callback;
}

pipeline::ElementPtr transcoder::SingleInput::getNode() {
    return this->_bin;
}

bool transcoder::SingleInput::isLive() const {
    return this->_isLive;
}

Protocol transcoder::SingleInput::getProtocol() const {
    return this->_protocol;
}

std::string transcoder::SingleInput::getName() const {
    return this->_name;
}

transcoder::InputStatus transcoder::SingleInput::getStatus() {
    transcoder::InputStatus ret;

    ret.addTransport(this->getId(), this->getInputParameters().getUris()[0], this->_state);
    ret.setCurrentTransportId(this->getId());
    
    return ret;
}

Error transcoder::SingleInput::setCurrentStream(const std::string &streamId) {
    return Error::NO_ERROR;
}

GstClockTime utpl::transcoder::SingleInput::getDuration() const {
    return this->_duration;
}

const std::string &transcoder::SingleInput::getId() const
{
    return this->_id.getString();
}

gboolean transcoder::SingleInput::_autoPlugContinueCallback(
    GstElement *bin, GstPad *padPtr, GstCaps *caps, Input *self
) {
    pipeline::Pad pad = pipeline::GstPadPtr::take(padPtr);
    pipeline::Element element = pad.getParent();

    DBG(("CALL! {}, {}", element.getName(), pad.getName()));

    return true;
}

// There is no public header containing the following enum 
typedef enum {
    GST_AUTOPLUG_SELECT_TRY,
    GST_AUTOPLUG_SELECT_EXPOSE,
    GST_AUTOPLUG_SELECT_SKIP
} GstAutoplugSelectResult;

int transcoder::SingleInput::_autoPlugSelectCallback(
    GstElement *bin, GstPad *pad, GstCaps *caps, GstElementFactory *factory, Input *self
) {
    DBG(("Decodebin asked for factory: {}", gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory))));

    if (gst_element_factory_list_is_type (factory, GST_ELEMENT_FACTORY_TYPE_DECODER)) {
        return GST_AUTOPLUG_SELECT_EXPOSE;
    }

    return GST_AUTOPLUG_SELECT_TRY;
}

void transcoder::SingleInput::_demuxerElementAdded(GstBin *bin, GstElement *element, SingleInput *self) {
    auto *factory = gst_element_get_factory(element);
    if (factory) {
        std::string factoryName = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));

        // TODO: support for other mpegts demuxers
        if (factoryName == "tsdemux") {
            const auto &selectorParams = self->_inputParameters.getStreamSelector();

            if (! selectorParams.fixedProgram()) {
                logger.log("Detected MPEG-TS stream on input. No program selector defined - the first program will be chosen as the source.");

            } else {
                pipeline::Element demux(pipeline::GstElementPtr::take(element));

                logger.log("Detected MPEG-TS stream on input. Program selector defined for program {}.", selectorParams.getProgram());

                demux.setProperty<gint>("program-number", selectorParams.getProgram());
            }
        }
    }
}
