#include <filesystem>
#include <atomic>

#include <glib.h>
#include <glib-unix.h>

#include <boost/url/url.hpp>
#include <boost/url/parse.hpp>

#include "utpl.h"
#include "utpl/common/types/exception.h"
#include "utpl/common/utils.h"
#include "utpl/common/debug.h"

#include "application.h"

#include "application/transcoder.h"
#include "application/transcoder/api.h"
#include "application/transcoder/api/grpc.h"

#include "application/license/manager.h"

#include "application/config/parser/json.h"

#include "application/event/provider/dummy.h"
#include "application/event/provider/sqlite.h"


using namespace utpl;

namespace urls = boost::urls;


class Application::Pimpl {
    private:
        void _breakLoop() {
            if (g_main_loop_is_running(this->_loop)) {
                g_main_loop_quit(this->_loop);
            }
        }

        static void _onSigint(Pimpl *self) {
            DBG(("Caught SIGINT signal, exiting"));

            self->_stopRequested = true;
            self->_breakLoop();
        }

    public:
        Pimpl() {
            this->_loop = g_main_loop_new(nullptr, false);
            if (IS_NULL(this->_loop)) {
                throw utpl::NoFreeMemoryException("Unable to create main loop object");
            }

            this->_api = std::make_unique<app::TranscoderGrpcApi>();
        }

        ~Pimpl() {
            TRACE(("CALL"));
            
            this->stop();

            g_main_loop_unref(this->_loop);
        }


        void configure(const std::string &configPath, const std::string &dbPath, const std::string &licensePath, const utpl::license::License &license, const utpl::crypto::PublicKey &licensePublicKey) {
            {
                auto parser = app::JsonDebugConfigParserFactory::createParser(configPath);
                
                app::DebugConfig config = parser->parse(configPath);

                const app::DebugParameters &debugParams = config.getDebugParameters();
                // Verify debug configuration
                for (const auto &loggerParams : {debugParams.getTranscoderLogger(), debugParams.getGstreamerLogger()}) {
                    for (const auto &output : loggerParams.getOutputs()) {
                        if (output.isEnabled() &&  output.getType() == utpl::LoggerOutputType::FILE && output.getPath().empty()) {
                            throw utpl::InvalidArgumentException("Logger output file path is empty!");
                        }
                    }
                }

                utpl::Debug::configureTranscoderDebug(debugParams.getTranscoderLogger());
                utpl::Debug::configureGstreamerDebug(debugParams.getGstreamerLogger());
            }

            auto parser = app::JsonConfigParserFactory::createParser(configPath);

            this->_config = parser->parse(configPath);
            
            utpl::configureGstPlugins(
                this->_config.getApplicationParameters().getGstPluginsParameters()
            );

            {
                app::ApplicationParameters params = this->_config.getApplicationParameters();

                params.setDbPath(dbPath);

                this->_config.setApplicationParameters(params);
            }

            // Verify application configuration
            {
                const app::ApplicationParameters &appParams = this->_config.getApplicationParameters();

                if (! appParams.getDbPath().empty() && ! std::filesystem::exists(appParams.getDbPath())) {
                    throw utpl::InvalidArgumentException("DB file '" + appParams.getDbPath() + "' does not exist!");
                }
            }

            // Verify license manager configuration
            {
                const auto &appParams = this->_config.getApplicationParameters();

                if (! license.getModel().isOffline()) {
                    const auto &uri = appParams.getLicenseServerUri();

                    if (uri.empty()) {
                        throw utpl::InvalidArgumentException("The license server URL cannot be empty for online licenses.");

                    } else {
                        auto url = urls::parse_uri(appParams.getLicenseServerUri());

                        if (
                            url->scheme_id() != urls::scheme::http &&
                            url->scheme_id() != urls::scheme::https
                        ) {
                            throw utpl::InvalidArgumentException("The license server URL must explicitly specify the HTTP or HTTPS protocol.");
                        }
                    }
                }
            }

            // Verify transcoder configuration
            {
                auto &params = this->_config.getTranscoderParameters().getEngineParameters();

                bool passthroughVideo = false;
                bool passthroughAudio = false;

                for (const auto &outParams : params.getOutputParameters()) {
                    for (const auto &stream : outParams.getStreams()) {
                        if (stream.getCodec() == Codec::PASSTHROUGH && stream.hasVideoParameters()) {
                            passthroughVideo = true;
                        }

                        if (stream.getCodec() == Codec::PASSTHROUGH && stream.hasAudioParameters()) {
                            passthroughAudio = true;
                        }
                    }
                }

                {
                    auto &inParams = params.getInputParameters();

                    if (inParams.getUris().empty()) {
                        throw utpl::InvalidArgumentException("No URIs for input!");
                    }

                    if (inParams.getUris().size() > 2) {
                        throw utpl::InvalidArgumentException("Input supports at most 2 URIs (redundancy)");
                    }

                    for (const auto &uri : inParams.getUris()) {
                        if (uri.empty()) {
                            throw utpl::InvalidArgumentException("Source file/stream URI is empty!");
                        }

                        Protocol subprotocol;
                        
                        switch (utpl::parseProtocol(uri, subprotocol)) {
                            case Protocol::PLAYLIST:
                                if (passthroughVideo || passthroughAudio) {
                                    throw utpl::InvalidArgumentException("Playlist input cannot be used with passthrough codec!");
                                }

                                if (inParams.getUris().size() > 1) {
                                    throw utpl::InvalidArgumentException("Playlist input cannot be used with redundancy!");
                                }

                                break;

                            case Protocol::NONE:
                            case Protocol::FILE:
                                if (inParams.getUris().size() > 1) {
                                    throw utpl::InvalidArgumentException("Playlist input cannot be used with redundancy!");
                                }

                                break;
                        }
                    }
                }

                {
                    std::set<std::string> outputNames;

                    for (const auto &outParams : params.getOutputParameters()) {
                        const auto &outName = outParams.getName();

                        // Verify if output name is unique
                        if (! outName.empty()) {
                            if (outputNames.find(outName) != outputNames.end()) {
                                throw utpl::InvalidArgumentException("Duplicate output name '" + outName + "' found. Output names must be unique.");
                            }

                            outputNames.insert(outName);
                        }
                    }

                    // dashsink2 takes one H.264/H.265 video and AAC audio, in CMAF (fmp4) only.
                    auto validateDashStreams = [passthroughVideo](const transcoder::OutputParameters &out) {
                        const std::string where = "DASH output '" + out.getName() + "': ";

                        if (out.getContainer() != utpl::Container::FMP4 && out.getContainer() != utpl::Container::UNKNOWN) {
                            throw utpl::InvalidArgumentException(where + "container must be 'fmp4'");
                        }

                        if (passthroughVideo) {
                            throw utpl::InvalidArgumentException(where + "cannot be used with passthrough video codec");
                        }

                        int videoStreams = 0;

                        for (const auto &stream : out.getStreams()) {
                            if (stream.hasVideoParameters()) {
                                if (stream.getCodec() != Codec::H264 && stream.getCodec() != Codec::H265) {
                                    throw utpl::InvalidArgumentException(where + "video codec must be h264 or h265");
                                }

                                if (++videoStreams > 1) {
                                    throw utpl::InvalidArgumentException(where + "carries one video stream; use a GROUP output for several renditions");
                                }

                            } else if (stream.hasAudioParameters()) {
                                if (stream.getCodec() != Codec::AAC) {
                                    throw utpl::InvalidArgumentException(where + "audio codec must be aac");
                                }

                            } else if (stream.hasCaptionParameters()) {
                                throw utpl::InvalidArgumentException(where + "caption streams are not supported");
                            }
                        }
                    };

                    for (const auto &outParams : params.getOutputParameters()) {
                        for (const auto &transport : outParams.getTransports()) {
                            if (transport.getUri().empty()) {
                                throw utpl::InvalidArgumentException("Output file/stream URI is empty!");
                            }

                            {
                                utpl::Protocol subprotocol;

                                if (utpl::parseProtocol(transport.getUri(), subprotocol) == utpl::Protocol::DASH) {
                                    if (subprotocol != utpl::Protocol::FILE && subprotocol != utpl::Protocol::HTTP) {
                                        throw utpl::InvalidArgumentException("DASH output URI must be dash+file://, dash+http:// or dash+https://");
                                    }

                                    if (outParams.getTransports().size() > 1) {
                                        throw utpl::InvalidArgumentException("A DASH output must be the output's only transport");
                                    }

                                    if (outParams.getType() == transcoder::OutputType::SINGLE) {
                                        if (outParams.getContainer() != utpl::Container::FMP4) {
                                            throw utpl::InvalidArgumentException("DASH output '" + outParams.getName() + "': container must be 'fmp4'");
                                        }

                                        validateDashStreams(outParams);

                                    } else {
                                        for (const auto &stream : outParams.getStreams()) {
                                            if (! stream.hasLinkParameters()) {
                                                continue;
                                            }

                                            const auto &linkedName = stream.getLinkParameters().getOutputParameters().getOutputName();

                                            for (const auto &member : params.getOutputParameters()) {
                                                if (member.getName() == linkedName) {
                                                    validateDashStreams(member);
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            {
                                const auto *params = dynamic_cast<const transcoder::HlsOutputTransportParameters *>(
                                    transport.getTransportParameters(utpl::Protocol::HLS)
                                );

                                if (params) {                                
                                    if (params->getMaxSegmentDuration() < transcoder::HlsOutputTransportParameters::MIN_SEGMENT_DURATION) {
                                        throw utpl::InvalidArgumentException("HLS segment duration is below minimum");
                                    }

                                    if (params->getMaxSegments() < transcoder::HlsOutputTransportParameters::MIN_SEGMENTS) {
                                        throw utpl::InvalidArgumentException("HLS window size in segments is below minimum");
                                    }

                                    if (passthroughVideo) {
                                        throw utpl::InvalidArgumentException("HLS output cannot be used with passthrough video codec!");
                                    }
                                }
                            }
                        }

                        switch (outParams.getType()) {
                            case transcoder::OutputType::SINGLE:
                                {
                                    if (outParams.getTransports().empty()) {
                                        // Output is not assigned to any group
                                        if (outParams.getGroupName().empty()) {
                                            throw utpl::InvalidArgumentException("Output has no transports defined!");
                                        }
                                    }

                                    switch (outParams.getContainer()) {
                                        case utpl::Container::ES:
                                            if (outParams.getStreams().size() > 1) {
                                                throw utpl::InvalidArgumentException("WAV and ES containers allow to hold just a single stream!");
                                            }
                                            break;

                                        case utpl::Container::MPEGTS:
                                            break;

                                        case utpl::Container::UNKNOWN:
                                            // GROUP elements don't have to specify 
                                            if (outParams.getGroupName().empty()) {
                                                throw utpl::InvalidArgumentException("Unknown container!");
                                            }
                                    }

                                    if (outParams.getStreams().empty()) {
                                        throw utpl::InvalidArgumentException("No output streams defined by the configuration!");
                                    }

                                    for (const auto &stream : outParams.getStreams()) {
                                        if (stream.hasLinkParameters()) {
                                            throw utpl::InvalidArgumentException("SINGLE output type does not support linked streams");
                                        }
                                    }
                                }
                                break;

                            case transcoder::OutputType::GROUP:
                                {                              
                                    const auto &groupName = outParams.getName();

                                    if (groupName.empty()) {
                                        throw utpl::InvalidArgumentException("The GROUP output must have a name");
                                    }

                                    if (outParams.getTransports().empty()) {
                                        throw utpl::InvalidArgumentException("The GROUP Output has no transports defined!");
                                    }

                                    // Verify transport types - one HLS or DASH transport is supported
                                    if (outParams.getTransports().size() > 1) {
                                        throw utpl::InvalidArgumentException("Currently only 1 HLS or DASH transport is supported on GROUP outputs");
                                    }

                                    for (const auto &transport : outParams.getTransports()) {
                                        utpl::Protocol subprotocol;
                                        utpl::Protocol protocol = utpl::parseProtocol(transport.getUri(), subprotocol);

                                        if (
                                            (protocol != Protocol::HLS) &&
                                            (protocol != Protocol::HTTP) &&
                                            (protocol != Protocol::DASH)
                                        ) {
                                            throw utpl::InvalidArgumentException("Currently only HLS and DASH transports are supported on GROUP outputs");
                                        }
                                    }

                                    // Group output accepts link streams only.
                                    // Verify uniqueness of the name modifier.
                                    {
                                        std::set<std::string> modifiers;

                                        for (const auto &stream : outParams.getStreams()) {
                                            if (! stream.hasLinkParameters()) {
                                                throw utpl::InvalidArgumentException("The GROUP output only accepts streams of type 'link'.");
                                            }

                                            const auto &nameModifier = stream.getLinkParameters().getOutputParameters().getOutputNameModifier();
                                            if (! nameModifier.empty()) {
                                                if (modifiers.find(nameModifier) != modifiers.end()) {
                                                    throw utpl::InvalidArgumentException("GROUP outputs require unique stream name modifiers.");
                                                }

                                                modifiers.insert(nameModifier);
                                            }
                                        }
                                    }
                                }
                                break;

                            case transcoder::OutputType::MXL:
                                // The parser has checked the flows; here only what the channel relies on.
                                if (! outParams.getMxl().isEnabled() || outParams.getStreams().empty()) {
                                    throw utpl::InvalidArgumentException("The MXL output has no flow to write");
                                }
                                break;

                            default:
                                throw utpl::InvalidArgumentException("Output type is unknown");
                        }

                        for (const auto &stream : outParams.getStreams()) {
                            if (
                                stream.hasAudioParameters() ||
                                stream.hasVideoParameters() ||
                                stream.hasCaptionParameters()
                            ) {
                                if (stream.getCodec() == Codec::UNKNOWN) {
                                    throw utpl::InvalidArgumentException("Invalid stream codec: AV stream requires a valid codec definition, but 'UNKNOWN' was provided.");
                                }

                            } else if (
                                stream.hasLinkParameters()
                            ) {
                                std::string linkedName;

                                if (! stream.getLinkParameters().hasOutputParameters()) {
                                    throw utpl::InvalidArgumentException("The GROUP output stream supports only the 'OUTPUT' link type.");
                                }

                                linkedName = stream.getLinkParameters().getOutputParameters().getOutputName();

                                if (linkedName.empty()) {
                                    throw utpl::InvalidArgumentException("A linked output must have a name. It is missing for output '" + outParams.getName() + "'");
                                }

                                if (outputNames.find(linkedName) == outputNames.end()) {
                                    throw utpl::InvalidArgumentException(
                                        "The output '" + linkedName + "' linked in GROUP output '" + outParams.getName() + "' does not exist in the configuration file."
                                    );
                                }

                                // Verify that linked group is not group
                                auto linkedOutputIt = std::find_if(
                                    params.getOutputParameters().begin(),
                                    params.getOutputParameters().end(),
                                    [&linkedName](const utpl::transcoder::OutputParameters &params) {
                                        return linkedName == params.getName();
                                    }
                                );

                                if (linkedOutputIt->getType() != transcoder::OutputType::SINGLE) {
                                    throw utpl::InvalidArgumentException("The GROUP output stream can link SINGLE outputs only.");
                                }
                            }
                        }
                    }
                }

                // One writer per MXL flow: two taps or outputs on the same flow would fight over its memory (the tap
                // would refuse at run time and the channel would run without it, so refuse here, where the cause is
                // clear). A flow an MXL input of the channel reads cannot be written by the same channel either.
                {
                    std::map<std::string, std::string> writers; // "<domain>|<flow id>" -> who writes it
                    std::set<std::string>              readers;

                    auto addWriter = [&writers](const std::string &domain, const std::string &flowId, const std::string &who) {
                        if (flowId.empty()) {
                            return;
                        }

                        const auto inserted = writers.emplace(domain + "|" + flowId, who);

                        if (! inserted.second) {
                            throw utpl::InvalidArgumentException(
                                "MXL flow " + flowId + " is written by both " + inserted.first->second + " and " + who + "; a flow has one writer."
                            );
                        }
                    };

                    size_t inputIndex = 0;

                    for (const auto &inParams : params.getAllInputParameters()) {
                        const std::string who = "transcoder.inputs[" + std::to_string(inputIndex) + "].mxlTap";

                        addWriter(inParams.getMxlTap().getDomain(), inParams.getMxlTap().getVideoFlowId(), who + ".video");
                        addWriter(inParams.getMxlTap().getDomain(), inParams.getMxlTap().getAudioFlowId(), who + ".audio");

                        if (const auto *mxlInput = dynamic_cast<const transcoder::MxlInputTransportParameters *>(
                            inParams.getTransportParameters(Protocol::MXL)
                        )) {
                            if (! inParams.getUris().empty()) {
                                std::string domain = inParams.getUris()[0].substr(std::string("mxl://").size());

                                while (domain.size() > 1 && domain.back() == '/') {
                                    domain.pop_back();
                                }

                                for (const auto &flowId : { mxlInput->getVideoFlowId(), mxlInput->getAudioFlowId(), mxlInput->getDataFlowId() }) {
                                    if (! flowId.empty()) {
                                        readers.insert(domain + "|" + flowId);
                                    }
                                }
                            }
                        }

                        inputIndex++;
                    }

                    size_t outputIndex = 0;

                    for (const auto &outParams : params.getOutputParameters()) {
                        if (outParams.getType() == transcoder::OutputType::MXL) {
                            const std::string who = "transcoder.outputs[" + std::to_string(outputIndex) + "].mxl";

                            addWriter(outParams.getMxl().getDomain(), outParams.getMxl().getVideoFlowId(), who + ".video");
                            addWriter(outParams.getMxl().getDomain(), outParams.getMxl().getAudioFlowId(), who + ".audio");
                            addWriter(outParams.getMxl().getDomain(), outParams.getMxl().getDataFlowId(),  who + ".data");
                        }

                        outputIndex++;
                    }

                    for (const auto &writer : writers) {
                        if (readers.count(writer.first)) {
                            throw utpl::InvalidArgumentException(
                                "MXL flow " + writer.first.substr(writer.first.find('|') + 1) + " is written by " + writer.second + " and read by an MXL input of the same channel."
                            );
                        }
                    }
                }

                {
                    auto &poisParams = params.getScteParameters().getPoisParameters();

                    if (
                        poisParams.isEnabled() && 
                        (
                            poisParams.getEndpoint().empty() ||
                            poisParams.getAcquisitionPointId().empty()
                        )
                    ) {
                        throw utpl::InvalidArgumentException("POIS conditioning enabled but the server URI or Acquisition ID is empty!");
                    }
                }
            }

            this->_licenseManager.configure(
                license, 
                licensePath,
                this->_config.getApplicationParameters().getLicenseServerUri(),
                licensePublicKey
            );

            this->_transcoder.configure(
                this->_config.getTranscoderParameters().getEngineParameters()
            );
        }


        void start() {
            g_unix_signal_add(SIGINT, G_SOURCE_FUNC(_onSigint), this);

            LOG(("Starting transcoder thread"));

            {
                {
                    auto &appParams = this->_config.getApplicationParameters();

                    if (! appParams.getDbPath().empty()) {
                        this->_eventProvider = std::make_unique<app::SqliteEventProvider>(appParams.getDbPath());
                    }
                }
                
                if (this->_eventProvider) {
                    this->_transcoder.addEventProvider(this->_eventProvider.get());
                }
            }

            this->_licenseManager.start([this](app::LicenseManager::LicenseStatus status) {
                if (status == app::LicenseManager::LicenseStatus::VALID) {
                    this->_transcoder.start([this](utpl::Error error) {
                        this->onTranscoderStopped(error);
                    });

                } else {
                    this->_error = utpl::Error::LICENSE_ERROR;
                    this->stop();
                }
            });

            this->_api->start(&this->_transcoder);
        }

        void stop() {
            this->_breakLoop();
            
            this->_api->stop();
            this->_licenseManager.stop();
            this->_transcoder.stop();

            if (this->_eventProvider) {
                this->_transcoder.removeEventProvider(this->_eventProvider.get());
                this->_eventProvider.reset();
            }
        }

        void loop() {
            g_main_loop_run(this->_loop);
        }

        utpl::Error getError() const {
            return this->_error;
        }

    private:
        void onTranscoderStopped(utpl::Error errorCode) {
            DBG(("Transcoder worker has stopped with error code {}", utpl::utils::to_underlying(errorCode)));

            // A failure while stopping on request is not a failure of the channel, and the first
            // recorded cause wins.
            if (errorCode != utpl::Error::NO_ERROR && ! this->_stopRequested && this->_error == utpl::Error::NO_ERROR) {
                this->_error = errorCode;
            }

            this->_breakLoop();
        }

    private:
        GMainLoop *_loop;

        app::ApplicationConfig _config;
        app::Transcoder        _transcoder;
        app::LicenseManager    _licenseManager;

        std::unique_ptr<app::TranscoderApi> _api;

        std::unique_ptr<utpl::transcoder::EventProvider> _eventProvider;

        // Written from the worker and license threads, read by main() after the loop ends.
        std::atomic<utpl::Error> _error         { utpl::Error::NO_ERROR };
        std::atomic<bool>        _stopRequested { false };
};


Application::Application() {
    this->self = std::make_unique<Pimpl>();
}


Application::~Application() {
    this->stop();

    this->self.reset();
}


void Application::configure(const std::string &configPath, const std::string &dbPath, const std::string &licensePath, const utpl::license::License &license, const utpl::crypto::PublicKey &licensePublicKey) {
    self->configure(configPath, dbPath, licensePath, license, licensePublicKey);
}


void Application::start() {
    self->start();
}


void Application::stop() {
    self->stop();
}


void Application::loop() {
    self->loop();
}


utpl::Error Application::getError() const {
    return self->getError();
}