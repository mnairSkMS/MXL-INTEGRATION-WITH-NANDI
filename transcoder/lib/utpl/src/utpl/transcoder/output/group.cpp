#include "utpl/common/debug.h"

#include "utpl/transcoder/output/sink.h"

#include "utpl/transcoder/output/single.h"
#include "utpl/transcoder/output/group.h"

#include "utils.h"
#include "dash/presentation.h"

using namespace utpl;

transcoder::GroupOutput::GroupOutput(const std::vector<std::shared_ptr<SingleOutput>> &elements) {
    DBG(("Creating group output for {} children", elements.size()));

    this->_children = elements;
}

transcoder::GroupOutput::~GroupOutput() {
}

const transcoder::OutputParameters &transcoder::GroupOutput::getOutputParameters() const {
    return this->_parameters;
}

void transcoder::GroupOutput::prepare(const OutputParameters &parameters) {
    {
        Protocol subprotocol;

        if (utpl::parseProtocol(parameters.getTransports()[0].getUri(), subprotocol) == Protocol::DASH) {
            this->prepareDash(parameters);
            return;
        }
    }

    const auto &transport    = parameters.getTransports()[0];
    const auto *transportHls = reinterpret_cast<const HlsOutputTransportParameters *>(transport.getTransportParameters(Protocol::HLS));

    DBG(("Preparing Group output '{}'", parameters.getName(), parameters.getGroupName()));
 
    this->_hlsStreamer       = createHlsStreamer(transport);    
    this->_hlsMasterPlaylist = createHlsMasterPlaylist(transportHls);

    for (auto &child : this->_children) {
        const auto &childParameters = child->getOutputParameters();

        std::vector<StreamParameters>            targetStreamParameters;
        std::vector<std::unique_ptr<OutputSink>> targetSinks;

        // TODO: Merge transport parameters

        for (const auto &stream : child->getStreams()) {
            targetStreamParameters.push_back(stream->getStreamParameters());
        }

        std::string nameModifier;

        for (auto &s : parameters.getStreams()) {
            auto &outputParameters = s.getLinkParameters().getOutputParameters();

            if (outputParameters.getOutputName() == child->getOutputParameters().getName()) {
                nameModifier = outputParameters.getOutputNameModifier();
                break;
            }
        }

        targetSinks.push_back(std::move(
            OutputSink::createHls(
                transport, 
                targetStreamParameters,
                childParameters.getScteParameters(), 
                childParameters.getContainer(), 
                this->_hlsStreamer,
                
                createHlsVariant(
                    this->_hlsMasterPlaylist, 
                    transportHls, 
                    targetStreamParameters, 
                    childParameters.getContainer(),
                    nameModifier
                )
            )
        ));

        child->prepare(childParameters, std::move(targetSinks));
    }

    this->_hlsStreamer->setPlaylist(this->_hlsMasterPlaylist);
}

// Every member gets its own dashsink2 (its own encoders feed it), all sharing one
// dash::Presentation that merges their Representations into a single MPD. Members cut on the
// same running-time grid, so their segments line up for switching.
void transcoder::GroupOutput::prepareDash(const OutputParameters &parameters) {
    const auto &transport    = parameters.getTransports()[0];
    auto        presentation = OutputSink::createDashPresentation(transport);
    const auto &baseFileName = presentation->getParameters().getBaseFileName();

    DBG(("Preparing DASH group output '{}' with {} members", parameters.getName(), this->_children.size()));

    int index = 0;

    for (auto &child : this->_children) {
        const auto &childParameters = child->getOutputParameters();

        std::vector<StreamParameters>            targetStreamParameters;
        std::vector<std::unique_ptr<OutputSink>> targetSinks;

        for (const auto &stream : child->getStreams()) {
            targetStreamParameters.push_back(stream->getStreamParameters());
        }

        std::string nameModifier;

        for (auto &s : parameters.getStreams()) {
            auto &outputParameters = s.getLinkParameters().getOutputParameters();

            if (outputParameters.getOutputName() == childParameters.getName()) {
                nameModifier = outputParameters.getOutputNameModifier();
                break;
            }
        }

        // Same naming as the HLS variants: "variant" + modifier, or "variant_<n>".
        std::string prefix = "variant" + (nameModifier.empty() ? "_" + std::to_string(index) : nameModifier);

        if (! baseFileName.empty()) {
            prefix = baseFileName + "_" + prefix;
        }

        targetSinks.push_back(
            OutputSink::createDash(transport, targetStreamParameters, childParameters.getScteParameters(), presentation, prefix)
        );

        child->prepare(childParameters, std::move(targetSinks));

        index++;
    }
}

void transcoder::GroupOutput::cleanup() {
    for (auto &child : this->_children) {
        child->cleanup();
    }
}

const std::vector<std::shared_ptr<transcoder::OutputStream>> &transcoder::GroupOutput::getStreams() {
    static std::vector<std::shared_ptr<transcoder::OutputStream>> ret;

    return ret;
}

bool transcoder::GroupOutput::isLive() const {
    bool ret = false;

    for (auto &child : this->_children) {
        if (child->isLive()) {
            ret = true;
            break;
        }
    }

    return ret;
}

pipeline::ElementPtr transcoder::GroupOutput::getNode() {
    return nullptr;
}

void transcoder::GroupOutput::putScteData(const Scte35Data &data) {
    for (auto &child : this->_children) {
        child->putScteData(data);
    }
}
