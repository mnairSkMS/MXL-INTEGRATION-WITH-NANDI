#include "utpl/common/types/exception.h"
#include "utpl/common/utils/gstreamer.h"
#include "utpl/common/debug.h"

#include "utpl/pipeline/utils.h"

#include "utpl/transcoder/output/single.h"
#include "utpl/transcoder/output/muxer.h"
#include "utpl/transcoder/stream/output/pad.h"

#include "muxer/mpegts.h"

using namespace utpl;


#define ELEMENT_NAME_MUXER "muxer"

transcoder::SingleOutput::SingleOutput() {
    this->_bin = pipeline::Bin::create();

    // Required to correctly propagate the EOS event upstream
    GST_OBJECT_FLAG_SET((*this->_bin)->get(), GST_ELEMENT_FLAG_SINK);
}

transcoder::SingleOutput::~SingleOutput() {
    this->cleanup();
}

void transcoder::SingleOutput::prepare(const OutputParameters &parameters, std::vector<std::unique_ptr<OutputSink>> &&sinks) {
    DBG(("Preparing Single output '{}', output group: '{}', sinks: {}, local streams: {}, parameters streams: {}", 
        parameters.getName(), parameters.getGroupName(), sinks.size(), this->_streams.size(), parameters.getStreams().size()
    ));

    this->_parameters = parameters;
    this->_sinks      = std::move(sinks);

    if (this->_sinks.empty()) {
        for (const auto &transport : parameters.getTransports()) {
            this->_sinks.push_back(
                std::move(OutputSink::create(transport, parameters.getStreams(), parameters.getScteParameters(), parameters.getContainer()))
            );
        }
    }

    if (this->_streams.empty()) {
        int audioIdx   = 0;
        int videoIdx   = 0;
        int captionIdx = 0;

        for (auto &stream : parameters.getStreams()) {
            std::string name;

            if (stream.hasAudioParameters()) {
                name = "audio_" + std::to_string(audioIdx++);

            } else if (stream.hasVideoParameters()) {
                name = "video_" + std::to_string(videoIdx++);

            } else if (stream.hasCaptionParameters()) {
                name = "caption_" +  std::to_string(captionIdx++);
            }

            if (! name.empty()) {
                this->_streams.push_back(
                    std::make_shared<OutputPadStream>(stream, this->_bin->addPad(name, nullptr, false))
                );
            }
        }
    }

    // This is intentionally postponed. If an output has no sinks, it most likely means
    // it is linked to a GROUP output, which will handle the output preparation itself.
    if (! this->_sinks.empty()) {
        bool needsClockSync = false;

        if (! parameters.hasLiveSource()) {
            for (auto &sink : this->_sinks) {
                if (sink->isLive() && sink->needExternalSync()) {
                    needsClockSync = true;

                    break;
                }
            }
        }
        
        // Each stream's tee, with its media type and configured PID
        std::map<pipeline::ElementPtr, std::pair<MediaType, Pid>> splitters;
        {
            for (auto &stream : this->_streams) {
                const auto &params = stream->getStreamParameters();

                MediaType mediaType;

                if (params.hasAudioParameters()) {
                    mediaType = MediaType::AUDIO;

                } else if (params.hasVideoParameters()) {
                    mediaType = MediaType::VIDEO;

                } else if (params.hasCaptionParameters()) {
                    mediaType = MediaType::CAPTION;

                } else {
                    mediaType = MediaType::UNKNOWN;
                }

                {
                    pipeline::ElementPtr ghostPadTarget;
                
                    auto name = stream->getPad()->getName();

                    auto tee = this->_bin->add(pipeline::Element::make(name + "-tee", "tee"));
                    ghostPadTarget = tee;
                    
                    splitters.insert({ tee, { mediaType, params.getPid() } });
                    
                    if (needsClockSync) {
                        ghostPadTarget = this->_bin->add(pipeline::Element::make(name + "-sync", "clocksync"));
                        ghostPadTarget->link(tee);
                    }

                    std::reinterpret_pointer_cast<pipeline::GhostPad>(stream->getPad())->setTarget(
                        ghostPadTarget->getPad("sink")
                    );
                }
            }
        }

        for (auto &sink : this->_sinks) {
            auto types = sink->getSupportedMediaTypes();

            this->_bin->add(sink->getNode());

            if (types.find(MediaType::DATA) != types.end()) {
                auto mux = getMuxer(parameters);

                if (mux) {
                    mux->getSrcPad()->link(sink->getPad(MediaType::DATA));
                
                } else {
                    for (auto &splitter : splitters) {
                        linkSplitterToPad(splitter.first, sink->getPad(splitter.second.first, splitter.second.second));
                    }                
                }

            } else {
                for (auto &splitter : splitters) {
                    linkSplitterToPad(splitter.first, sink->getPad(splitter.second.first, splitter.second.second));
                }
            }

            sink->link();
        }
        
        if (this->_muxer) {
            for (auto &splitter : splitters) {
                linkSplitterToPad(splitter.first, this->_muxer->getSinkPad(splitter.second.first, splitter.second.second));
            }

            this->_muxer->link();
        }
    }

    this->_bin->ready();
}

void transcoder::SingleOutput::prepare(const OutputParameters &parameters) {
    if (! this->_parameters.getGroupName().empty()) {
        DBG(("Single output '{}' is owned by group output '{}' - skipping preparation.", this->_parameters.getName(), this->_parameters.getGroupName()));

        return;
    }

    this->prepare(parameters, {});
}

const transcoder::OutputParameters &transcoder::SingleOutput::getOutputParameters() const {
    return this->_parameters;
}

void transcoder::SingleOutput::cleanup() {
    if (this->_bin) {
        this->_bin = nullptr;
    }
}

const std::vector<std::shared_ptr<transcoder::OutputStream>> &transcoder::SingleOutput::getStreams() {
    return this->_streams;
}

bool transcoder::SingleOutput::isLive() const {
    bool ret = false;

    for (auto &sink : this->_sinks) {
        if (sink->isLive()) {
            ret = true;
        }
    }

    return ret;
}

pipeline::ElementPtr transcoder::SingleOutput::getNode() {
    return this->_bin;
}

void transcoder::SingleOutput::putScteData(const Scte35Data &data) {
    DBG(("Emitting SCTE-35 section on output '{}': {}", this->_parameters.getName(), toBase64(data)));

    if (this->_muxer) {
        this->_muxer->putScteData(data);
    }

    for (auto &sink : this->_sinks) {
        sink->putScteData(data);
    }
}

std::shared_ptr<transcoder::output::Muxer> transcoder::SingleOutput::getMuxer(const OutputParameters &parameters) {
    switch (parameters.getContainer()) {
        case Container::MPEGTS:
            if (! this->_muxer) {
                this->_muxer = std::make_shared<transcoder::output::MpegTsMuxer>(ELEMENT_NAME_MUXER, parameters);

                this->_bin->add(this->_muxer->getNode());
            }
            break;
    
        case Container::ES:
            if (parameters.getTransports().size() > 1) {
                throw NotSupportedException("Multiple transports are not supported for elementary streams");
            }
            break;
            
        default:
            throw NotSupportedException("Requested container " + utpl::toString(parameters.getContainer()) + " is not yet supported");
    }

    return this->_muxer;
}

void utpl::transcoder::SingleOutput::linkSplitterToPad(pipeline::ElementPtr splitter, pipeline::PadPtr pad) {
    auto queue = pipeline::utils::createQueue(this->_bin, std::string(), 16, 0, 0);
    
    splitter->link(queue);
    
    queue->getPad("src")->link(pad);
}
