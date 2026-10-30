#include "utpl/common/debug.h"

#include "utpl/pipeline/utils.h"

#include "dolby.h"

using namespace utpl;

class DolbyFlowline : public transcoder::Flowline {
    public:
        DolbyFlowline(const std::string &name, const transcoder::StreamParameters &outParameters) : _name(name), _outputParameters(outParameters) {
        }

    public:
        void addOutput(transcoder::OutputStream *stream) override {
            this->_outputs[stream] = nullptr;
        }

        bool compatible(const transcoder::StreamParameters &outputParameters) override {
            return this->_outputParameters == outputParameters;
        }

        void prepare(const transcoder::StreamParameters &inParameters, pipeline::BinPtr &bin) override {
            auto &outParams = this->_outputParameters.getAudioParameters();

            auto encoder = bin->add(pipeline::Element::make("dlbpatenc"));
            auto filter  = bin->add(pipeline::Element::make("capsfilter"));
            // The live muxer holds the audio back until the video encoder delivers the same running time, and a
            // video encoder can hold well over a second (x264 frame threads and lookahead at 1080p). With a few
            // frames of room the audio path stalls back to the connector's fallback switch, which then times out
            // and drops real audio. Time-limited, so it only fills while the muxer waits.
            auto queue   = pipeline::utils::createQueue(bin, std::string(), 0, 0, 3 * GST_SECOND);

            {
                // This processor is only chosen when the configured encoder is empty or "dlbpatenc" (see
                // DolbyTranscodingProcessor::isCompatible), so every configured property belongs to this element.
                // The values arrive as strings and are deserialized into each property's own type, which the
                // enum properties (bitrate, drc-line, ...) require.
                GObject *object = G_OBJECT((*encoder)->get());

                for (auto &prop : _outputParameters.getEncoderParameters().getProperties()) {
                    auto *paramSpec = g_object_class_find_property(G_OBJECT_GET_CLASS(object), prop.first.c_str());
                    if (! paramSpec) {
                        WARN(("Encoder 'dlbpatenc' hasn't the '{}' property", prop.first));

                    } else {
                        GValue propValue = G_VALUE_INIT;

                        g_value_init(&propValue, paramSpec->value_type);

                        if (! gst_value_deserialize(&propValue, prop.second.c_str())) {
                            ERR(("Unable to set property '{}' to value '{}' for encoder 'dlbpatenc'!", prop.first, prop.second));

                        } else {
                            LOG(("Setting property '{}' to value '{}' for encoder 'dlbpatenc'", prop.first, prop.second));

                            g_object_set_property(object, prop.first.c_str(), &propValue);
                        }

                        g_value_unset(&propValue);
                    }
                }
            }

            {
                auto caps = pipeline::Caps::create();

                caps->setMediaFormat(MediaType::AUDIO, Container::ES, _outputParameters.getCodec());
                caps->setChannels(outParams.getChannels());

                filter->setProperty<GstCaps *>("caps", (*caps)->get());
            }

            encoder->link(filter);
            filter->link(queue);

            this->_encoderInputPad = encoder->getPad("sink");
            
            {
                int outputIdx = 0;
        
                if (this->_outputs.size() > 1) {
                    auto tee = bin->add(pipeline::Element::make(this->_name + "-out-tee", "tee"));
        
                    for (auto &output : this->_outputs) {
                        auto queue = bin->add(pipeline::Element::make(this->_name + "-out-queue-" + std::to_string(outputIdx), "queue"));
        
                        queue->setProperty<gboolean>("silent", true);
        
                        tee->link(queue);
        
                        output.second = bin->addPad(this->_name + "-src-" + std::to_string(outputIdx++), queue->getPad("src"));
                    }
        
                    queue->link(tee);
        
                } else {
                    auto output = this->_outputs.begin();
        
                    // Queue here is redundant because encodebin creates a queue at the end by default and Dolby PAT pipeline
                    // does the same.
                    output->second = bin->addPad(this->_name + "-src", queue->getPad("src"));
                }
            }
        }

        pipeline::PadPtr getInputPad() override {
            return this->_encoderInputPad;
        }

        const transcoder::StreamParameters &getParameters() const override {
            return this->_outputParameters;
        }

        void link() override {
            DBG(("Linking flowline to outputs"));

            for (auto &output : this->_outputs) {
                output.second->link(output.first->getPad());
            }
        }

    private:
        std::string          _name;
        pipeline::PadPtr     _encoderInputPad;
        transcoder::StreamParameters _outputParameters;

        std::unordered_map<transcoder::OutputStream *, pipeline::PadPtr> _outputs;

};

transcoder::DolbyTranscodingProcessor::DolbyTranscodingProcessor(const std::string &processorName, const transcoder::StreamParameters &outParams) : _name(processorName), _inputParameters(outParams)
{
    LOG(("Creating Audio processor for Dolby PAT encoding/transcoding"));

    this->_inputParameters.setCodec(Codec::ES);
}

std::shared_ptr<transcoder::Flowline> transcoder::DolbyTranscodingProcessor::createFlowLine(Container container, const transcoder::StreamParameters &outParams) {
    this->_flowLines.push_back(
        std::make_shared<DolbyFlowline>(this->_name + "-flowline-" + std::to_string(this->_flowLines.size()), outParams)
    );

    return *this->_flowLines.rbegin();
}

bool transcoder::DolbyTranscodingProcessor::isAudio() const {
    return this->_connector->getStreamParameters().hasAudioParameters();
}

bool transcoder::DolbyTranscodingProcessor::isVideo() const {
    return this->_connector->getStreamParameters().hasVideoParameters();
}

bool transcoder::DolbyTranscodingProcessor::isCaption() const {
    return this->_connector->getStreamParameters().hasCaptionParameters();
}

const transcoder::StreamParameters &transcoder::DolbyTranscodingProcessor::getInputParameters() {
    return this->_inputParameters;
}

void transcoder::DolbyTranscodingProcessor::prepare(pipeline::BinPtr &bin) {
    pipeline::ElementPtr converter;

    // Create and link processor chain
    {
        converter = pipeline::utils::createIdentity(bin);

        this->_decoderPad = converter->getPad("sink");
    }

    for (auto &flowLine : this->_flowLines) {
        flowLine->prepare(this->_connector->getStreamParameters(), bin);
    }

    // Link self with flowlines
    {
        pipeline::PadPtr srcPad;

        if (this->_muter) {
            srcPad = this->_muter->getPad("src");   

        } else {
            srcPad = converter->getPad("src");
        }

        {
            pipeline::ElementPtr splitter;

            if (this->_flowLines.size() > 1) {
                splitter = bin->add(pipeline::Element::make(this->_name + "-splitter", "tee"));

                srcPad->link(
                    splitter->getPad("sink")
                );
            }

            for (auto &f : this->_flowLines) {
                pipeline::PadPtr targetPad;

                if (splitter) {
                    auto queue = pipeline::utils::createQueue(bin);

                    splitter->requestPad("src_%u")->link(
                        queue->getPad("sink")
                    );

                    targetPad = queue->getPad("src");

                } else {
                    targetPad = srcPad;
                }

                targetPad->link(f->getInputPad());
            }
        }
    }

    if (this->_connector) {
        this->_connector->prepare(bin);

        // Link with connector
        this->_connector->getPad(this->getInputParameters())->link(
            this->_decoderPad
        );
    }
}

void transcoder::DolbyTranscodingProcessor::link() {
    DBG(("Linking input to processor"));

    // Link outputs
    for (auto &flowLine : this->_flowLines) {
        flowLine->link();
    }

    if (this->_connector) {
        this->_connector->link();
    }
}

const std::vector<std::shared_ptr<transcoder::Flowline>> &transcoder::DolbyTranscodingProcessor::getFlowlines() const {
    return this->_flowLines;   
}

std::shared_ptr<transcoder::Compositor> transcoder::DolbyTranscodingProcessor::getCompositor() const {
    return nullptr;
}

pipeline::ElementPtr transcoder::DolbyTranscodingProcessor::getMuter() {
    return this->_muter;
}

std::shared_ptr<transcoder::StreamConnector> transcoder::DolbyTranscodingProcessor::getConnector() {
    return this->_connector;
}

void transcoder::DolbyTranscodingProcessor::setConnector(std::shared_ptr<transcoder::StreamConnector> connector) {
    this->_connector = connector;
}

const std::string &transcoder::DolbyTranscodingProcessor::getName() {
    return this->_name;
}

void transcoder::DolbyTranscodingProcessor::blank(bool blank) {
    if (this->_muter) {
        this->_muter->setProperty("mute", blank);
    }
}

bool transcoder::DolbyTranscodingProcessor::compatible(const StreamParameters &outputParameters) {
    return 
        isCompatible(outputParameters) &&
        (this->_inputParameters.getAudioParameters() == outputParameters.getAudioParameters());
}

bool transcoder::DolbyTranscodingProcessor::isCompatible(const StreamParameters &outParameters) {
    if (outParameters.isPassthrough()) {
        return false;
    }

    if (! outParameters.hasAudioParameters()) {
        return false;
    }

    {
        auto &params = outParameters.getAudioParameters();

        if (
            outParameters.getCodec() != Codec::AC3 &&
            outParameters.getCodec() != Codec::EAC3
        ) {
            return false;
        }

        if (params.getRate() != 48000) {
            return false;
        }

        auto encoderName = outParameters.getEncoderParameters().getName();

        if (! encoderName.empty() && (encoderName != "dlbpatenc")) {
            return false;
        }
    }

    return true;
}
