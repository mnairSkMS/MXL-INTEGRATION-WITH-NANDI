#include "utpl/common/debug.h"

#include "utpl/pipeline/utils.h"

#include "video.h"
#include "flowline.h"

using namespace utpl;


transcoder::VideoTranscodingProcessor::VideoTranscodingProcessor(const std::string &processorName, const transcoder::StreamParameters &outParams) 
    : _name(processorName), _outputParameters(outParams)
{
    this->_inputParameters = outParams;
    this->_prepared        = false;
    this->_linked          = false;

    {
        auto &videoParams = this->_inputParameters.getVideoParameters();

        this->_inputParameters.setCodec(Codec::ES);

        videoParams.setInterlace(Interlace::PROGRESSIVE);
        videoParams.setPixelFormat(PixelFormat::BGRA);
    }
}

std::shared_ptr<transcoder::Flowline> transcoder::VideoTranscodingProcessor::createFlowLine(Container container, const transcoder::StreamParameters &outParams) {
    // An interlaced output weaves two progressive frames into one, so the shared canvas must run
    // at its field rate for each field to carry its own moment of motion. Outputs join a
    // processor in config order, so this is raised per flowline rather than set once.
    {
        const auto &outVideo = outParams.getVideoParameters();

        if (outVideo.getInterlace() == Interlace::INTERLEAVED) {
            const auto &rate = outVideo.getFramerate();

            Fraction fieldRate(rate.getNumerator() * 2, rate.getDenominator());

            auto &canvas = this->_inputParameters.getVideoParameters();

            if (fieldRate > canvas.getFramerate()) {
                canvas.setFramerate(fieldRate);
            }
        }
    }

    this->_flowLines.push_back(
        std::make_shared<TranscodingFlowline>(this->_name + "-flowline-" + std::to_string(this->_flowLines.size()), container, outParams)
    );

    return *this->_flowLines.rbegin();
}

bool transcoder::VideoTranscodingProcessor::isAudio() const {
    return this->_connector->getStreamParameters().hasAudioParameters();
}

bool transcoder::VideoTranscodingProcessor::isVideo() const {
    return this->_connector->getStreamParameters().hasVideoParameters();
}

bool transcoder::VideoTranscodingProcessor::isCaption() const {
    return this->_connector->getStreamParameters().hasCaptionParameters();
}

const transcoder::StreamParameters &transcoder::VideoTranscodingProcessor::getInputParameters() {
    return this->_inputParameters;
}

void transcoder::VideoTranscodingProcessor::prepare(pipeline::BinPtr &bin) {
    std::vector<pipeline::ElementPtr> filters;

    // Extra filters
    {
        filters.push_back(pipeline::utils::createIdentity(bin));
        if (! filters.empty()) {
            pipeline::ElementPtr lastFilter;

            for (auto &filter : filters) {
                if (lastFilter) {
                    lastFilter->link(filter);
                }

                lastFilter = filter;
            }

            if (this->_compositor) {
                this->_compositor->getSrcPad()->link(
                    (*filters.begin())->getPads(false)[0]
                );

            } else {
                this->_decoderPad = (*filters.begin())->getPads(false)[0];
            }
        }
    }

    for (auto &flowLine : this->_flowLines) {
        flowLine->prepare(this->_connector->getStreamParameters(), bin);
    }

    // Link self with flowlines
    {
        pipeline::PadPtr srcPad;

        if (! filters.empty()) {
            srcPad = (*filters.rbegin())->getPad("src");

        } else if (this->_compositor) {
            srcPad = this->_compositor->getSrcPad();
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
        // Register the processor's requested raw-video contract before preparing the
        // connector. The complex connector uses it as its fixed compositor canvas, so
        // lower-resolution inputs are scaled before graphics are applied.
        auto connectorPad = this->_connector->getPad(this->getInputParameters());

        this->_connector->prepare(bin);

        // Link with connector
        connectorPad->link(this->_decoderPad);
    }

    this->_bin      = bin;
    this->_prepared = true;
}

void transcoder::VideoTranscodingProcessor::link() {
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

const std::vector<std::shared_ptr<transcoder::Flowline>> &transcoder::VideoTranscodingProcessor::getFlowlines() const {
    return this->_flowLines;   
}

std::shared_ptr<transcoder::Compositor> transcoder::VideoTranscodingProcessor::getCompositor() const {
    return this->_compositor;
}

pipeline::ElementPtr transcoder::VideoTranscodingProcessor::getMuter() {
    return nullptr;
}

std::shared_ptr<transcoder::StreamConnector> transcoder::VideoTranscodingProcessor::getConnector() {
    return this->_connector;
}

void transcoder::VideoTranscodingProcessor::setConnector(std::shared_ptr<transcoder::StreamConnector> connector) {
    if (this->_connector && this->_connector != connector) {
        this->_connector->cleanup();
    }

    if (connector) {
        if (this->_prepared) {
            auto connectorPad = connector->getPad(this->getInputParameters());

            connector->prepare(this->_bin);
        
            // Link with connector
            connectorPad->link(this->_decoderPad);
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

const std::string &transcoder::VideoTranscodingProcessor::getName() {
    return this->_name;
}

void transcoder::VideoTranscodingProcessor::blank(bool blank) {
    if (this->_compositor) {
        this->_compositor->blank(blank);
    }
}

bool transcoder::VideoTranscodingProcessor::compatible(const StreamParameters &outputParameters) { 
    if (! outputParameters.hasVideoParameters()) {
        return false;
    }

    {
        const auto &vThis  = this->_outputParameters.getVideoParameters();
        const auto &vOther = outputParameters.getVideoParameters();

        if (
            (vThis.getWidth()     != vOther.getWidth())  ||
            (vThis.getHeight()    != vOther.getHeight()) ||
            (vThis.getFramerate() != vOther.getFramerate())
        ) {
            return false;
        }
    }

    return true;
}
