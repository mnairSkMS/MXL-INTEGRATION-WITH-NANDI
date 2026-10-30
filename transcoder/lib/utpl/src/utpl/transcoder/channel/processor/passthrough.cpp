#include "utpl/common/debug.h"

#include "utpl/pipeline/utils.h"

#include "passthrough.h"

using namespace utpl;


class PassthroughFlowline : public transcoder::Flowline {
    public:
        PassthroughFlowline(const std::string &name, const transcoder::StreamParameters &outParameters) : _name(name), _outputParameters(outParameters) {
        }

        void addOutput(transcoder::OutputStream *stream) override {
            this->outputs[stream] = nullptr;
        }

        bool compatible(const transcoder::StreamParameters &outputParameters) override {
            return this->_outputParameters == outputParameters;
        }

        void prepare(const transcoder::StreamParameters &inParameters, pipeline::BinPtr &bin) override {
            this->_encoder = pipeline::utils::createIdentity(bin, this->_name);

            if (this->outputs.empty()) {
                WARN(("No outputs is assigned to the flowline {}", this->_name));

            } else {
                pipeline::ElementPtr splitter;

                if (this->outputs.size() > 1) {
                    splitter = bin->add(pipeline::Element::make(this->_name + "-splitter", "tee"));

                    this->_encoder->link(splitter);
                }

                size_t outputIdx = 0;
                for (auto &output : this->outputs) {
                    pipeline::PadPtr targetPad;

                    if (splitter) {
                        auto queue = pipeline::utils::createQueue(bin);

                        splitter->requestPad("src_%u")->link(
                            queue->getPad("sink")
                        );

                        targetPad = queue->getPad("src");

                    } else {
                        targetPad = this->_encoder->getPad("src");
                    }

                    output.second = bin->addPad(this->_name + "-src-" + std::to_string(outputIdx++), targetPad);
                }
            }
        }

        pipeline::PadPtr getInputPad() override {
            return this->_encoder->getPad("sink");
        }

        const transcoder::StreamParameters &getParameters() const override {
            return this->_outputParameters;
        }

        void link() override {
            DBG(("Linking flowline to outputs"));

            for (auto &output : this->outputs) {
                output.second->link(output.first->getPad());
            }
        }

    private:
        std::string                  _name;
        pipeline::ElementPtr         _encoder;
        transcoder::StreamParameters _outputParameters;

        std::unordered_map<transcoder::OutputStream *, pipeline::PadPtr> outputs;
};


transcoder::PassthroughProcessor::PassthroughProcessor(const std::string &processorName, const transcoder::StreamParameters &outParams)
: _name(processorName), _outputParameters(outParams)
{
    this->_inputParameters = outParams;
}

std::shared_ptr<transcoder::Flowline> transcoder::PassthroughProcessor::createFlowLine(Container container, const transcoder::StreamParameters &outParams) {
    this->_flowLines.push_back(
        std::make_shared<PassthroughFlowline>(this->_name + "-flowline-" + std::to_string(this->_flowLines.size()), outParams)
    );

    return *this->_flowLines.rbegin();
}

bool transcoder::PassthroughProcessor::isAudio() const {
    return this->_connector->getStreamParameters().hasAudioParameters();
}

bool transcoder::PassthroughProcessor::isVideo() const {
    return this->_connector->getStreamParameters().hasVideoParameters();
}

bool transcoder::PassthroughProcessor::isCaption() const {
    return this->_connector->getStreamParameters().hasCaptionParameters();
}

const transcoder::StreamParameters &transcoder::PassthroughProcessor::getInputParameters() {
    return this->_inputParameters;
}

void transcoder::PassthroughProcessor::prepare(pipeline::BinPtr &bin) {
    // decoder
    if (this->isVideo()) {
        std::string parserName;

        // H264, H265 are parsed by decodebin element in input object. It creates Xparse element which sets
        // format=avc what is not compatible with further muxer. According that we need to switch
        // format of video buffers to compatible one.
        switch (this->_connector->getStream()->getStreamParameters().getCodec()) {
            case Codec::H264: parserName = "h264parse"; break;
            case Codec::H265: parserName = "h265parse"; break;
            default:
                break;
        }

        if (! parserName.empty()) {
            this->_decoder = bin->add(pipeline::Element::make(parserName));

            this->_decoder->setProperty<gint>("config-interval", -1);
        }
    }

    // Passthrough - identity decoder
    if (! this->_decoder) {
        this->_decoder = pipeline::utils::createIdentity(bin);
    }

    // muter
    if (this->isCaption()) {
        this->_muter = bin->add(pipeline::Element::make("utplcaptionmute"));

        this->_decoder->link(this->_muter);
    }

    for (auto &flowLine : this->_flowLines) {
        flowLine->prepare(this->_connector->getStreamParameters(), bin);
    }

    // Link self with flowlines
    {
        pipeline::PadPtr srcPad;

        if (this->_muter) {
            srcPad = this->_muter->getPad("src");
            
        } else if (this->_decoder) {
            srcPad = this->_decoder->getPad("src");
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
    }

    // Link with connector
    this->_connector->getPad(this->getInputParameters())->link(
        this->_decoder->getPad("sink")
    );
}

void transcoder::PassthroughProcessor::link() {
    DBG(("Linking input to processor"));

    // Link outputs
    for (auto &flowLine : this->_flowLines) {
        flowLine->link();
    }

    if (this->_connector) {
        this->_connector->link();
    }
}

const std::vector<std::shared_ptr<transcoder::Flowline>> &transcoder::PassthroughProcessor::getFlowlines() const {
    return this->_flowLines;   
}

std::shared_ptr<transcoder::Compositor> transcoder::PassthroughProcessor::getCompositor() const {
    return nullptr;
}

pipeline::ElementPtr transcoder::PassthroughProcessor::getMuter() {
    return this->_muter;
}

std::shared_ptr<transcoder::StreamConnector> transcoder::PassthroughProcessor::getConnector() {
    return this->_connector;
}

void transcoder::PassthroughProcessor::setConnector(std::shared_ptr<transcoder::StreamConnector> connector) {
    this->_connector = connector;
}

const std::string &transcoder::PassthroughProcessor::getName() {
    return this->_name;
}

void transcoder::PassthroughProcessor::blank(bool blank) {
    if (this->_muter) {
        this->_muter->setProperty("mute", blank);
    }
}

bool transcoder::PassthroughProcessor::compatible(const StreamParameters &outputParameters) { 
    if (! outputParameters.isPassthrough()) {
        return false;
    }

    if (this->isAudio() && ! outputParameters.hasAudioParameters()) {
        return false;
    }

    if (this->isVideo() && ! outputParameters.hasVideoParameters()) {
        return false;
    }

    if (this->isCaption() && ! outputParameters.hasCaptionParameters()) {
        return false;
    }

    return true;
}