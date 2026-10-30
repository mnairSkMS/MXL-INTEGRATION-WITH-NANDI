#include <filesystem>

#include <gst/audio/audio-format.h>

#include "utpl/common/debug.h"

#include "utpl/transcoder/input/overlay.h"

using namespace utpl;


#define DEFAULT_TIMEOUT_MS 4000
#define DEFAULT_OUTPUT_FMT AV_PIX_FMT_BGRA


transcoder::OverlayInput::OverlayInputStream::OverlayInputStream(OverlayInput *parent, const StreamParameters &parameters, pipeline::PadPtr decodedPad, pipeline::BinPtr bin) : BaseInputStream(bin) {
    this->_parent = parent;
    this->_pad    = decodedPad;

    this->setStreamParameters(parameters);
}

void transcoder::OverlayInput::OverlayInputStream::synchronizeClock(GstClockTime runningTime) {
    if (this->getStreamParameters().hasAudioParameters()) {
        this->_parent->_audioPts = runningTime;

    } else {
        this->_parent->_videoPts = runningTime;
    }
}

pipeline::PadPtr transcoder::OverlayInput::OverlayInputStream::getSrcPad(bool passthrough) {
    if (! passthrough) {
        return this->_pad;
    }

    return nullptr;
}

transcoder::OverlayInput::OverlayInput(const std::string &tmpPath, utpl::Timestamp timeout) : _tmpPath(tmpPath) {
    this->_formatCtx     = nullptr;
    this->_audioCodecCtx = nullptr;
    this->_audioStreamId = -1;
    this->_videoCodecCtx = nullptr;
    this->_videoStreamId = -1;
    this->_videoScaller  = nullptr;
    this->_feedTaskExit  = false;

    this->_bin           = pipeline::Bin::create();
    this->_localFilePath = std::filesystem::path(tmpPath) / ("overaly-" + this->_id.getString() + "-src.bin");

    this->_videoPts = GST_CLOCK_TIME_NONE;
    this->_audioPts = GST_CLOCK_TIME_NONE;

    this->_state = InputState::CREATED;
}

transcoder::OverlayInput::~OverlayInput() {
    this->cleanup();
}

void transcoder::OverlayInput::prepare(const InputParameters &parameters) {
    this->_inputParameters = parameters;
    this->_feedTaskExit    = false;

    this->_bin->onPlay([this]() {
        if (! this->_feedTask && ! this->_feedTaskExit) {
            this->_feedTask = std::make_unique<std::thread>(&OverlayInput::_feedTaskRoutine, this);
        }

        return false;
    });
}

template <typename... Ts>
static cpr::AsyncResponse downloadAsync(std::shared_ptr<cpr::Session> session, cpr::WriteCallback &&writeCallback, const std::function<void(const cpr::Response &)> &&responseCallback, Ts&& ...ts) {
    return cpr::AsyncWrapper{std::async(
        std::launch::async,
        [session](const cpr::WriteCallback &&writeCb, const std::function<void(const cpr::Response&)> &&responseCb, Ts&&... ts) {
            const cpr::Response response = session->Download(std::move(writeCb), std::forward<Ts>(ts)...);
            responseCb(response);
            return response;
        },
        std::move(writeCallback), std::move(responseCallback), std::move(ts)...)
    };
}

pipeline::ElementPtr transcoder::OverlayInput::_createAppSrc() {
    auto src = this->_bin->add(pipeline::Element::make("appsrc"));

    src->setProperty<gint>("format", GST_FORMAT_TIME);
    src->setProperty<gboolean>("block", true);
    src->setProperty<guint64>("max-buffers", 16);
    src->setProperty<guint64>("max-bytes", 0);
    src->setProperty<gboolean>("is-live", true);

    gst_app_src_set_stream_type(GST_APP_SRC((*src)->get()), GST_APP_STREAM_TYPE_STREAM);
    gst_app_src_set_size       (GST_APP_SRC((*src)->get()), -1);
    gst_app_src_set_latency    (GST_APP_SRC((*src)->get()), 0, -1);
    gst_app_src_set_duration   (GST_APP_SRC((*src)->get()), GST_CLOCK_TIME_NONE);
    gst_app_src_set_leaky_type (GST_APP_SRC((*src)->get()), GST_APP_LEAKY_TYPE_NONE);

    return src;
}

void transcoder::OverlayInput::probe(ProbeCallback callback) {
    this->_probeCallback = callback;

    this->_bin->ready([this](bool success) {
        auto path = std::filesystem::path(this->_localFilePath);

        if (std::filesystem::exists(path) && std::filesystem::file_size(path) > 0) {
            LOG(("File '{}' is already downloaded - skipping", path.string()));

        } else {
            this->_session = std::make_shared<cpr::Session>();

            this->_setState(InputState::BUFFERING);

            this->_session->SetUrl(this->_inputParameters.getUris()[0]);
            this->_session->SetTimeout(DEFAULT_TIMEOUT_MS);

            auto stream = std::make_shared<std::ofstream>(this->_localFilePath, std::ios_base::out | std::ios_base::trunc);
            if (! stream->is_open()) {
                this->_probeCallback(false);

            } else {
                downloadAsync(this->_session, 
                    cpr::WriteCallback([stream](std::string data, intptr_t userdata) {
                        stream->write(data.data(), data.size());

                        return true;

                    }, 0),
                    [stream, this](const cpr::Response &response) {
                        bool probed = false;

                        stream->close();

                        this->_setState(InputState::BUFFERED);

                        this->_audioStreamId = -1;
                        this->_videoStreamId = -1;

                        do {
                            if (response.status_code != 200) {
                                ERR(("Error has occurred while downloading media file!"));
                                break;
                            }

                            if (avformat_open_input(&this->_formatCtx, this->_localFilePath.c_str(), nullptr, nullptr) < 0) {
                                ERR(("Unable to open image file from uri: {}", this->_localFilePath));
                                break;
                            }
            
                            if (avformat_find_stream_info(this->_formatCtx, nullptr) < 0) {
                                ERR(("Unable to find stream info in source file (uri: {})", this->_localFilePath));
                                break;
                            }
            
                            this->_videoStreamId = av_find_best_stream(this->_formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
                            this->_audioStreamId = av_find_best_stream(this->_formatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);

                            if (this->_audioStreamId < 0 && this->_videoStreamId < 0) {
                                ERR(("FFmpeg is not able to find neither video or audio stream in source file (uri: {})", this->_localFilePath));
                                break;
                            }

                            LOG(("Probed input file, audio: {}, video: {}", this->_audioStreamId >= 0, this->_videoStreamId >= 0));

                            if (this->_audioStreamId >= 0) {
                                auto *stream = this->_formatCtx->streams[this->_audioStreamId];

                                const auto *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
                                if (! decoder) {
                                    ERR(("FFmpeg has no implementation of codec {} (uri: {})", stream->codecpar->codec_id, this->_localFilePath));
                                    break;
                                }
            
                                this->_audioCodecCtx = avcodec_alloc_context3(decoder);
                                if (! this->_audioCodecCtx) {
                                    ERR(("Error allocating memory for AVCodec context (uri: {})", this->_localFilePath));
                                    break;
                                }
            
                                if (avcodec_parameters_to_context(this->_audioCodecCtx, stream->codecpar) < 0) {
                                    ERR(("Error setting codec parameters on AVCodec context (uri: {})", this->_localFilePath));
                                    break;
                                }
            
                                if (avcodec_open2(this->_audioCodecCtx, decoder, nullptr) < 0) {
                                    ERR(("Error openning video decoder (uri: {})", this->_localFilePath));
                                    break;
                                }

                                {
                                    auto srcCaps = pipeline::Caps::create();

                                    srcCaps->setMediaFormat(MediaType::AUDIO, Container::ES, Codec::ES);

                                    srcCaps->setRate(this->_audioCodecCtx->sample_rate);
                                    srcCaps->setChannels(this->_audioCodecCtx->ch_layout.nb_channels);

                                    {
                                        GstAudioFormat format = GST_AUDIO_FORMAT_UNKNOWN;
                                        GstAudioLayout layout = GST_AUDIO_LAYOUT_NON_INTERLEAVED;

                                        switch (this->_audioCodecCtx->sample_fmt) {
                                            case AV_SAMPLE_FMT_U8: 
                                                layout = GST_AUDIO_LAYOUT_INTERLEAVED;
                                            case AV_SAMPLE_FMT_U8P:    
                                                format = GST_AUDIO_FORMAT_U8; 
                                                break;

                                            case AV_SAMPLE_FMT_S16: 
                                                layout = GST_AUDIO_LAYOUT_INTERLEAVED;
                                            case AV_SAMPLE_FMT_S16P:    
                                                format = GST_AUDIO_FORMAT_S16; 
                                                break;

                                            case AV_SAMPLE_FMT_S32: 
                                                layout = GST_AUDIO_LAYOUT_INTERLEAVED;
                                            case AV_SAMPLE_FMT_S32P:    
                                                format = GST_AUDIO_FORMAT_S32; 
                                                break;

                                            case AV_SAMPLE_FMT_FLT: 
                                                layout = GST_AUDIO_LAYOUT_INTERLEAVED;
                                            case AV_SAMPLE_FMT_FLTP:    
                                                format = GST_AUDIO_FORMAT_F32; 
                                                break;

                                            case AV_SAMPLE_FMT_DBL: 
                                                layout = GST_AUDIO_LAYOUT_INTERLEAVED;
                                            case AV_SAMPLE_FMT_DBLP:    
                                                format = GST_AUDIO_FORMAT_F64; 
                                                break;

                                            default:
                                                break;
                                        }

                                        if (format != GST_AUDIO_FORMAT_UNKNOWN) {
                                            srcCaps->setString("format", gst_audio_format_to_string(format));
                                            srcCaps->setString("layout", layout == GST_AUDIO_LAYOUT_INTERLEAVED ? "interleaved" : "non-interleaved");

                                            this->_audioSrc = _createAppSrc();

                                            DBG(("Updating src audio element caps to: '{}'", srcCaps->toString()));
                        
                                            this->_audioSrc->setProperty<GstCaps *>("caps", (*srcCaps)->get());
        
                                            gst_audio_info_init(&this->_audioInfo);
                                            gst_audio_info_from_caps(&this->_audioInfo, (*srcCaps)->get());

                                            this->_streams.push_back(
                                                std::make_shared<OverlayInputStream>(
                                                    this, StreamParameters::fromCaps(*srcCaps), this->_audioSrc->getPad("src"), this->_bin
                                                )
                                            );
                                        }                                        
                                    }
                                }
                            }

                            if (this->_videoStreamId >= 0) {
                                auto *stream = this->_formatCtx->streams[this->_videoStreamId];

                                const auto *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
                                if (! decoder) {
                                    ERR(("FFmpeg has no implementation of codec {} (uri: {})", stream->codecpar->codec_id, this->_localFilePath));
                                    break;
                                }
            
                                this->_videoCodecCtx = avcodec_alloc_context3(decoder);
                                if (! this->_videoCodecCtx) {
                                    ERR(("Error allocating memory for AVCodec context (uri: {})", this->_localFilePath));
                                    break;
                                }
            
                                if (avcodec_parameters_to_context(this->_videoCodecCtx, stream->codecpar) < 0) {
                                    ERR(("Error setting codec parameters on AVCodec context (uri: {})", this->_localFilePath));
                                    break;
                                }
            
                                if (avcodec_open2(this->_videoCodecCtx, decoder, nullptr) < 0) {
                                    ERR(("Error openning video decoder (uri: {})", this->_localFilePath));
                                    break;
                                }

                                this->_videoScaller = sws_getContext(
                                    this->_videoCodecCtx->width, this->_videoCodecCtx->height, this->_videoCodecCtx->pix_fmt, 
                                    this->_videoCodecCtx->width, this->_videoCodecCtx->height, DEFAULT_OUTPUT_FMT,
                                    SWS_BILINEAR, NULL, NULL, NULL
                                );
                                
                                this->_videoSrc = _createAppSrc();

                                {
                                    auto srcCaps = pipeline::Caps::create();

                                    srcCaps->setMediaFormat(MediaType::VIDEO, Container::ES, Codec::ES);

                                    srcCaps->setFormat(PixelFormat::BGRA);
                                    srcCaps->setInterlace(Interlace::PROGRESSIVE);
                                    
                                    srcCaps->setWidth(this->_videoCodecCtx->width);
                                    srcCaps->setHeight(this->_videoCodecCtx->height);
                                    
                                    srcCaps->setFrameRate(Fraction(
                                        this->_videoCodecCtx->framerate.num, this->_videoCodecCtx->framerate.den
                                    ));

                                    DBG(("Updating src video element caps to: '{}'", srcCaps->toString()));
                        
                                    this->_videoSrc->setProperty<GstCaps *>("caps", (*srcCaps)->get());

                                    this->_streams.push_back(
                                        std::make_shared<OverlayInputStream>(
                                            this, StreamParameters::fromCaps(*srcCaps), this->_videoSrc->getPad("src"), this->_bin
                                        )
                                    );
                                }
                            }

                            probed = true;

                        } while (0);

                        if (! probed) {
                            this->cleanup();
                        }

                        this->_probeCallback(probed);
                    }
                );
            }
        }
    });
}

void transcoder::OverlayInput::cleanup() {
    this->_feedTaskExit = true;

    if (this->_feedTask) {
        if (this->_audioSrc) {
            this->_audioSrc->null();
        }

        if (this->_videoSrc) {
            this->_videoSrc->null();
        }

        this->_feedTask->join();

        this->_feedTask = nullptr;
    }

    this->_bin->null();

    {
        this->_videoSrc.reset();

        if (this->_videoScaller) {
            sws_freeContext(this->_videoScaller);

            this->_videoScaller = nullptr;
        }

        if (this->_videoCodecCtx) {
            avcodec_close(this->_videoCodecCtx);
            avcodec_free_context(&this->_videoCodecCtx);

            this->_videoCodecCtx = nullptr;
        }
        this->_videoStreamId = -1;
    }

    {
        this->_audioSrc.reset();

        if (this->_audioCodecCtx) {
            avcodec_close(this->_audioCodecCtx);
            avcodec_free_context(&this->_audioCodecCtx);

            this->_audioCodecCtx = nullptr;
        }
        this->_audioStreamId = -1;
    }
    
    if (this->_formatCtx) {
        avformat_close_input(&this->_formatCtx);

        this->_formatCtx = nullptr;
    }

    if (std::filesystem::exists(this->_localFilePath)) {
        std::filesystem::remove(this->_localFilePath);
    }

    this->_videoPts = GST_CLOCK_TIME_NONE;
    this->_audioPts = GST_CLOCK_TIME_NONE;

    this->_setState(InputState::CREATED);
}

const std::vector<std::shared_ptr<transcoder::InputStream>> &transcoder::OverlayInput::getStreams() const {
    return this->_streams;
}

const transcoder::InputParameters &transcoder::OverlayInput::getInputParameters() const {
    return this->_inputParameters;
}

void transcoder::OverlayInput::onScteData(ScteDataCallback callback) {
    // Intentionally ignored - no SCTE in graphics or slate streams
}

void transcoder::OverlayInput::onStateChanged(StateChangedCallback callback) {
    this->_stateChangedCallback = callback;
}

void transcoder::OverlayInput::onError(ErrorCallback callback) {
    // TODO:
}

pipeline::ElementPtr transcoder::OverlayInput::getNode() {
    return this->_bin;
}

bool transcoder::OverlayInput::isLive() const {
    return false;
}

Protocol transcoder::OverlayInput::getProtocol() const {
    return Protocol::UNKNOWN;
}

std::string transcoder::OverlayInput::getName() const {
    if (this->_bin) {
        return this->_bin->getName();
    }

    return std::string();
}

GstClockTime transcoder::OverlayInput::getDuration() const {
    return GST_CLOCK_TIME_NONE;
}

const std::string &transcoder::OverlayInput::getId() const {
    return this->_id.getString();
}

transcoder::InputStatus transcoder::OverlayInput::getStatus() {
    transcoder::InputStatus ret;

    ret.addTransport(this->getId(), this->getInputParameters().getUris()[0], this->_state);
    ret.setCurrentTransportId(this->getId());
    
    return ret;
}

Error transcoder::OverlayInput::setCurrentStream(const std::string &streamId) {
    return Error::NO_ERROR;
}

void utpl::transcoder::OverlayInput::onStreamsChanged(StreamsChangedCallback callback) {
}

void transcoder::OverlayInput::_setState(InputState state)
{
    if (this->_state != state) {
        this->_state = state;

        if (this->_stateChangedCallback) {
            this->_stateChangedCallback(this->_state);
        }
    }
}

static GstClockTime _getElementClockTime(pipeline::ElementPtr element) {
    GstClockTime ret = GST_CLOCK_TIME_NONE;

    auto clock = gst_element_get_clock((*element)->get());
    if (clock) {
        ret = gst_clock_get_time(clock);

        {
            GstClockTime baseTime = gst_element_get_base_time(GST_ELEMENT_CAST((*element)->get()));

            if (ret > baseTime) {
                ret -= baseTime;

            } else {
                ret = 0;
            }
        }

        gst_object_unref(clock);
    }

    return ret;
}

void transcoder::OverlayInput::_feedTaskRoutine() {
    auto *frame  = av_frame_alloc();
    auto *packet = av_packet_alloc();

    bool readResult = true;

    bool eofAudio = false;
    bool eofVideo = false;

    bool processAudio = false;
    bool processVideo = false;

    uint8_t *videoData[4] = { NULL };
    int      videoLineSize[4];
    int      videoBuffSize;

    if (this->_audioSrc && this->_audioSrc->getPad("src")->getPeer()) {
        processAudio = true;
    }

    if (this->_videoSrc && this->_videoSrc->getPad("src")->getPeer()) {
        processVideo = true;
    }

    if (processVideo) {
        videoBuffSize = av_image_alloc(
            videoData, videoLineSize, this->_videoCodecCtx->width, this->_videoCodecCtx->height, DEFAULT_OUTPUT_FMT, 1
        );
    }

    if (this->_videoPts == GST_CLOCK_TIME_NONE && this->_videoSrc) {
        DBG(("Setting initial video PTS to element's running time"));

        this->_videoPts = _getElementClockTime(this->_videoSrc);
    }

    if (this->_audioPts == GST_CLOCK_TIME_NONE && this->_audioSrc) {
        DBG(("Setting initial audio PTS to element's running time"));

        this->_audioPts = _getElementClockTime(this->_audioSrc);
    }

    DBG(("Starting overlay input with audio PTS {}, video PTS {}", (gint64) this->_audioPts, (gint64) this->_videoPts));

    while (! this->_feedTaskExit && readResult) {
        if (
            (this->_videoStreamId < 0 || (this->_videoStreamId >= 0 && eofVideo)) &&
            (this->_audioStreamId < 0 || (this->_audioStreamId >= 0 && eofAudio))
        ) {
            if (avformat_seek_file(this->_formatCtx, -1, INT64_MIN, 0, INT64_MAX, 0) < 0) {
                ERR(("Unable to seek!"));
            } else {
                DBG(("Successfully performed seek on overlay input"));
            }

            eofAudio = false;
            eofVideo = false;
        }

        {
            auto readRet = av_read_frame(this->_formatCtx, packet);

            if (readRet < 0) {
                if (readRet == AVERROR_EOF) {
                    eofAudio = true;
                    eofVideo = true;

                } else {
                    ERR(("Error reading input stream! {}", readRet));

                    readResult = false;
                }

                continue;
            }
        }

        {
            AVCodecContext *codecCtx = nullptr;

            if (processVideo && packet->stream_index == this->_videoStreamId) {
                codecCtx = this->_videoCodecCtx;

            } else if (processAudio && packet->stream_index == this->_audioStreamId) {
                codecCtx = this->_audioCodecCtx;
            }

            if (codecCtx) {
                auto tmp = avcodec_send_packet(codecCtx, packet);
                if (tmp < 0) {
                    ERR(("FFmpeg decoder returned error while feeding it by src packet"));
                    break;
                }

                while (tmp >= 0) {
                    GstBuffer *newBuffer = nullptr;
                    GstAppSrc *appSrc    = nullptr;

                    tmp = avcodec_receive_frame(codecCtx, frame);
                    if (tmp < 0) {
                        if (tmp == AVERROR_EOF) {
                            ERR(("EOF"));
    
                            eofVideo = true;
    
                        } else if (tmp != AVERROR(EAGAIN)) {
                            ERR(("FFmpeg decoder returned error while reading raw data from it {}", tmp));
    
                            readResult = false;
                        }
    
                        break;
                    }

                    if (codecCtx == this->_videoCodecCtx) {
                        GstMapInfo map;

                        newBuffer = gst_buffer_new_allocate(nullptr, videoBuffSize, nullptr);

                        sws_scale(
                            this->_videoScaller, frame->data, frame->linesize, 0, frame->height, videoData, videoLineSize
                        );

                        gst_buffer_map(newBuffer, &map, GST_MAP_WRITE);
                        {
                            memcpy(map.data, videoData[0], videoBuffSize);
                        }
                        gst_buffer_unmap(newBuffer, &map);

                        GST_BUFFER_DTS(newBuffer)       = GST_CLOCK_TIME_NONE;
                        GST_BUFFER_TIMESTAMP(newBuffer) = this->_videoPts;

                        {
                            GstClockTime duration = GST_CLOCK_TIME_NONE;

                            if (frame->duration != AV_NOPTS_VALUE) {
                                AVRational bq = { 1, GST_SECOND };

                                GST_BUFFER_DURATION(newBuffer) = av_rescale_q(frame->duration, this->_formatCtx->streams[this->_videoStreamId]->time_base, bq);

                                this->_videoPts += GST_BUFFER_DURATION(newBuffer);
                            }
                        }

                        appSrc = GST_APP_SRC((*this->_videoSrc)->get());

                    } else {
                        auto bytesPerSample = this->_audioInfo.finfo->width / 8;
                        auto channels       = this->_audioInfo.channels;
                        auto samplesCount   = frame->nb_samples;
                        auto outputSize     = samplesCount * bytesPerSample * channels;

                        newBuffer = gst_buffer_new_allocate(nullptr, outputSize, nullptr);

                        if (av_sample_fmt_is_planar((AVSampleFormat) frame->format)) {

                            GstAudioMeta *meta = gst_buffer_add_audio_meta(newBuffer, &this->_audioInfo, samplesCount, NULL);
                      
                            for (int i = 0; i < channels; i++) {
                                gst_buffer_fill(newBuffer, meta->offsets[i], frame->extended_data[i], samplesCount * bytesPerSample);
                            }

                        } else {
                            gst_buffer_fill(newBuffer, 0, frame->data[0], outputSize);
                        }

                        if (frame->flags & AV_FRAME_FLAG_CORRUPT) {
                            GST_BUFFER_FLAG_SET(newBuffer, GST_BUFFER_FLAG_CORRUPTED);
                        }

                        // TODO: Reorder channels to the GStreamer channel order
                        GST_BUFFER_DTS(newBuffer)       = GST_CLOCK_TIME_NONE;
                        GST_BUFFER_TIMESTAMP(newBuffer) = this->_audioPts;

                        {
                            GstClockTime duration = GST_CLOCK_TIME_NONE;

                            if (frame->duration != AV_NOPTS_VALUE) {
                                AVRational bq = { 1, GST_SECOND };

                                GST_BUFFER_DURATION(newBuffer) = av_rescale_q(frame->duration, this->_formatCtx->streams[this->_audioStreamId]->time_base, bq);

                                this->_audioPts += GST_BUFFER_DURATION(newBuffer);
                            }
                        }

                        appSrc = GST_APP_SRC((*this->_audioSrc)->get());
                    }

                    if (newBuffer) {
                        gst_app_src_push_buffer(appSrc, newBuffer);
                    }

                    av_frame_unref(frame);
                }

                av_packet_unref(packet);
            }
        }
    }

    av_frame_free(&frame);
    av_packet_free(&packet);

    if (processVideo) {
        av_freep(&videoData[0]);
    }
}
