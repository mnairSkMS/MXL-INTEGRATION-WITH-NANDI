#include "utpl/common/debug.h"

#include "utpl/pipeline/utils.h"

#include "video.h"

using namespace utpl;

#define DEFAULT_WIDTH     1280
#define DEFAULT_HEIGHT     720
#define DEFAULT_PXFMT     PixelFormat::BGRA
#define DEFAULT_FRAMERATE { 24, 1 }


transcoder::VideoComplexConnector::VideoComplexConnector(const std::string &name, pipeline::BinPtr channelBin, bool forLiveOutput) {
    this->_channelBin   = channelBin;
    this->_outputIsLive = forLiveOutput;
    this->_prepared     = false;
    this->_linked       = false;

    this->_outputParameters.setCodec(Codec::ES);
    this->_outputParameters.setVideoParameters(VideoParameters());

    this->_bin = pipeline::Bin::create(name);
}

std::shared_ptr<transcoder::InputStream> transcoder::VideoComplexConnector::getStream() {
    return this->_streamCurrent;
}

const transcoder::StreamParameters &transcoder::VideoComplexConnector::getStreamParameters() {
    return this->_outputParameters;
}

pipeline::PadPtr transcoder::VideoComplexConnector::_getOutputPad() {
    if (this->_compositor) {
        return this->_compositor->getSrcPad();
        
    } else {
        return this->_deinterlace->getPad("src");
    }

    return nullptr;
}

pipeline::PadPtr transcoder::VideoComplexConnector::peekPad() {
    return this->_getOutputPad();
}

pipeline::PadPtr transcoder::VideoComplexConnector::getPad(const StreamParameters &parameters) {
    auto pad = this->_bin->addPad(std::string(), nullptr, true);

    this->_pads[pad] = parameters;

    return pad;
}

void transcoder::VideoComplexConnector::prepare(pipeline::BinPtr &bin) {
    std::lock_guard guard(this->_guard);

    if (! this->_prepared) {
        this->_channelBin->add(this->_bin);

        {
            this->_selector = this->_bin->add(pipeline::Element::make("input-selector"));

            // An inactive pad must drop its data, not hold it. With the default
            // sync-streams the selector parks an inactive pad inside
            // gst_input_selector_wait_running_time until the active pad catches up, so a
            // source that is off air and not advancing - a frozen ad playlist, or the
            // dummy source itself - leaves that pad's streaming thread stuck. Any later
            // state change on the stuck branch then waits on that thread forever.
            this->_selector->setProperty<gboolean>("sync-streams", false);

            gst_segment_init(&this->_selectorSegment, GST_FORMAT_TIME);

            auto selectorSrcPad = this->_selector->getPad("src");

            selectorSrcPad->addProbe(
                { 
                    GST_PAD_PROBE_TYPE_BUFFER,
                    GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM
                },
                [this, selectorSrcPad](GstPadProbeInfo *info) {
                    auto infoType = GST_PAD_PROBE_INFO_TYPE(info);

                    if (infoType & GST_PAD_PROBE_TYPE_BUFFER) {
                        GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);

                        GstClockTime timestamp = GST_BUFFER_TIMESTAMP(buffer);
                        GstClockTime duration  = GST_BUFFER_DURATION(buffer);

                        if (timestamp == GST_CLOCK_TIME_NONE) {
                            timestamp = this->_selectorSegment.position;
                        }

                        if (duration != GST_CLOCK_TIME_NONE) {
                            timestamp += duration;
                        }

                        this->_selectorSegment.position = timestamp;

                    } else if (infoType & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
                        GstEvent    *event     = GST_PAD_PROBE_INFO_EVENT(info);
                        GstEventType eventType = GST_EVENT_TYPE(event);

                        if (eventType == GST_EVENT_SEGMENT) {
                            gst_event_copy_segment(event, &this->_selectorSegment);
                        }
                    }

                    return true;
                }
            );

            // Configure dummy input
            {
                auto &p = this->_dummySrcParameters;

                p.setCodec(Codec::ES);
                
                {
                    VideoParameters videoParams;

                    videoParams.setPixelFormat(DEFAULT_PXFMT);
                    videoParams.setWidth(DEFAULT_WIDTH);
                    videoParams.setHeight(DEFAULT_HEIGHT);
                    videoParams.setInterlace(Interlace::PROGRESSIVE);

                    p.setVideoParameters(videoParams);
                }

                // The downstream processor registers its required raw-video parameters
                // before prepare(). Use that contract as a stable compositor canvas. This
                // is the in-engine equivalent of the old playout feeder's fixed caps chain.
                for (const auto &padEntry : this->_pads) {
                    const auto &parameters = padEntry.second;
                    if (parameters.hasVideoParameters()) {
                        const auto &video = parameters.getVideoParameters();
                        if (video.getWidth() > 0 && video.getHeight() > 0) {
                            p.setVideoParameters(video);
                            this->_outputParameters.setVideoParameters(video);
                            break;
                        }
                    }
                }

                this->_dummySrcPad = this->_selector->requestPad("sink_%u");
            }

            this->_deinterlace = this->_bin->add(pipeline::Element::make("deinterlace"));

            this->_selector->link(
                this->_deinterlace
            );
        }

        if (this->_outputIsLive) {
            {
                this->_fallback = this->_bin->add(pipeline::Element::make("fallbackswitch"));

                this->_fallback->setProperty<guint64>("timeout", GST_SECOND / 2);

                // real input
                {
                    auto pad = this->_fallback->requestPad("sink_%u");

                    pad->setProperty<guint>("priority", 0);

                    this->_deinterlace->getPad("src")->link(
                        pad
                    );
                }

                // fallback input
                {
                    auto pad = this->_fallback->requestPad("sink_%u");
            
                    pad->setProperty<guint>("priority", 1);

                    {
                        this->_fallbackSrc = this->_bin->add(pipeline::Element::make("videotestsrc"));
                        
                        this->_fallbackSrc->setProperty<gint>("pattern", 2);
                        this->_fallbackSrc->setProperty<gboolean>("is-live", true);
                    }

                    {
                        this->_fallbackSrcFilter = this->_bin->add(pipeline::Element::make("capsfilter"));

                        {
                            auto caps = pipeline::Caps::create();

                            caps->setMediaFormat(MediaType::VIDEO, Container::ES, Codec::ES);
                            caps->setFrameRate(DEFAULT_FRAMERATE);

                            this->_fallbackSrcFilter->setProperty<GstCaps *>("caps", (*caps)->get());
                        }

                        this->_fallbackSrcFilter->getPad("src")->link(
                            pad
                        );
                    }

                    this->_fallbackSrc->link(
                        this->_fallbackSrcFilter
                    );
                }
            }

            this->_compositor = std::make_shared<Compositor>(true);

            this->_compositor->prepare(this->_dummySrcParameters.getVideoParameters(), this->_bin);

            // Link to compositor
            this->_fallback->getPad("src")->link(
                this->_compositor->getVideoLayer()->getSinkPad()
            );
        }

        // Link streams if not yet linked
        for (auto &entry : this->_streamMap) {
            this->_linkStream(entry.second, entry.first);
        }

        if (this->_streamCurrent) {
            auto it = this->_streamMap.find(this->_streamCurrent);
            if (it != this->_streamMap.end()) {
                it->first->synchronizeClock(
                    gst_segment_to_running_time(&this->_selectorSegment, GST_FORMAT_TIME, this->_selectorSegment.position)
                );

                this->_setActiveInput(it->second.selectorPad, this->_streamCurrent->getStreamParameters());
            }

        } else {
            this->_setActiveInput(this->_dummySrcPad, this->_dummySrcParameters);
        }

        this->_prepared = true;
    }
}

void transcoder::VideoComplexConnector::link() {
    if (! this->_linked) {
        pipeline::PadPtr pad = this->_getOutputPad();

        if (this->_pads.size() > 1) {
            this->_splitter = this->_bin->add(pipeline::Element::make("tee"));

            pad->link(this->_splitter->getPad("sink"));
        }

        for (auto &padEntry : this->_pads) {
            auto converterBin = pipeline::Bin::create();

            {
                std::vector<pipeline::ElementPtr> converterElements;

                if (this->_splitter) {
                    converterElements.push_back(pipeline::utils::createQueue(converterBin, std::string(), 16, 0, 0));
                }

                const auto &expectedParameters = padEntry.second;

                {
                    auto filter = converterBin->add(pipeline::Element::make("capsfilter"));

                    {
                        auto caps = pipeline::Caps::create();

                        caps->setMediaFormat(MediaType::VIDEO, Container::ES, Codec::ES);

                        filter->setProperty<GstCaps *>("caps", (*caps)->get());
                    }

                    converterElements.push_back(filter);
                }

                {
                    auto filter = converterBin->add(pipeline::Element::make("capsfilter"));

                    converterElements.push_back(
                        converterBin->add(pipeline::Element::make("videoconvertscale"))
                    );

                    // Disable borders - scale ignoring aspect ratio
                    (*converterElements.rbegin())->setProperty<gboolean>("add-borders", false);

                    {
                        auto caps = pipeline::Caps::create();

                        caps->setMediaFormat(MediaType::VIDEO, Container::ES, Codec::ES);

                        {
                            const auto &videoParams = expectedParameters.getVideoParameters();

                            caps->setFormat(videoParams.getPixelFormat());
                            caps->setWidth (videoParams.getWidth());
                            caps->setHeight(videoParams.getHeight());

                            caps->setFraction("pixel-aspect-ratio", { 1, 1 });
                        }

                        filter->setProperty<GstCaps *>("caps", (*caps)->get());
                    }

                    converterElements.push_back(filter);
                }

                {
                    auto filter = converterBin->add(pipeline::Element::make("capsfilter"));

                    converterElements.push_back(
                        converterBin->add(pipeline::Element::make("videorate"))
                    );

                    (*converterElements.rbegin())->setProperty<gboolean>("skip-to-first", true);

                    {
                        auto caps = pipeline::Caps::create();

                        caps->setMediaFormat(MediaType::VIDEO, Container::ES, Codec::ES);

                        {
                            const auto &videoParams = expectedParameters.getVideoParameters();

                            caps->setFrameRate(videoParams.getFramerate());
                        }

                        filter->setProperty<GstCaps *>("caps", (*caps)->get());
                    }

                    converterElements.push_back(filter);
                }

                {
                    pipeline::ElementPtr lastElement;

                    for (auto &el : converterElements) {
                        if (lastElement) {
                            lastElement->link(el);
                        }

                        lastElement = el;
                    }
                }

                converterBin->addPad("sink", (*converterElements.begin())->getPad("sink"));
                converterBin->addPad("src",  (*converterElements.rbegin())->getPad("src"));

                this->_bin->add(converterBin);
            }

            if (this->_splitter) {
                this->_splitter->requestPad("src_%u")->link(
                    converterBin->getPad("sink")
                );

            } else {
                pad->link(converterBin->getPad("sink"));
            }

            padEntry.first->setTarget(converterBin->getPad("src"));
        }

        this->_linked = true;
    }
}

void transcoder::VideoComplexConnector::unlink() {
    if (this->_linked) {
        auto pad = this->_selector->getPad("src");

        for (auto &entry : this->_streamMap) {
            auto &inCtx = entry.second;

            this->_unlinkStream(inCtx, entry.first);
        }

        if (pad->getPeer()) {
            pad->unlink();
        }

        if (this->_splitter) {
            for (auto &padPtr : this->_splitter->getPads(true)) {
                if (padPtr->getPeer()) {
                    auto pad    = pipeline::Pad(padPtr->getPeer());
                    auto parent = pipeline::Element(pad.getParent());

                    if (parent) {
                        this->_bin->remove(this->_bin->get(parent.getName()));
                    }
                }
            }

            this->_bin->remove(this->_splitter);
            this->_splitter = nullptr;
        }

        this->_linked = false;
    }
}

void transcoder::VideoComplexConnector::cleanup() {
    this->_bin->null();

    this->unlink();

    if (this->_prepared) {
        this->_channelBin->remove(this->_bin);

        this->_prepared = false;
    }
}

pipeline::ElementPtr transcoder::VideoComplexConnector::getNode() {
    return this->_bin;
}

void transcoder::VideoComplexConnector::_linkStream(InputContext &ctx, std::shared_ptr<InputStream> stream) {
    if (! ctx.selectorPad) {
        ctx.selectorPad = this->_selector->requestPad("sink_%u");

        if (! ctx.isLive && this->_outputIsLive) {
            ctx.clockSync = this->_bin->add(pipeline::Element::make("clocksync"));
        }

        if (ctx.clockSync) {
            ctx.inputPad = this->_channelBin->addPad(std::string(), ctx.clockSync->getPad("sink"));

        } else {
            ctx.inputPad = this->_channelBin->addPad(std::string(), ctx.selectorPad);
        }

        {
            auto streamPad = stream->requestPad(false);

            streamPad->link(ctx.inputPad);
        }

        if (ctx.clockSync) {
            ctx.clockSync->getPad("src")->link(
                ctx.selectorPad
            );
        }

        if (ctx.clockSync) {
            ctx.clockSync->syncWithParent();
        }
    }
}

void transcoder::VideoComplexConnector::_unlinkStream(InputContext &ctx, std::shared_ptr<InputStream> stream) {
    if (ctx.inputPad) {
        auto inputPeer = ctx.inputPad->getPeer();
        if (inputPeer) {
            auto inputPeerPtr = std::make_shared<pipeline::Pad>(inputPeer);

            ctx.inputPad->unlink();

            stream->releasePad(inputPeerPtr);
        }

        if (ctx.clockSync) {
            ctx.clockSync->null();

            this->_bin->remove(ctx.clockSync);

            ctx.clockSync = nullptr;
        }

        this->_channelBin->releasePad(ctx.inputPad);
        ctx.inputPad = nullptr;

        if (ctx.selectorPad) {
            this->_selector->releasePad(ctx.selectorPad);

            ctx.selectorPad = nullptr;
        }
    }
}

bool transcoder::VideoComplexConnector::hasStream(std::shared_ptr<InputStream> stream) {
    return this->_streamMap.find(stream) != this->_streamMap.end();
}

void transcoder::VideoComplexConnector::addStream(std::shared_ptr<InputStream> stream, bool isLive) {
    std::lock_guard guard(this->_guard);

    auto it = this->_streamMap.find(stream);
    if (it != this->_streamMap.end()) {
        ERR(("Stream {} is already added", (void *) stream.get()));

    } else {
        it = this->_streamMap.emplace(std::make_pair(stream, InputContext(isLive))).first;

        if (this->_prepared) {
            this->_linkStream(it->second, stream);
        }
    }
}

void transcoder::VideoComplexConnector::removeStream(std::shared_ptr<InputStream> stream) {
    std::lock_guard guard(this->_guard);

    DBG(("Removing stream: {}, currentStream: {}, totalStreams: {}", (void *) stream.get(), (void *) this->_streamCurrent.get(), this->_streamMap.size()));

    auto it = this->_streamMap.find(stream);
    if (it == this->_streamMap.end()) {
        ERR(("Stream {} has not been found in local list", (void *) stream.get()));

    } else {
        this->_unlinkStream(it->second, it->first);

        this->_streamMap.erase(it);

        if (this->_streamCurrent == stream) {
            this->_streamCurrent = nullptr;


            this->_setActiveInput(this->_dummySrcPad, this->_dummySrcParameters);
        }
    }
}

void transcoder::VideoComplexConnector::setCurrentStream(std::shared_ptr<InputStream> stream) {
    std::lock_guard guard(this->_guard);

    DBG(("Setting current stream to: {}, current: {}, totalStreams: {}", (void *) stream.get(), (void *) this->_streamCurrent.get(), this->_streamMap.size()));

    if (this->_streamCurrent != stream) {
        auto it = this->_streamMap.find(stream);
        if (it == this->_streamMap.end()) {
            ERR(("Stream {} has not been found in local list", (void *) stream.get()));

        } else {
            if (! this->_prepared) {
                DBG(("Postponing setting current stream as input is not yet prepared"));

            } else {
                stream->synchronizeClock(
                    gst_segment_to_running_time(&this->_selectorSegment, GST_FORMAT_TIME, this->_selectorSegment.position)
                );

                this->_setActiveInput(it->second.selectorPad, stream->getStreamParameters());
            }

            this->_streamCurrent = stream;
        }
    }
}

std::shared_ptr<transcoder::InputStream> transcoder::VideoComplexConnector::getCurrentStream() {
    return this->_streamCurrent;
}

void transcoder::VideoComplexConnector::_setActiveInput(pipeline::PadPtr pad, const StreamParameters &streamParameters) {
    std::lock_guard guard(this->_guard);

    if (! this->_selector) {
        WARN(("Cannot set active input as there is no selector available!"));

    } else {
        {
            if (this->_dummySrc) {
                this->_dummySrc->null();

                this->_bin->remove(this->_dummySrc);

                this->_dummySrc = nullptr;
            }

            if (this->_dummySrcFilter) {
                this->_dummySrcFilter->null();

                this->_bin->remove(this->_dummySrcFilter);

                this->_dummySrcFilter = nullptr;
            }

            if (this->_dummySrcSync) {
                this->_dummySrcSync->null();

                this->_bin->remove(this->_dummySrcSync);

                this->_dummySrcSync = nullptr;
            }

            {
                bool createDummySrc = true;

                if (pad != this->_dummySrcPad) {
                    if (! this->_outputIsLive) {
                        createDummySrc = false;
                    }
                }

                if (createDummySrc) {
                    {
                        this->_dummySrc = this->_bin->add(pipeline::Element::make("videotestsrc"));
                        
                        this->_dummySrc->setProperty<gint>("pattern", 2);//13);
                        this->_dummySrc->setProperty<gboolean>("is-live", true);
                    }

                    this->_dummySrcFilter = this->_bin->add(pipeline::Element::make("capsfilter"));

                    if (this->_outputIsLive) {
                        this->_dummySrcSync = this->_bin->add(pipeline::Element::make("clocksync"));
                    }

                    // link
                    {
                        this->_dummySrc->link(
                            this->_dummySrcFilter
                        );

                        if (this->_dummySrcSync) {
                            this->_dummySrcFilter->link(
                                this->_dummySrcSync
                            );

                            this->_dummySrcSync->getPad("src")->link(
                                this->_dummySrcPad
                            );

                        } else {
                            this->_dummySrcFilter->getPad("src")->link(
                                this->_dummySrcPad
                            );
                        }
                    }
                }
            }
        }

        if (this->_fallback) {
            this->_fallbackSrc->null();

            this->_fallback->pause();
            this->_fallbackSrcFilter->null();
        }

        {
            auto caps = pipeline::Caps::create();

            const auto &vParams = this->_dummySrcParameters.getVideoParameters();

            caps->setMediaFormat(MediaType::VIDEO, Container::ES, Codec::ES);

            if (vParams.getWidth()) {
                caps->setWidth(vParams.getWidth());

            } else {
                caps->setWidth(DEFAULT_WIDTH);
            }

            if (vParams.getHeight()) {
                caps->setHeight(vParams.getHeight());

            } else {
                caps->setHeight(DEFAULT_HEIGHT);
            }
            
            if (vParams.getFramerate()) {
                caps->setFrameRate(vParams.getFramerate());
                
            } else {
                caps->setFrameRate(DEFAULT_FRAMERATE);
            }
            
            if (vParams.getPixelFormat() != PixelFormat::UNKNOWN) {
                caps->setFormat(vParams.getPixelFormat());
            }

            if (this->_fallbackSrcFilter) {
                this->_fallbackSrcFilter->setProperty<GstCaps *>("caps", (*caps)->get());
            }

            if (this->_dummySrcFilter) {
                this->_dummySrcFilter->setProperty<GstCaps *>("caps", (*caps)->get());
            }
        }

        if (this->_compositor) {
            Rectangle newWindow;

            newWindow.width(
                this->_dummySrcParameters.getVideoParameters().getWidth()
            );

            newWindow.height(
                this->_dummySrcParameters.getVideoParameters().getHeight()
            );

            if (newWindow.width() > 0 && newWindow.height() > 0) {
                auto &videoParams = this->_dummySrcParameters.getVideoParameters();

                {
                    auto caps = pipeline::Caps::create();

                    caps->setMediaFormat(MediaType::VIDEO, Container::ES, Codec::ES);

                    caps->setWidth(newWindow.width());
                    caps->setHeight(newWindow.height());

                    if (videoParams.getFramerate()) {
                        caps->setFrameRate(videoParams.getFramerate());                        

                    } else {
                        caps->setFrameRate(DEFAULT_FRAMERATE);   
                    }

                    if (videoParams.getPixelFormat() != PixelFormat::UNKNOWN) {
                        caps->setFormat(
                            videoParams.getPixelFormat()
                        );
                    }

                    this->_fallbackSrcFilter->setProperty<GstCaps *>("caps", (*caps)->get());
                }

                this->_compositor->setWindowSize(newWindow);
            }
        }

        {
            auto peer = pipeline::Pad(this->_dummySrcPad->getPeer());
            if (peer) {
                this->_dummySrcPad->unlink();

                peer.link(this->_dummySrcPad);
            }
        }

        if (this->_fallback) {
            this->_fallbackSrc->syncWithParent();
            this->_fallbackSrcFilter->syncWithParent();
        }

        if (this->_dummySrc) {
            this->_dummySrc->syncWithParent();
            this->_dummySrcFilter->syncWithParent();
        }

        if (this->_dummySrcSync) {
            this->_dummySrcSync->syncWithParent();
        }

        if (this->_fallback) {
            if (pad != this->_dummySrcPad) {
                this->_fallback->setProperty<gboolean>("auto-switch", false);
                this->_fallback->setProperty<GstPad *>("active-pad", (*this->_fallback->getPad("sink_0"))->get());
                this->_fallback->setProperty<gboolean>("auto-switch", true);

            } else {
                this->_fallback->setProperty<gboolean>("auto-switch", false);
                this->_fallback->setProperty<GstPad *>("active-pad", (*this->_fallback->getPad("sink_1"))->get());
            }

            this->_fallback->syncWithParent();
        }

        this->_selector->setProperty<GstPad *>("active-pad", (*pad)->get());
    }
}

std::shared_ptr<transcoder::Compositor> transcoder::VideoComplexConnector::getCompositor() {
    return this->_compositor;
}
