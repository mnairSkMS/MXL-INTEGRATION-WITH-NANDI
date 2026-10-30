#include "utpl/common/debug.h"

#include "utpl/pipeline/utils.h"

#include "audio.h"
#include "flowline.h"

using namespace utpl;


transcoder::AudioTranscodingProcessor::AudioTranscodingProcessor(const std::string &processorName, const transcoder::StreamParameters &outParams) 
    : _name(processorName), _inputParameters(outParams)
{
    this->_inputParameters = outParams;
    this->_prepared        = false;
    this->_linked          = false;

    this->_inputParameters.setCodec(Codec::ES);
}

std::shared_ptr<transcoder::Flowline> transcoder::AudioTranscodingProcessor::createFlowLine(Container container, const transcoder::StreamParameters &outParams) {
    this->_flowLines.push_back(
        std::make_shared<TranscodingFlowline>(this->_name + "-flowline-" + std::to_string(this->_flowLines.size()), container, outParams)
    );

    return *this->_flowLines.rbegin();
}

bool transcoder::AudioTranscodingProcessor::isAudio() const {
    return this->_connector->getStreamParameters().hasAudioParameters();
}

bool transcoder::AudioTranscodingProcessor::isVideo() const {
    return this->_connector->getStreamParameters().hasVideoParameters();
}

bool transcoder::AudioTranscodingProcessor::isCaption() const {
    return this->_connector->getStreamParameters().hasCaptionParameters();
}

const transcoder::StreamParameters &transcoder::AudioTranscodingProcessor::getInputParameters() {
    return this->_inputParameters;
}

void transcoder::AudioTranscodingProcessor::prepare(pipeline::BinPtr &bin) {
    // When input samples are coded as floats, then conversion to integer is required
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

    this->_bin      = bin;
    this->_prepared = true;
}

void transcoder::AudioTranscodingProcessor::link() {
    DBG(("Linking input to processor"));

    // Link outputs
    for (auto &flowLine : this->_flowLines) {
        flowLine->link();
    }

    if (this->_connector) {
        this->_connector->link();
    }

    this->_linked = true;
}

const std::vector<std::shared_ptr<transcoder::Flowline>> &transcoder::AudioTranscodingProcessor::getFlowlines() const {
    return this->_flowLines;   
}

std::shared_ptr<transcoder::Compositor> transcoder::AudioTranscodingProcessor::getCompositor() const {
    return nullptr;
}

pipeline::ElementPtr transcoder::AudioTranscodingProcessor::getMuter() {
    return this->_muter;
}

std::shared_ptr<transcoder::StreamConnector> transcoder::AudioTranscodingProcessor::getConnector() {
    return this->_connector;
}

void transcoder::AudioTranscodingProcessor::setConnector(std::shared_ptr<transcoder::StreamConnector> connector) {
    if (this->_connector && this->_connector != connector) {
        bool linked = this->_decoderPad && this->_decoderPad->getPeer();

        if (linked) {
            this->_decoderPad->unlink();
        }

        this->_connector->cleanup();
    }

    if (connector) {
        if (this->_prepared) {
            connector->prepare(this->_bin);
        
            // Link with connector
            connector->getPad(this->_inputParameters)->link(
                this->_decoderPad
            );
        }

        if (this->_linked) {
            connector->link();
        }

        if (this->_prepared && this->_linked) {
            connector->getNode()->syncWithParent();
        }
    }

    this->_connector = connector;
}

const std::string &transcoder::AudioTranscodingProcessor::getName() {
    return this->_name;
}

void transcoder::AudioTranscodingProcessor::blank(bool blank) {
    if (this->_muter) {
        this->_muter->setProperty("mute", blank);
    }
}

bool transcoder::AudioTranscodingProcessor::compatible(const StreamParameters &outputParameters) { 
    if (outputParameters.isPassthrough()) {
        return false;
    }

    if (! outputParameters.hasAudioParameters()) {
        return false;
    }

    {
        const auto &aThis  = this->_inputParameters.getAudioParameters();
        const auto &aOther = outputParameters.getAudioParameters();

        if (
            (aThis.getChannels() != aOther.getChannels()) || 
            (aThis.getRate()     != aOther.getRate())
        ) {
            return false;
        }
    }
    
    return true;
}
