#include "utpl/common/debug.h"

#include "utpl/transcoder/output/mxl.h"
#include "utpl/transcoder/stream/output/pad.h"

using namespace utpl;

transcoder::MxlOutput::MxlOutput() {
    this->_bin = pipeline::Bin::create();

    // Required to correctly propagate the EOS event upstream
    GST_OBJECT_FLAG_SET((*this->_bin)->get(), GST_ELEMENT_FLAG_SINK);
}

transcoder::MxlOutput::~MxlOutput() {
    this->cleanup();
}

void transcoder::MxlOutput::prepare(const OutputParameters &parameters) {
    DBG(("Preparing MXL output '{}', streams: {}", parameters.getName(), parameters.getStreams().size()));

    this->_parameters = parameters;

    if (! this->_streams.empty()) {
        return;
    }

    int audioIdx = 0;
    int videoIdx = 0;

    for (auto &stream : parameters.getStreams()) {
        std::string name;
        MxlTap::Kind kind;

        if (stream.hasAudioParameters()) {
            name = "audio_" + std::to_string(audioIdx++);
            kind = MxlTap::Kind::AUDIO;

        } else if (stream.hasVideoParameters()) {
            name = "video_" + std::to_string(videoIdx++);
            kind = MxlTap::Kind::VIDEO;

        } else {
            continue;
        }

        auto pad = this->_bin->addPad(name, nullptr, false);

        // Only the first stream of each kind has a flow. The tap is null when the plugin is missing or
        // the flow is already written by another stream.
        const bool first = (kind == MxlTap::Kind::AUDIO) ? (audioIdx == 1) : (videoIdx == 1);

        std::unique_ptr<MxlTap> tap;

        if (first) {
            tap = MxlTap::create(parameters.getMxl(), kind);
        }

        pipeline::ElementPtr target;

        if (tap) {
            target = this->_bin->add(tap->getBin());

            this->_taps.push_back(std::move(tap));

        } else {
            // The programme has to go somewhere, or the flowline would stop at an unlinked pad.
            WARN(("MXL output '{}': no flow for the {} stream, its media is discarded", parameters.getName(), name));

            target = this->_bin->add(pipeline::Element::make("fakesink"));

            target->setProperty<gboolean>("sync",  false);
            target->setProperty<gboolean>("async", false);
        }

        std::reinterpret_pointer_cast<pipeline::GhostPad>(pad)->setTarget(target->getPad("sink"));

        this->_streams.push_back(std::make_shared<OutputPadStream>(stream, pad));
    }

    this->_bin->ready();
}

const transcoder::OutputParameters &transcoder::MxlOutput::getOutputParameters() const {
    return this->_parameters;
}

void transcoder::MxlOutput::cleanup() {
    this->_taps.clear();
    this->_streams.clear();

    if (this->_bin) {
        this->_bin = nullptr;
    }
}

const std::vector<std::shared_ptr<transcoder::OutputStream>> &transcoder::MxlOutput::getStreams() {
    return this->_streams;
}

bool transcoder::MxlOutput::isLive() const {
    return true;
}

pipeline::ElementPtr transcoder::MxlOutput::getNode() {
    return this->_bin;
}

void transcoder::MxlOutput::putScteData(const Scte35Data &data) {
    // Only the video tap with a data flow takes cues; the others have none. Without a data flow the output carries no SCTE-35.
    for (auto &tap : this->_taps) {
        if (tap->hasCueFlow()) {
            tap->putCue(data);
        }
    }
}
