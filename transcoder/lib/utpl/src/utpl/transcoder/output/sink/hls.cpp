#include <gst/gst.h>

#include <boost/url/url.hpp>
#include <boost/url/parse.hpp>

#include <algorithm>

#include "utpl/common/debug.h"
#include "utpl/common/types/exception.h"

#include "hls.h"

#include "../utils.h"

using namespace utpl;

namespace urls = boost::urls;

transcoder::HlsOutputSink::HlsOutputSink(
    const std::string                   &name, 
    const OutputTransport               &transport, 
    const std::vector<StreamParameters> &streams, 
    const ScteOutputParameters          &scteParameters, 
    Container                            container,
    std::shared_ptr<hls::Streamer>       streamer,
    std::shared_ptr<hls::MediaPlaylist>  playlist
) {
    this->_bin = pipeline::Bin::create();

    this->_name           = name;
    this->_container      = container;
    this->_scteParameters = scteParameters;
    
    this->streamer      = streamer;
    this->mediaPlaylist = playlist;

    this->_supportedMediaTypes.insert(MediaType::AUDIO);
    this->_supportedMediaTypes.insert(MediaType::VIDEO);

    if (container == Container::MPEGTS) {
        this->_supportedMediaTypes.insert(MediaType::CAPTION);
    }

    {
        const auto *hlsParameters = reinterpret_cast<const transcoder::HlsOutputTransportParameters *>(transport.getTransportParameters(Protocol::HLS));
        
        this->chunkLength = hlsParameters->getMaxSegmentDuration();
        this->vodMode     = hlsParameters->isVodMode();

        if (! this->streamer) {
            this->streamer = createHlsStreamer(transport);
        }

        if (! this->mediaPlaylist) {
            this->masterPlaylist = createHlsMasterPlaylist(hlsParameters);
            this->mediaPlaylist  = createHlsVariant(this->masterPlaylist, hlsParameters, streams, this->_container, std::string());

            this->streamer->setPlaylist(this->masterPlaylist);
        }
    }
}

void transcoder::HlsOutputSink::link() {
    this->_sink = pipeline::Element::make("appsink");

    {
        GstAppSink *appsink = GST_APP_SINK((*this->_sink)->get());

        {
            GstAppSinkCallbacks callbacks;

            callbacks.eos                = nullptr;
            callbacks.new_event          = nullptr;
            callbacks.new_preroll        = nullptr;
            callbacks.propose_allocation = nullptr;
            callbacks.new_sample         = onNewSample;

            gst_app_sink_set_callbacks(appsink, &callbacks, this, nullptr);
        }

        this->_sink->onPlay([this]() {
            this->streamer->start();

            return true;
        });

        this->_sink->onNull([this]() {
            this->streamer->stop();

            return true;
        });

        this->_sink->setProperty<gboolean>("buffer-list", true);
        this->_sink->setProperty<gboolean>("async",       false);
    }

    if (this->_container == Container::MPEGTS) {
        this->_splitmuxsink = this->_bin->add(pipeline::Element::make("splitmuxsink"));
        
        this->_muxer = pipeline::Element::make("mpegtsmux");
        
        this->_splitmuxsink->setProperty<GstElement *>("muxer", (*this->_muxer)->get());
        this->_splitmuxsink->setProperty<GstElement *>("sink", (*this->_sink)->get());
        this->_splitmuxsink->setProperty<guint64>("max-size-time", this->chunkLength * GST_MSECOND);
        this->_splitmuxsink->setProperty<gboolean>("send-keyframe-requests", true);
        this->_splitmuxsink->setProperty<gboolean>("reset-muxer", false);

        this->_splitmuxsink->onSignal("format-location", G_CALLBACK(onFormatLocation), this);

        this->_splitmuxsink->onMessage([this](GstMessage *msg, bool &handled) {
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT((*this->_splitmuxsink)->get())) {
                const GstStructure *s = gst_message_get_structure(msg);
              
                std::unique_lock lock(this->guard);

                if (gst_structure_has_name(s, "splitmuxsink-fragment-opened")) {
                    if (! gst_structure_get_clock_time(s, "running-time", &this->runningTime)) {
                        ERR(("Unable to get running time of segment beginning!"));

                        this->runningTime = -1;
                    }

                    if (this->currentSegment) {
                        this->currentSegment->setBeginRunningTime(this->runningTime);
                    }

                } else if (gst_structure_has_name (s, "splitmuxsink-fragment-closed")) {
                    GstClockTime endTime = GST_CLOCK_TIME_NONE;

                    if (! gst_structure_get_clock_time(s, "running-time", &endTime)) {
                        WARN(("Unable to retrieve the running time at the end of the chunk!"));
                    }

                    {
                        std::shared_ptr<hls::Segment> segmentToFinish;

                        if (this->lastSegment) {
                            segmentToFinish = std::move(this->lastSegment);

                        } else {
                            segmentToFinish = std::move(this->currentSegment);
                        }

                        if (! segmentToFinish) {
                            ERR(("End of segment reported, but no open segment found"));

                        } else {
                            segmentToFinish->setDuration(this->chunkLength);
                            segmentToFinish->setEndRunningTime(endTime);

                            DBG(("Fragment closed, duration: {} ms", segmentToFinish->getDuration()));

                            if (! segmentToFinish->isBuffering()) {
                                ERR(("End of segment reported, but no segment is in buffering state"));

                            } else {
                                segmentToFinish->setBuffered();                            
                            }
                        }
                    }
                }
            }
            
            return true;
        });

        this->_splitmuxsink->onPlay([this]() {
            this->streamer->start();

            return true;
        });

        this->_splitmuxsink->onNull([this]() {
            this->streamer->stop();

            return true;
        });

        {
            if (this->_scteParameters.getPid() != PID_NULL) {
                guint scteInterval = this->_scteParameters.getNullInterval();
    
                if (scteInterval < 0) {
                    scteInterval = 0;
                }
    
                // Convert to 90kHz ticks
                scteInterval *= 90;
    
                this->_muxer->setProperty<guint>("scte-35-null-interval", scteInterval);
                this->_muxer->setProperty<guint>("scte-35-pid", this->_scteParameters.getPid());
            }
        }

    } else if (this->_container == Container::FMP4) {
        this->_bin->add(this->_sink);
        this->_muxer = this->_bin->add(pipeline::Element::make("isofmp4mux"));
        
        this->_muxer->setProperty<guint64>("fragment-duration", this->chunkLength * GST_MSECOND);
        this->_muxer->setProperty<gboolean>("write-mehd", true);
        this->_muxer->setProperty<guint>("header-update-mode", 2);
        
        this->_muxer->link(this->_sink);
    }

    if (this->_container == Container::FMP4) {
        for (auto &pad : this->_pads) {
            pad.pad->setTarget(this->_muxer->requestPad("sink_%u"));
        }

    } else {
        // splitmuxsink requests the mpegtsmux pads itself, so a configured PID reaches the muxer
        // through muxer-pad-map, keyed by the splitmuxsink pad name. The pads are named here so
        // the keys are known; configured PIDs go first so a muxer-numbered stream cannot take one.
        std::vector<std::pair<StreamPad *, std::string>> requests;
        int audioIdx   = 0;
        int captionIdx = 0;

        for (auto &pad : this->_pads) {
            switch (pad.mediaType) {
                case MediaType::AUDIO:
                    requests.push_back({&pad, "audio_" + std::to_string(audioIdx++)});
                    break;

                case MediaType::VIDEO:
                    requests.push_back({&pad, "video"});
                    break;

                case MediaType::CAPTION:
                    requests.push_back({&pad, "caption_" + std::to_string(captionIdx++)});
                    break;

                default:
                    break;
            }
        }

        std::stable_partition(requests.begin(), requests.end(), [](const auto &r) {
            return r.first->pid != PID_NULL;
        });

        GstStructure *padMap = gst_structure_new_empty("map");

        for (auto &r : requests) {
            if (r.first->pid != PID_NULL) {
                gst_structure_set(padMap, r.second.c_str(), G_TYPE_STRING, ("sink_" + std::to_string(r.first->pid)).c_str(), nullptr);
            }
        }

        if (gst_structure_n_fields(padMap) > 0) {
            this->_splitmuxsink->setProperty<GstStructure *>("muxer-pad-map", padMap);
        }

        gst_structure_free(padMap);

        for (auto &r : requests) {
            auto splitPad = this->_splitmuxsink->requestPad(r.second);

            if (! *splitPad) {
                throw InvalidArgumentException("HLS output '" + this->_name + "': unable to request muxer pad '" + r.second + "' (PID " + std::to_string(r.first->pid) + ")");
            }

            r.first->pad->setTarget(splitPad);
        }
    }
}

pipeline::ElementPtr transcoder::HlsOutputSink::getNode() {
    return this->_bin;
}

pipeline::PadPtr transcoder::HlsOutputSink::getPad(MediaType mediaType, Pid pid) {
    this->_pads.push_back(
        {mediaType, pid, this->_bin->addPad("", nullptr, false)}
    );

    return this->_pads.back().pad;
}

void transcoder::HlsOutputSink::putScteData(const Scte35Data &data) {
    if (this->_scteParameters.getMode() == ScteOutputMode::PASSTHROUGH) {
        switch (this->_container) {
            case Container::MPEGTS:
                {
                    auto *event = gst_event_new_mpegts_section((GstMpegtsSection *) data.getSection());

                    if (event) {
                        auto timingMap = data.getTimingMap();

                        if (timingMap) {
                            auto *s = gst_event_writable_structure(event);

                            gst_structure_set(s,
                            "mpeg-pts-offset", G_TYPE_UINT64, 0,
                            "running-time-map", GST_TYPE_STRUCTURE, data.getTimingMap(),
                            NULL
                            );
                        }

                        if (! gst_element_send_event((*this->_muxer)->get(), event)) {
                            ERR(("Unable to send SCTE event to muxer!"));

                            gst_event_unref (event);
                        }
                    }

                    g_signal_emit_by_name((*this->_splitmuxsink)->get(), "split-at-running-time", data.getSpliceTime());

                    {
                        hls::ScteEvent wrapped(data);

                        if (wrapped.isOut()) {
                            auto duration = wrapped.getDuration();

                            if (duration != GST_CLOCK_TIME_NONE) {
                                auto endTime = data.getSpliceTime() + duration;

                                g_signal_emit_by_name((*this->_splitmuxsink)->get(), "split-at-running-time", endTime);
                            }
                        }
                    }

                    this->streamer->putScteData(data);
                }
                break;

            default:
                WARN(("Container {} is not supported", toString(this->_container)));
                break;
        }
    }
}

bool transcoder::HlsOutputSink::isLive() const {
    return this->vodMode == false;
}

bool transcoder::HlsOutputSink::needExternalSync() const {
    return true;
}

const std::set<MediaType> &transcoder::HlsOutputSink::getSupportedMediaTypes() const {
    return this->_supportedMediaTypes;
}

gchar *transcoder::HlsOutputSink::onFormatLocation(GstElement *splitmux, guint fragment_id, HlsOutputSink *self) {
    std::unique_lock lock(self->guard);
    
    DBG(("Format new fragment with id: {}", fragment_id));
    
    // Occasionally, a race condition occurs between the format-location event and the splitmuxsink-fragment-opened|closed messages.  
    // To handle this, we need to track the last chunk to ensure proper completion.
    if (self->currentSegment) {
        self->lastSegment = self->currentSegment;
    }
    
    self->currentSegment = self->streamer->createSegment(self->mediaPlaylist);
    
    return nullptr;
}

GstFlowReturn transcoder::HlsOutputSink::onNewSample(GstAppSink *sink, gpointer data) {
    GstFlowReturn ret = GST_FLOW_OK;

    {
        auto *self = reinterpret_cast<HlsOutputSink *>(data);

        GstSample *sample = gst_app_sink_pull_sample(sink);

        do {
            if (! sample) {
                ERR(("Failed to get sample from appsink!"));
            
                ret = GST_FLOW_ERROR;
                break;
            }

            auto *bufferList = gst_sample_get_buffer_list(sample);
            if (! bufferList) {
                ERR(("Failed to get buffer list from the sample"));

                ret = GST_FLOW_ERROR;
                break;
            }

            auto bufferListLength = gst_buffer_list_length(bufferList);

            if (! self->currentSegment) {
                if (self->_container == utpl::Container::FMP4) {
                    self->currentSegment = self->streamer->createSegment(self->mediaPlaylist);

                } else {
                    ERR(("Current segment is null - it should never happen for mpegts packetizing"));
                }
            }

            if (self->currentSegment->isEmpty()) {
                self->currentSegment->setBuffering();
            }

            {
                std::set<int> ignoredBuffersIndexes;

                if (self->_container == utpl::Container::FMP4) {
                    for (guint i = 0; i < bufferListLength; i++) {
                        GstBuffer *buffer = gst_buffer_list_get(bufferList, i);

                        if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT) && GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_HEADER)) {
                            DBG(("Got buffer for FMP4 init file at index {}", i));

                            ignoredBuffersIndexes.insert(i);
                        }
                    }
                }

                // Store initial segment
                if (! ignoredBuffersIndexes.empty()) {
                    auto initialSegment = self->streamer->createInitialSegment(
                        self->mediaPlaylist
                    );

                    initialSegment->setBuffering();
                    {
                        auto storage = initialSegment->getStorage();

                        size_t written = 0;

                        for (auto initialSegmentIndex : ignoredBuffersIndexes) {
                            written += storage->write(gst_buffer_list_get(bufferList, initialSegmentIndex));
                        }

                        DBG(("Written {} bytes of initial segment", written));
                    }
                    initialSegment->setBuffered();
                }

                // Store data for current segment
                size_t written = self->currentSegment->getStorage()->write(bufferList, ignoredBuffersIndexes);

                if (self->_container == utpl::Container::FMP4) {
                    utpl::Timestamp duration = 0;

                    for (int i = 0; i < bufferListLength; i++) {
                        if (ignoredBuffersIndexes.find(i) == ignoredBuffersIndexes.end()) {
                            duration = GST_TIME_AS_MSECONDS(GST_BUFFER_DURATION(gst_buffer_list_get(bufferList, i)));
                            break;
                        }
                    }

                    self->currentSegment->setDuration(duration);
                    self->currentSegment->setBuffered();

                    self->currentSegment = nullptr;
                }
            }
        } while (0);

        if (sample) {
            gst_sample_unref(sample);
        }
    }

    return ret;
}
