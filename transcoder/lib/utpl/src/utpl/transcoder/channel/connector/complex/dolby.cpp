#include "utpl/common/debug.h"

#include "utpl/pipeline/utils.h"

#include "../../processor/transcoder/dolby.h"
#include "dolby.h"

using namespace utpl;

#define DEFAULT_CHANNELS 2
#define DEFAULT_RATE     48000

// PALOUD takes 2.0, 5.1 and 5.1.4 (and mono, widened to stereo here so that it can be upmixed)
static gint _dolbyInputChannels(gint channels) {
    if (channels <= 2) {
        return 2;
    }

    return channels >= 10 ? 10 : 6;
}

// Re-targets the converter before it negotiates a new input layout (the next playlist item, a live layout
// change). A fixed channel list cannot do this: audioconvert picks the first allowed layout whenever it
// cannot pass the input through unchanged.
static GstPadProbeReturn _followInputLayout(GstPad *pad, GstPadProbeInfo *info, gpointer filter) {
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);

    if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
        GstCaps *caps;
        gint channels;

        gst_event_parse_caps(event, &caps);

        if (gst_caps_get_size(caps) > 0 && gst_structure_get_int(gst_caps_get_structure(caps, 0), "channels", &channels)) {
            GstCaps *current = nullptr;
            GstCaps *wanted  = gst_caps_new_simple(
                "audio/x-raw",
                "rate",     G_TYPE_INT, DEFAULT_RATE,
                "channels", G_TYPE_INT, _dolbyInputChannels(channels),
                nullptr
            );

            g_object_get(filter, "caps", &current, nullptr);

            if (! current || ! gst_caps_is_equal(current, wanted)) {
                DBG(("Dolby input layout: {} channels -> {}", channels, _dolbyInputChannels(channels)));

                g_object_set(filter, "caps", wanted, nullptr);
            }

            if (current) {
                gst_caps_unref(current);
            }

            gst_caps_unref(wanted);
        }
    }

    return GST_PAD_PROBE_OK;
}

static gint _sortDlbHi(GValue *a, GValue *b) {
    auto *aName = GST_ELEMENT_NAME((GstElementFactory *) g_value_get_object(a));
    auto *bName = GST_ELEMENT_NAME((GstElementFactory *) g_value_get_object(b));

    if (strcmp(aName, bName) != 0) {
        if (strcmp(aName, "dlbpatdec") == 0) {
            return -1;

        } else if (strcmp(bName, "dlbpatdec") == 0) {
            return 1;
        }
    }

    return 0;
}

static GValueArray *_decoderSortCallback(GstElement *bin, GstPad *pad, GstCaps *caps, GValueArray *factories, void *data) {
    GValueArray *ret = g_value_array_copy(factories);

    g_value_array_sort(ret, (GCompareFunc)_sortDlbHi);

    return ret;
}

static bool _checkUseDolbyDecoder(const transcoder::StreamParameters &params) {
    if (
        (
            (params.getCodec() == Codec::AC3) || (params.getCodec() == Codec::EAC3)
        ) &&
        (params.getAudioParameters().getRate() == 48000)
    ) {
        return true;
    }

    return false;
}

transcoder::DolbyComplexConnector::DolbyComplexConnector(const std::string &name, pipeline::BinPtr channelBin, bool forLiveOutput) {
    this->_channelBin   = channelBin;
    this->_outputIsLive = forLiveOutput;
    this->_prepared     = false;
    this->_linked       = false;

    this->_outputParameters.setCodec(Codec::ES);
    this->_outputParameters.setAudioParameters(AudioParameters());

    this->_bin = pipeline::Bin::create(name);
}

std::shared_ptr<transcoder::InputStream> transcoder::DolbyComplexConnector::getStream() {
    return this->_streamCurrent;
}

const transcoder::StreamParameters &transcoder::DolbyComplexConnector::getStreamParameters() {
    return this->_outputParameters;
}

pipeline::PadPtr transcoder::DolbyComplexConnector::_getOutputPad() {
    if (this->_muter) {
        return this->_muter->getPad("src");
    
    } else {
        return this->_selector->getPad("src");
    }

    return nullptr;
}

pipeline::PadPtr transcoder::DolbyComplexConnector::peekPad() {
    return this->_getOutputPad();
}

pipeline::PadPtr transcoder::DolbyComplexConnector::getPad(const StreamParameters &parameters) {
    auto pad = this->_bin->addPad(std::string(), nullptr, true);

    this->_pads[pad] = parameters;

    return pad;
}

void transcoder::DolbyComplexConnector::prepare(pipeline::BinPtr &bin) {
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
                    AudioParameters audioParams;

                    audioParams.setChannels(DEFAULT_CHANNELS);
                    audioParams.setRate(DEFAULT_RATE);

                    p.setAudioParameters(audioParams);
                }

                this->_dummySrcPad = this->_selector->requestPad("sink_%u");
            }
        }

        // create fallback
        if (this->_outputIsLive) {
            this->_fallback = this->_bin->add(pipeline::Element::make("fallbackswitch"));

            this->_fallback->setProperty<guint64>("timeout", GST_SECOND / 2);

            // real input
            {
                auto pad = this->_fallback->requestPad("sink_%u");

                pad->setProperty<guint>("priority", 0);

                this->_selector->getPad("src")->link(
                    pad
                );
            }

            // fallback input
            {
                this->_fallbackSrcSinkPad = this->_fallback->requestPad("sink_%u");
        
                this->_fallbackSrcSinkPad->setProperty<guint>("priority", 1);

                {
                    this->_fallbackSrc = this->_bin->add(pipeline::Element::make("audiotestsrc"));
                    
                    this->_fallbackSrc->setProperty<gint>("wave", 4); // silence, 0 - 440kHz sine
                    this->_fallbackSrc->setProperty<gboolean>("is-live", true);
                }

                {
                    this->_fallbackSrcFilter = this->_bin->add(pipeline::Element::make("capsfilter"));

                    {
                        auto caps = pipeline::Caps::create();

                        caps->setMediaFormat(MediaType::AUDIO, Container::ES, this->_dummySrcParameters.getCodec());

                        caps->setChannels(this->_dummySrcParameters.getAudioParameters().getChannels());
                        caps->setRate    (this->_dummySrcParameters.getAudioParameters().getRate());

                        this->_fallbackSrcFilter->setProperty<GstCaps *>("caps", (*caps)->get());
                    }
                }

                this->_fallbackSrc->link(
                    this->_fallbackSrcFilter
                );
            }

            this->_muter = this->_bin->add(pipeline::Element::make("utplaudiomute"));

            this->_fallback->link(this->_muter);
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

void transcoder::DolbyComplexConnector::link() {
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
                    auto element = converterBin->add(
                        this->_createDolbyTransformer(
                            true, 
                            _getUpdatedStreamParamters(this->getStream() ? this->getStream()->getStreamParameters() : this->_dummySrcParameters),
                            expectedParameters,
                            false,
                            true
                        )
                    );

                    converterElements.push_back(element);
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

void transcoder::DolbyComplexConnector::unlink() {
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

void transcoder::DolbyComplexConnector::cleanup() {
    this->_bin->null();

    this->unlink();

    if (this->_prepared) {
        this->_channelBin->remove(this->_bin);

        this->_prepared = false;
    }
}

pipeline::ElementPtr transcoder::DolbyComplexConnector::getNode() {
    return this->_bin;
}

bool transcoder::DolbyComplexConnector::hasStream(std::shared_ptr<InputStream> stream) {
    return this->_streamMap.find(stream) != this->_streamMap.end();
}

void transcoder::DolbyComplexConnector::addStream(std::shared_ptr<InputStream> stream, bool isLive) {
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

void transcoder::DolbyComplexConnector::removeStream(std::shared_ptr<InputStream> stream) {
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

void transcoder::DolbyComplexConnector::setCurrentStream(std::shared_ptr<InputStream> stream) {
    std::lock_guard guard(this->_guard);

    DBG(("Setting current stream to: {}, current: {}, totalStreams: {}", (void *) stream.get(), (void *) this->_streamCurrent.get(), this->_streamMap.size()));

    if (this->_streamCurrent != stream) {
        if (stream == nullptr) {
            this->_setActiveInput(this->_dummySrcPad, this->_dummySrcParameters);

            this->_streamCurrent = stream;

        } else {
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
}

std::shared_ptr<transcoder::InputStream> transcoder::DolbyComplexConnector::getCurrentStream() {
    return this->_streamCurrent;
}

void transcoder::DolbyComplexConnector::blank(bool blank) {
    if (this->_muter) {
        this->_muter->setProperty("mute", blank);
        
    } else {
        WARN(("Audio complex connector has no muter object!"));
    }
}

pipeline::ElementPtr transcoder::DolbyComplexConnector::getMuter() {
    return this->_muter;
}

void transcoder::DolbyComplexConnector::_linkStream(InputContext &ctx, std::shared_ptr<InputStream> stream) {
    if (! ctx.selectorPad) {
        const auto &streamParameters = stream->getStreamParameters();

        ctx.selectorPad = this->_selector->requestPad("sink_%u");

        // Dolby PAT part
        {
            if (pipeline::utils::isDolbyPatCompatible(streamParameters, true)) {
                this->_createDecoder(ctx);

            } else {
                ctx.converter = this->_bin->add(pipeline::Element::make("audioconvert"));
            }

            ctx.transformer = this->_bin->add(this->_createDolbyTransformer(
                ctx.decoder.get(), _getUpdatedStreamParamters(streamParameters), streamParameters
            ));

            ctx.decoderPad = ctx.transformer->getPad("sink");

            if (! ctx.decoder) {
                ctx.converter->link(ctx.transformer);
            }
        }

        // TODO: Detemine this
        if (! ctx.isLive && this->_outputIsLive) {
            ctx.clockSync = this->_bin->add(pipeline::Element::make("clocksync"));
        }

        {
            if (ctx.decoder) {
                ctx.inputPad = this->_channelBin->addPad(std::string(), ctx.decoder->getPad("sink"));

                // A live input may switch codec on the same stream (AC-3/E-AC-3 <-> AAC); the decoder built for the
                // first codec would refuse the new caps
                ctx.inputPad->addProbe({ GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM }, [this, &ctx](GstPadProbeInfo *info) {
                    GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);

                    if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
                        GstCaps *caps;

                        gst_event_parse_caps(event, &caps);

                        if (gst_caps_get_size(caps) > 0) {
                            std::string media = gst_structure_get_name(gst_caps_get_structure(caps, 0));

                            ctx.decoderParameters = StreamParameters::fromCaps(pipeline::Caps(pipeline::GstCapsPtr::wrap(gst_caps_ref(caps))));

                            if (! ctx.decoderMedia.empty() && media != ctx.decoderMedia) {
                                LOG(("Dolby connector input changed codec: {} -> {}, rebuilding its decoder", ctx.decoderMedia, media));

                                this->_replaceDecoder(ctx);
                            }

                            ctx.decoderMedia = media;
                        }
                    }

                    return true;
                });

            } else {
                ctx.inputPad = this->_channelBin->addPad(std::string(), ctx.converter->getPad("sink"));
            }

            if (ctx.clockSync) {
                ctx.transformer->getPad("src")->link(
                    ctx.clockSync->getPad("sink")
                );

                ctx.clockSync->getPad("src")->link(
                    ctx.selectorPad
                );

            } else {
                ctx.transformer->getPad("src")->link(
                    ctx.selectorPad
                );
                
            }
        }

        {
            auto streamPad = stream->requestPad(ctx.decoder != nullptr);

            streamPad->link(ctx.inputPad);
        }

        if (ctx.clockSync) {
            ctx.clockSync->syncWithParent();
        }

        if (ctx.transformer) {
            ctx.transformer->syncWithParent();
        }

        if (ctx.decoder) {
            ctx.decoder->syncWithParent();
        }

        if (ctx.converter) {
            ctx.converter->syncWithParent();
        }
    }
}

void transcoder::DolbyComplexConnector::_createDecoder(InputContext &ctx) {
    ctx.decoder = this->_bin->add(pipeline::Element::make("decodebin"));

    // This prevents connector -> channel bins from switching to the PAUSED state. By default, decodebin starts in ASYNC mode.
    ctx.decoder->setProperty<gboolean>("async-handling", true);

    ctx.decoder->onSignal("autoplug-sort", G_CALLBACK(_decoderSortCallback), this);

    ctx.decoder->onPadAdded([this, &ctx](pipeline::PadPtr &pad) {
        bool dolby = false;

        {
            GstPad *target = gst_ghost_pad_get_target(GST_GHOST_PAD((*pad)->get()));

            if (target) {
                GstElement *parent = gst_pad_get_parent_element(target);

                if (parent) {
                    GstElementFactory *factory = gst_element_get_factory(parent);

                    dolby = factory && strcmp(GST_OBJECT_NAME(factory), "dlbpatdec") == 0;

                    gst_object_unref(parent);
                }

                gst_object_unref(target);
            }
        }

        if (dolby) {
            pad->link(ctx.decoderPad);

        } else {
            // Plain PCM (a codec the Dolby decoder does not take): convert it as the raw-input branch would
            ctx.adapter = this->_bin->add(this->_createDolbyTransformer(
                false, _getUpdatedStreamParamters(ctx.decoderParameters), ctx.decoderParameters
            ));

            pad->link(ctx.adapter->getPad("sink"));

            ctx.adapter->getPad("src")->link(ctx.decoderPad);

            ctx.adapter->syncWithParent();
        }

        return false;
    });
}

void transcoder::DolbyComplexConnector::_removeAdapter(InputContext &ctx) {
    if (ctx.adapter) {
        ctx.adapter->getPad("src")->unlink();

        ctx.adapter->null();

        this->_bin->remove(ctx.adapter);

        ctx.adapter = nullptr;
    }
}

// Runs in the streaming thread, from the input pad probe, before the new caps reach the old decoder. The old
// decoder holds no thread of its own, so it can be stopped here.
void transcoder::DolbyComplexConnector::_replaceDecoder(InputContext &ctx) {
    auto old = ctx.decoder;

    {
        auto oldSrc = old->getPads(true);

        for (auto &pad : oldSrc) {
            if (pad->getPeer()) {
                pad->unlink();
            }
        }
    }

    this->_removeAdapter(ctx);

    this->_createDecoder(ctx);

    ctx.decoder->syncWithParent();

    ctx.inputPad->setTarget(ctx.decoder->getPad("sink"));

    old->null();

    this->_bin->remove(old);
}

void transcoder::DolbyComplexConnector::_unlinkStream(InputContext &ctx, std::shared_ptr<InputStream> stream) {
    if (ctx.inputPad) {
        auto inputPeer = ctx.inputPad->getPeer();
        if (inputPeer) {
            auto inputPeerPtr = std::make_shared<pipeline::Pad>(inputPeer);

            ctx.inputPad->unlink();

            stream->releasePad(inputPeerPtr);
        }

        if (ctx.decoder) {
            ctx.decoder->null();

            this->_bin->remove(ctx.decoder);

            ctx.decoder = nullptr;
        }

        this->_removeAdapter(ctx);

        if (ctx.converter) {
            ctx.converter->null();

            this->_bin->remove(ctx.converter);

            ctx.converter = nullptr;
        }

        if (ctx.transformer) {
            ctx.transformer->null();

            this->_bin->remove(ctx.transformer);

            ctx.transformer = nullptr;
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

transcoder::StreamParameters transcoder::DolbyComplexConnector::_getUpdatedStreamParamters(const StreamParameters &src) {
    auto expectedParameters = src;

    expectedParameters.getAudioParameters().setRate(DEFAULT_RATE);

    // At this stage, we don't know how many channels will be output.
    // Since the Dolby upmixer does not support mono-to-stereo upmixing,
    // we should perform the upmix to stereo by default using GStreamer.
    if (expectedParameters.getAudioParameters().getChannels() == 1) {
        expectedParameters.getAudioParameters().setChannels(2);
    }

    return expectedParameters;
}

pipeline::ElementPtr transcoder::DolbyComplexConnector::_createDolbyTransformer(bool pcmWithMeta, const StreamParameters &inParameters, const StreamParameters &outParameters, bool silence, bool outputSide) {
    std::string dolbyPipe = "queue silent=true max-size-time=0 max-size-bytes=0 max-size-buffers=2 ! ";

    auto &outParams = outParameters.getAudioParameters();
    auto &inParams  = inParameters.getAudioParameters();

    auto inChannels = inParams.getChannels();

    // The source declares a wider layout but carries only stereo content, and Dolby PAT metadata still describes
    // the declared layout. Such input is treated as 2.0 - the mixer below is told to take only the front left/right
    // channels (in-channels=2) and upmix them to the target layout.
    bool stereoSource = pcmWithMeta && outParams.isStereoSource() && outParams.getChannels() > 2;

    if (stereoSource) {
        LOG(("Source audio is marked as stereo - upmixing its front left/right channels to {} channels", outParams.getChannels()));

        inChannels = 2;
    }

    // The source's layout is determined from the audio itself by the Dolby mixer's automatic detection, so a source
    // that switches between real 5.1 and stereo carried in a 5.1 header (silent C/LFE/surrounds) is upmixed only
    // while it is stereo. The Dolby mixer upmixes to 5.1 and 5.1.4 only.
    bool autoLayout = false;

    if (pcmWithMeta && outputSide && outParams.getInputLayout() == AudioInputLayout::AUTO) {
        if (outParams.getChannels() == 6 || outParams.getChannels() == 10) {
            LOG(("Source audio layout is detected from its content - stereo carried in a wider layout is upmixed to {} channels", outParams.getChannels()));

            autoLayout = true;

        } else {
            WARN(("audio.inputLayout 'auto' needs a 5.1 or 5.1.4 output, not {} channels - using the declared layout", outParams.getChannels()));
        }
    }

    if (! pcmWithMeta) {
        // The channel count follows each input layout (see _followInputLayout), so a new item or a live layout
        // change renegotiates instead of being folded into the first stream's layout. Mono is widened to stereo:
        // the Dolby mixer does not upmix mono.
        dolbyPipe += " audioconvert name=dlbconvert ! audioresample ! capsfilter name=dlbchannels caps=\"audio/x-raw, rate=" + std::to_string(DEFAULT_RATE)
                   + ", channels=" + std::to_string(_dolbyInputChannels(inChannels ? inChannels : DEFAULT_CHANNELS)) + "\" ! ";
    }

    std::string dlbMxParams;
    std::string dlbLoudParams;

    {
        GstRegistry *registry = gst_registry_get();
        if (registry) {
            const std::vector<std::pair<std::string, std::string &>> toCheck = {
                { "dlbpatmx",   dlbMxParams   }, 
                { "dlbpatloud", dlbLoudParams }
            };

            for (const auto &plugin : toCheck) {
                GstPluginFeature *pluginFeature = gst_registry_lookup_feature(registry, plugin.first.c_str());
                if (pluginFeature) {
                    auto *factory = GST_ELEMENT_FACTORY(gst_plugin_feature_load(pluginFeature));
                    if (factory) {
                        auto *element = gst_element_factory_create (factory, NULL);
                        if (element) {
                            guint        propertiesCount;
                            GParamSpec **properties = g_object_class_list_properties(G_OBJECT_GET_CLASS(element), &propertiesCount);

                            for (auto &toSetPropertyName : outParameters.getEncoderParameters().getProperties()) {
                                for (int propertyIdx = 0; propertyIdx < propertiesCount; propertyIdx++) {
                                    auto *prop = properties[propertyIdx];

                                    if (strcmp(toSetPropertyName.first.c_str(), prop->name) == 0) {
                                        plugin.second += (toSetPropertyName.first + "=" + toSetPropertyName.second + " ");
                                    }
                                }
                            }

                            if (properties) {
                                g_free(properties);
                            }

                            gst_object_unref(element);
                        }

                        gst_object_unref(factory);
                    }

                    gst_object_unref(pluginFeature);
                }
            }
        }
    }

    if (pcmWithMeta) {
        if (outputSide && outParams.getChannels() > 1 && outParams.getChannels() <= 10) {
            // The mixer reconfigures per block from the source channel count the blocks carry (stereo is upmixed,
            // surround passed through or rendered), whichever input, playlist item or switch put it on air.
            std::string channels = outParams.getChannels() == 2 ? "2_auto" : std::to_string(outParams.getChannels());

            dolbyPipe += "dlbpatmx out-channels=" + channels + " " + dlbMxParams;
            dolbyPipe += stereoSource ? " in-channels=2 mode=upmix" : (autoLayout ? " in-channels=source-auto" : " in-channels=source");
            dolbyPipe += " ! dlbpatloud " + dlbLoudParams;

        } else {
            // Input side of a Dolby decoder: the blocks keep the decoder's layout and the output mixers follow it.
            // Rendering here to the layout the stream started with would fold later surround sections to it.
            dolbyPipe += "identity signal-handoffs=false silent=true";
        }

    // GStreamer and FFmpeg do not support parsing Dolby Atmos content with respect to the number of channels.
    // The maximum number of channels parsed from the DD/DD+ header is 5.1.
    // Dolby PADMX supports up to 7.1.4 configuration but PATLOUD just only 10 channels. According that mixer,
    // should be disabled in that case and upmix should be done by the codec directly (silence insertion).
    } else if (
        ((inChannels != outParams.getChannels()) || (inParameters.getCodec() == Codec::EAC3 && inChannels >= 6)) &&
        inChannels > 1 && outParams.getChannels() > 1 && outParams.getChannels() <= 10
    ) {
        std::string channels = outParams.getChannels() == 2 ? "2_auto" : std::to_string(outParams.getChannels());

        dolbyPipe += "dlbpatmx out-channels=" + channels + " " + dlbMxParams + " ! dlbpatloud " + dlbLoudParams;

    } else {
        // dlbpatloud in 'off' mode only converts to the data format the Dolby PAT encoder takes. Silence (dummy and
        // fallback sources) is tagged with no source layout, so it never switches a mixer that follows the source.
        dolbyPipe += silence ? "dlbpatloud mode=off source-channels=0" : "dlbpatloud mode=off";
    }

    DBG(("Creating dolby transformer from: '{}'", dolbyPipe));

    auto bin = pipeline::Bin::parse(dolbyPipe);

    if (! pcmWithMeta && bin) {
        GstElement *converter = gst_bin_get_by_name(GST_BIN((*bin)->get()), "dlbconvert");
        GstElement *filter    = gst_bin_get_by_name(GST_BIN((*bin)->get()), "dlbchannels");

        if (converter && filter) {
            GstPad *sinkPad = gst_element_get_static_pad(converter, "sink");

            gst_pad_add_probe(sinkPad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, _followInputLayout, gst_object_ref(filter), gst_object_unref);

            gst_object_unref(sinkPad);

        } else {
            ERR(("Dolby transformer has no layout converter - input layout changes will not be followed"));
        }

        if (converter) gst_object_unref(converter);
        if (filter)    gst_object_unref(filter);
    }

    return bin;
}

void transcoder::DolbyComplexConnector::_setActiveInput(pipeline::PadPtr pad, const StreamParameters &streamParameters) {
    std::lock_guard guard(this->_guard);

    if (! this->_selector) {
        WARN(("Cannot set active input as there is no selector available!"));

    } else {
        auto expectedStreamParameters = streamParameters;

        // only 48khz sample rate is supported by DLB PAT
        {
            auto &expectedAudioParameters = expectedStreamParameters.getAudioParameters();

            expectedAudioParameters.setRate(DEFAULT_RATE);
        }

        StreamParameters targetParameters = expectedStreamParameters;

        // For dummy sources we need to limitate channles number by 6
        if (targetParameters.getAudioParameters().getChannels() > 6) {
            targetParameters.getAudioParameters().setChannels(6);
        }
        
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

            if (this->_dummySrcTransformer) {
                this->_dummySrcTransformer->null();

                this->_bin->remove(this->_dummySrcTransformer);

                this->_dummySrcTransformer = nullptr;
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
                        this->_dummySrc = this->_bin->add(pipeline::Element::make("audiotestsrc"));
                    
                        this->_dummySrc->setProperty<gint>("wave", 4); // silence, 0 - 440kHz sine
                        this->_dummySrc->setProperty<gboolean>("is-live", true);
                    }

                    this->_dummySrcFilter = this->_bin->add(pipeline::Element::make("capsfilter"));

                    this->_dummySrcTransformer = this->_bin->add(
                        this->_createDolbyTransformer(
                            false, targetParameters, expectedStreamParameters, true
                        )
                    );

                    this->_dummySrc->link(
                        this->_dummySrcFilter
                    );

                    if (this->_outputIsLive) {
                        this->_dummySrcSync = this->_bin->add(pipeline::Element::make("clocksync"));

                        this->_dummySrcFilter->link(
                            this->_dummySrcSync
                        );

                        this->_dummySrcSync->getPad("src")->link(
                            this->_dummySrcTransformer->getPad("sink")
                        );

                    } else {
                        this->_dummySrcFilter->getPad("src")->link(
                            this->_dummySrcTransformer->getPad("sink")
                        );
                    }

                    this->_dummySrcTransformer->getPad("src")->link(
                        this->_dummySrcPad
                    );
                }
            }
        }

        if (this->_fallback) {
            this->_fallbackSrc->null();

            if (this->_fallbackSrcTransformer) {
                this->_fallbackSrcTransformer->null();

                this->_bin->remove(this->_fallbackSrcTransformer);

                this->_fallbackSrcTransformer = nullptr;
            }

            this->_fallback->pause();
            this->_fallbackSrcFilter->null();
        }

        // Update dummy src and fallback caps
        {
            auto caps = pipeline::Caps::create();

            {
                const auto &aParams = expectedStreamParameters.getAudioParameters();

                caps->setMediaFormat(MediaType::AUDIO, Container::ES, Codec::ES);

                caps->setChannels(aParams.getChannels());
                caps->setRate    (aParams.getRate());
            }

            if (this->_fallbackSrcFilter) {
                this->_fallbackSrcFilter->setProperty<GstCaps *>("caps", (*caps)->get());
            }

            if (this->_dummySrcFilter) {
                this->_dummySrcFilter->setProperty<GstCaps *>("caps", (*caps)->get());
            }
        }

        // Recreate Dolby transformers
        if (this->_fallback) {
            this->_fallbackSrcTransformer = this->_bin->add(
                this->_createDolbyTransformer(
                    false, targetParameters, expectedStreamParameters, true
                )
            );

            this->_fallbackSrcFilter->getPad("src")->link(
                this->_fallbackSrcTransformer->getPad("sink")
            );

            this->_fallbackSrcTransformer->getPad("src")->link(
                this->_fallbackSrcSinkPad
            );
        }

        // Relink dummy src - caps renegotiation
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
            this->_fallbackSrcTransformer->syncWithParent();
        }

        if (this->_dummySrc) {
            this->_dummySrc->syncWithParent();
            this->_dummySrcFilter->syncWithParent();
            this->_dummySrcTransformer->syncWithParent();
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
