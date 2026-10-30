#include <gst/pbutils/encoding-profile.h>
#include <gst/audio/audio-info.h>
#include <gst/video/video-info.h>
#include <gst/gstpadtemplate.h>

#include "utpl/common/debug.h"

#include "flowline.h"

using namespace utpl;

// An upstream force-key-unit request (sent by splitmuxsink or dashsink2 to get a keyframe at a
// segment boundary) is meant for the encoder. encodebin also forwards it through its passthrough
// branch, out of its sink pad and up the raw video chain to the input's parser, which answers at
// the next *input* keyframe with a downstream request: every encoder of the channel then emits an
// extra keyframe mid-segment. The encoder inside encodebin has already received it, so it stops here.
static GstPadProbeReturn _dropUpstreamKeyUnitRequest(GstPad *, GstPadProbeInfo *info, gpointer) {
    auto *event = GST_PAD_PROBE_INFO_EVENT(info);

    if (event && GST_EVENT_TYPE(event) == GST_EVENT_CUSTOM_UPSTREAM) {
        const auto *structure = gst_event_get_structure(event);

        if (structure && gst_structure_has_name(structure, "GstForceKeyUnit")) {
            return GST_PAD_PROBE_DROP;
        }
    }

    return GST_PAD_PROBE_OK;
}

transcoder::TranscodingFlowline::TranscodingFlowline(const std::string &name, Container container, const transcoder::StreamParameters &outParameters) : _name(name), _outputParameters(outParameters) {
    this->_outputContainer = container;
}

void transcoder::TranscodingFlowline::addOutput(transcoder::OutputStream *stream) {
    this->_outputs[stream] = nullptr;
}

bool transcoder::TranscodingFlowline::compatible(const transcoder::StreamParameters &params) {
    return this->_outputParameters == params;
}

void transcoder::TranscodingFlowline::prepare(const transcoder::StreamParameters &inParameters, pipeline::BinPtr &bin) {
    {
        bool isAudio = this->_outputParameters.hasAudioParameters();

        auto caps = pipeline::Caps::create();

        this->_encoder = bin->add(pipeline::Element::make(this->_name + "-encoder", "encodebin"));

        std::string capsString;

        switch (this->_outputParameters.getCodec()) {
            case Codec::AAC:
                caps->setMediaFormat(MediaType::AUDIO, Container::ES, Codec::AAC);

                if (this->_outputContainer == Container::FMP4) {
                    caps->setString("stream-format", "raw");

                } else {
                    caps->setString("stream-format", "adts");
                }
                break;

            case Codec::AC3:
                caps->setMediaFormat(MediaType::AUDIO, Container::ES, Codec::AC3);
                break;

            case Codec::EAC3:
                caps->setMediaFormat(MediaType::AUDIO, Container::ES, Codec::EAC3);
                break;

            case Codec::MP1:
                caps->setMediaFormat(isAudio ? MediaType::AUDIO : MediaType::VIDEO, Container::ES, Codec::MP1);
                break;

            case Codec::MP2:
                caps->setMediaFormat(isAudio ? MediaType::AUDIO : MediaType::VIDEO, Container::ES, Codec::MP2);
                break;

            case Codec::MP3:  
                caps->setMediaFormat(MediaType::AUDIO, Container::ES, Codec::MP3);
                break;

            case Codec::H264: 
                caps->setMediaFormat(MediaType::VIDEO, Container::ES, Codec::H264);
                break;

            case Codec::H265: 
                caps->setMediaFormat(MediaType::VIDEO, Container::ES, Codec::H265);
                break;
        }

        if (isAudio) {
            auto &audioParams = this->_outputParameters.getAudioParameters();

            if (audioParams.getRate() > 0) {
                caps->setRate(audioParams.getRate());
            }

            if (audioParams.getChannels() > 0) {
                caps->setChannels(audioParams.getChannels());
            }

        } else {
            auto videoParams = this->_outputParameters.getVideoParameters();

            if (videoParams.getWidth() > 0) {
                caps->setWidth(videoParams.getWidth());
            }

            if (videoParams.getHeight() > 0) {
                caps->setHeight(videoParams.getHeight());
            }

            // Pixel format
            {
                this->_filters.push_back(bin->add(pipeline::Element::make("videoconvert")));
                this->_filters.push_back(bin->add(pipeline::Element::make("capsfilter")));

                {
                    auto caps = pipeline::Caps::create();

                    caps->setMediaFormat(MediaType::VIDEO, Container::ES, Codec::ES);
                    caps->setFormat(videoParams.getPixelFormat());

                    (*this->_filters.rbegin())->setProperty("caps", (*caps)->get());
                }
            }

            const bool interleaved = videoParams.getInterlace() == Interlace::INTERLEAVED;

            // An interleaved output is woven from progressive frames at twice its frame rate.
            auto progressiveRate = videoParams.getFramerate();

            if (interleaved) {
                progressiveRate = Fraction(progressiveRate.getNumerator() * 2, progressiveRate.getDenominator());
            }

            // MPEG-1/2 encoders (avenc*) do not support forwarding framerate value to src pad.
            // It make pipeline creation impossible becasuse of incompatible caps.
            if (progressiveRate > 0) {
                auto &inVideoParams = inParameters.getVideoParameters();

                if (inVideoParams.getFramerate() != progressiveRate) {
                    {
                        auto rateConv = bin->add(pipeline::Element::make("videorate"));

                        rateConv->setProperty<gboolean>("skip-to-first", true);

                        this->_filters.push_back(rateConv);
                    }

                    this->_filters.push_back(bin->add(pipeline::Element::make("capsfilter")));

                    {
                        auto caps = pipeline::Caps::create();

                        caps->setMediaFormat(MediaType::VIDEO, Container::ES, Codec::ES);
                        caps->setFrameRate(progressiveRate);

                        (*this->_filters.rbegin())->setProperty("caps", (*caps)->get());
                    }
                }
            }

            // Pairs of frames become one frame of two fields; the encoder picks the scan type up from its input caps.
            if (interleaved) {
                auto interlace = bin->add(pipeline::Element::make("interlace"));

                interlace->setProperty<gint>("field-pattern", 0); // 1:1
                interlace->setProperty<gboolean>("top-field-first", videoParams.getFieldOrder() == FieldOrder::TOP_FIELD_FIRST);

                this->_filters.push_back(interlace);
            }
        }

        {
            const auto &encoderParams = _outputParameters.getEncoderParameters();

            GstEncodingProfile *profile;

            GstElementFactory *encoderFactory = nullptr;

            if (! encoderParams.getName().empty()) {
                encoderFactory = gst_element_factory_find(encoderParams.getName().c_str());
                if (! encoderFactory) {
                    WARN(("Element factory '{}' is not available!", encoderParams.getName()));
                }
            }

            if (encoderFactory) {
                for (const GList *tmp = gst_element_factory_get_static_pad_templates(encoderFactory); tmp; tmp = g_list_next(tmp)) {
                    auto *padTemplate = reinterpret_cast<GstStaticPadTemplate *>(tmp->data);

                    if (padTemplate->direction == GST_PAD_SRC) {
                        auto padCaps = pipeline::GstCapsPtr::wrap(
                            gst_caps_fixate(
                                gst_static_pad_template_get_caps(padTemplate)
                            )
                        );

                        auto *padStr = gst_caps_get_structure(*padCaps, 0);

                        for (auto &cap : encoderParams.getCapabilities()) {
                            if (! gst_structure_has_field(padStr, cap.first.c_str())) {
                                WARN(("Encoder '{}' hasn't the '{}' capability", encoderParams.getName(), cap.first));

                            } else {
                                GValue val = G_VALUE_INIT;

                                g_value_init(&val, gst_structure_get_field_type(padStr, cap.first.c_str()));

                                LOG(("Setting capability '{}' to value '{}' for encoder '{}'", cap.first, cap.second, encoderParams.getName()));
                                
                                if (! gst_value_deserialize(&val, cap.second.c_str())) {
                                    ERR(("Unable to set capability '{}' to value '{}'!", cap.first, cap.second));

                                } else {
                                    caps->setValue(cap.first, &val);
                                }

                                g_value_unset(&val);
                            }
                        }
                    }
                }
            }

            DBG(("Encoder profile will be set to caps generated from the following string: '{}'", caps->toString()));

            if (isAudio) {
                profile = (GstEncodingProfile *) gst_encoding_audio_profile_new((*caps)->get(), NULL, NULL, 0);

            } else {
                profile = (GstEncodingProfile *) gst_encoding_video_profile_new((*caps)->get(), NULL, NULL, 0);
            }

            if (encoderFactory) {
                GstStructure *properties = gst_structure_new_empty("element-properties-map");

                {
                    GValue list = G_VALUE_INIT;

                    gst_value_list_init(&list, 1);
                    {
                        GstStructure *codecProperties = gst_structure_new_empty(encoderParams.getName().c_str());

                        {
                            encoderFactory = GST_ELEMENT_FACTORY(gst_plugin_feature_load(GST_PLUGIN_FEATURE(encoderFactory)));

                            auto *gstEncoderClass = (GObjectClass *) g_type_class_ref(gst_element_factory_get_element_type(encoderFactory));

                            for (auto &prop : encoderParams.getProperties()) {
                                auto *gstParamSpec = g_object_class_find_property(gstEncoderClass, prop.first.c_str());
                                if (! gstParamSpec) {
                                    WARN(("Encoder '{}' hasn't the '{}' property", encoderParams.getName(), prop.first));

                                } else {
                                    GValue propValue = G_VALUE_INIT;

                                    g_value_init(&propValue, gstParamSpec->value_type);

                                    LOG(("Setting property '{}' to value '{}' for encoder '{}'", prop.first, prop.second, encoderParams.getName()));

                                    if (! gst_value_deserialize(&propValue, prop.second.c_str())) {
                                        ERR(("Unable to set property '{}' to value '{}'!", prop.first, prop.second));

                                    } else {
                                        gst_structure_set_value(codecProperties, prop.first.c_str(), &propValue);
                                    }

                                    g_value_unset(&propValue);
                                }
                            }
                        }

                        {
                            GValue codecPropertiesValue = G_VALUE_INIT;

                            g_value_init(&codecPropertiesValue, GST_TYPE_STRUCTURE);
                            {
                                gst_value_set_structure(&codecPropertiesValue, codecProperties);

                                gst_value_list_append_value(&list, &codecPropertiesValue);
                            }
                            g_value_unset(&codecPropertiesValue);
                        }

                        gst_structure_free(codecProperties);

                        gst_structure_set_value(properties, "map", &list);
                    }
                    g_value_unset(&list);
                }

                gst_encoding_profile_set_preset_name(profile, encoderParams.getName().c_str());

                gst_encoding_profile_set_element_properties(profile, properties);
            }

            this->_encoder->setProperty("profile", profile);
        }
    }

    if (! this->_filters.empty()) {
        for (int i = 0; i < this->_filters.size() - 1; i++) {
            this->_filters[i]->link(this->_filters[i + 1]);
        }

        this->_encoderInputPad = this->_filters.begin()->get()->getPads(false)[0];

        // Link filters to encoder
        (*this->_filters.rbegin())->link(this->_encoder);

    } else {
        this->_encoderInputPad = this->_encoder->getPads(false)[0];
    }

    for (auto &pad : this->_encoder->getPads(false)) {
        gst_pad_add_probe((*pad)->get(), GST_PAD_PROBE_TYPE_EVENT_UPSTREAM, _dropUpstreamKeyUnitRequest, nullptr, nullptr);
    }

    {
        int outputIdx = 0;

        if (this->_outputs.size() > 1) {
            auto tee = bin->add(pipeline::Element::make(this->_name + "-out-tee", "tee"));

            for (auto &output : this->_outputs) {
                auto queue = bin->add(pipeline::Element::make(this->_name + "-out-queue-" + std::to_string(outputIdx), "queue"));

                queue->setProperty<gboolean>("silent", true);

                tee->link(queue);

                output.second = bin->addPad(this->_name + "-src-" + std::to_string(outputIdx++), queue->getPads(true)[0]);
            }

            this->_encoder->link(tee);

        } else {
            auto output = this->_outputs.begin();

            // Queue here is redundant because encodebin creates a queue at the end by default and Dolby PAT pipeline
            // does the same.
            output->second = bin->addPad(this->_name + "-src", this->_encoder->getPad("src"));
        }
    }
}

pipeline::PadPtr transcoder::TranscodingFlowline::getInputPad() {
    return this->_encoderInputPad;
}

const transcoder::StreamParameters &transcoder::TranscodingFlowline::getParameters() const {
    return this->_outputParameters;
}

void transcoder::TranscodingFlowline::link() {
    DBG(("Linking flowline to outputs"));

    for (auto &output : this->_outputs) {
        output.second->link(output.first->getPad());
    }
}