#include <set>

#include "utpl/common/debug.h"

#include "utpl/playlist/parser.h"

#include "parser.h"


#define VERSION_MIN "1.0.0"
#define VERSION_MAX "1.5.0"

// 0x0000-0x000F are reserved by MPEG-TS and 0x1FFF is the null packet PID.
static utpl::Pid _parseStreamPid(const nlohmann::json &json) {
    if (! json.is_number_integer()) {
        throw utpl::InvalidArgumentException("Stream 'pid' must be an integer.");
    }

    auto pid = json.get<int>();

    if (pid < 0x10 || pid > 0x1FFE) {
        throw utpl::InvalidArgumentException("Stream 'pid' " + std::to_string(pid) + " is out of range; use 16-8190 (0x10-0x1FFE).");
    }

    return static_cast<utpl::Pid>(pid);
}

// Two streams, or a stream and the SCTE-35 track, on one PID would break the multiplex.
// The domain, flow ids and audio channels of an MXL block: the input's "mxlTap" and the output's "mxl"
// have the same keys. `where` names the block in the messages ("transcoder.inputs[0].mxlTap").
static utpl::transcoder::MxlTapParameters _parseMxlFlows(const nlohmann::json &json, const std::string &where) {
    utpl::transcoder::MxlTapParameters tap;

    if (! json.is_object()) {
        throw utpl::InvalidArgumentException(where + " must be an object");
    }

    if (! json.contains("domain") || ! json["domain"].is_string() || json["domain"].get<std::string>().empty()) {
        throw utpl::InvalidArgumentException(where + ".domain is required (the MXL domain directory)");
    }

    tap.setDomain(json["domain"].get<std::string>());

    auto parseFlowId = [&](const char *key) -> std::string {
        if (! json.contains(key) || json[key].is_null()) {
            return std::string();
        }

        const auto &keyJson = json[key];

        if (! keyJson.is_object() || ! keyJson.contains("flowId") || ! keyJson["flowId"].is_string()) {
            throw utpl::InvalidArgumentException(where + "." + key + ".flowId is required");
        }

        const auto flowId = keyJson["flowId"].get<std::string>();

        if (! utpl::transcoder::MxlTapParameters::isValidFlowId(flowId)) {
            throw utpl::InvalidArgumentException(where + "." + key + ".flowId '" + flowId + "' is not a UUID");
        }

        return flowId;
    };

    tap.setVideoFlowId(parseFlowId("video"));
    tap.setAudioFlowId(parseFlowId("audio"));

    if (tap.getVideoFlowId().empty() && tap.getAudioFlowId().empty()) {
        throw utpl::InvalidArgumentException(where + " needs a video or an audio flow");
    }

    if (! tap.getVideoFlowId().empty() && tap.getVideoFlowId() == tap.getAudioFlowId()) {
        throw utpl::InvalidArgumentException(where + " video and audio need different flow ids");
    }

    if (json.contains("audio") && json["audio"].is_object() && json["audio"].contains("channels")) {
        const int channels = json["audio"]["channels"].get<int>();

        if (channels < 1 || channels > utpl::transcoder::MxlTapParameters::MAX_AUDIO_CHANNELS) {
            throw utpl::InvalidArgumentException(where + ".audio.channels must be 1 to 8");
        }

        tap.setAudioChannels(channels);
    }

    return tap;
}

// The picture of an MXL output: "width", "height" and "rate" (a whole number, or a fraction such as
// "30000/1001"). What is left out follows the input.
static utpl::transcoder::VideoParameters _parseMxlVideo(const nlohmann::json &video, const std::string &where) {
    utpl::transcoder::VideoParameters params;

    auto positive = [&](const char *key) -> int {
        const int value = video[key].get<int>();

        if (value <= 0) {
            throw utpl::InvalidArgumentException(where + "." + key + " must be positive");
        }

        return value;
    };

    if (video.contains("width")) {
        params.setWidth(positive("width"));
    }

    if (video.contains("height")) {
        params.setHeight(positive("height"));
    }

    if (video.contains("rate")) {
        const auto &rate = video["rate"];
        int num = 0, den = 1;

        if (rate.is_number_integer()) {
            num = rate.get<int>();

        } else if (rate.is_string()) {
            const auto text = rate.get<std::string>();
            const auto slash = text.find('/');

            try {
                num = std::stoi(text.substr(0, slash));
                den = (slash == std::string::npos) ? 1 : std::stoi(text.substr(slash + 1));

            } catch (const std::exception &) {
                num = 0;
            }
        }

        if (num <= 0 || den <= 0) {
            throw utpl::InvalidArgumentException(where + ".rate must be a whole number or a fraction such as \"30000/1001\"");
        }

        params.setFramerate(utpl::Fraction(num, den));
    }

    return params;
}

// An MXL input: the uri names the domain ("mxl:///run/mxl", no authority: a domain on another host needs MXL
// Fabrics) and the "mxl" block names the flows. `where` names the input in the messages.
static std::unique_ptr<utpl::transcoder::MxlInputTransportParameters> _parseMxlInput(
    const nlohmann::json &json, const std::vector<std::string> &uris, const std::string &where
) {
    if (uris.size() != 1) {
        throw utpl::InvalidArgumentException(where + ".uri must be one \"mxl:///<domain directory>\" (a redundant MXL input is not supported)");
    }

    {
        const std::string &uri = uris[0];
        const std::string  scheme = "mxl://";

        if (utpl::utils::toLower(uri.substr(0, scheme.size())) != scheme) {
            throw utpl::InvalidArgumentException(where + ".uri '" + uri + "' must start with mxl:// when the transport has an \"mxl\" block");
        }

        const std::string rest = uri.substr(scheme.size());

        if (rest.empty() || rest[0] != '/') {
            throw utpl::InvalidArgumentException(where + ".uri '" + uri + "' names a host: a domain on another host needs MXL Fabrics, which is not supported; use mxl:///<domain directory>");
        }

        if (rest.find_first_not_of('/') == std::string::npos) {
            throw utpl::InvalidArgumentException(where + ".uri '" + uri + "' needs the domain directory after mxl://, for example mxl:///run/mxl");
        }
    }

    if (! json.is_object()) {
        throw utpl::InvalidArgumentException(where + ".mxl must be an object");
    }

    auto ret = std::make_unique<utpl::transcoder::MxlInputTransportParameters>();

    auto parseFlowId = [&](const char *key) -> std::string {
        if (! json.contains(key) || json[key].is_null()) {
            return std::string();
        }

        const auto &keyJson = json[key];

        if (! keyJson.is_object() || ! keyJson.contains("flowId") || ! keyJson["flowId"].is_string()) {
            throw utpl::InvalidArgumentException(where + ".mxl." + key + ".flowId is required");
        }

        const auto flowId = keyJson["flowId"].get<std::string>();

        if (! utpl::transcoder::MxlTapParameters::isValidFlowId(flowId)) {
            throw utpl::InvalidArgumentException(where + ".mxl." + key + ".flowId '" + flowId + "' is not a UUID");
        }

        return flowId;
    };

    ret->setVideoFlowId(parseFlowId("video"));
    ret->setAudioFlowId(parseFlowId("audio"));

    if (ret->getVideoFlowId().empty() && ret->getAudioFlowId().empty()) {
        throw utpl::InvalidArgumentException(where + ".mxl needs a video or an audio flow");
    }

    if (ret->getVideoFlowId() == ret->getAudioFlowId()) {
        throw utpl::InvalidArgumentException(where + ".mxl video and audio need different flow ids");
    }

    if (json.contains("delayMs") && ! json["delayMs"].is_null()) {
        const auto &delay = json["delayMs"];
        const auto  max   = utpl::transcoder::MxlInputTransportParameters::MAX_DELAY_MS;

        if (! delay.is_number_integer() || delay.get<int64_t>() < 0 || delay.get<int64_t>() > static_cast<int64_t>(max)) {
            throw utpl::InvalidArgumentException(where + ".mxl.delayMs must be a whole number of milliseconds from 0 to " + std::to_string(max));
        }

        ret->setDelayMs(delay.get<uint32_t>());
    }

    return ret;
}

// The path of an "mxl://" uri: the domain directory.
static std::string _mxlDomainOfUri(const std::string &uri) {
    std::string path = uri.substr(std::string("mxl://").size());

    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }

    return path;
}

static void _checkOutputPids(const utpl::transcoder::OutputParameters &outputParams, const nlohmann::json &output) {
    std::set<utpl::Pid> used;
    bool                hasPid = false;

    for (const auto &stream : outputParams.getStreams()) {
        if (stream.getPid() == utpl::PID_NULL) {
            continue;
        }

        hasPid = true;

        if (! used.insert(stream.getPid()).second) {
            throw utpl::InvalidArgumentException(
                "Output '" + outputParams.getName() + "' uses PID " + std::to_string(stream.getPid()) + " for more than one stream."
            );
        }
    }

    auto sctePid = outputParams.getScteParameters().getPid();

    if (sctePid != utpl::PID_NULL && used.count(sctePid)) {
        throw utpl::InvalidArgumentException(
            "Output '" + outputParams.getName() + "' uses PID " + std::to_string(sctePid) + " for both a stream and SCTE-35."
        );
    }

    if (hasPid && output.contains("container") && outputParams.getContainer() != utpl::Container::MPEGTS) {
        WARN(("Output '{}': stream 'pid' applies only to the mpegts container and is ignored", outputParams.getName()));
    }
}

static void _parseNormRect(nlohmann::json &json, utpl::NormRect &rect) {
    if (! json.is_null()) {
        if (json.contains("x")) {
            auto &x = json["x"];
            if (! x.is_null()) {
                rect.x(x.get<double>());
            }
        }

        if (json.contains("y")) {
            auto &y = json["y"];
            if (! y.is_null()) {
                rect.y(y.get<double>());
            }
        }

        if (json.contains("width")) {
            auto &w = json["width"];
            if (! w.is_null()) {
                rect.width(w.get<double>());
            }
        }

        if (json.contains("height")) {
            auto &h = json["height"];
            if (! h.is_null()) {
                rect.height(h.get<double>());
            }
        }
    }
}

static void _parseSqueeze(nlohmann::json &json, utpl::transcoder::SqueezeParameters &params) {
    if (json.contains("windows")) {
        auto &windows = json["windows"];
        if (! windows.is_null()) {
            if (windows.contains("input")) {
                utpl::NormRect r = params.getSrcWindow();

                _parseNormRect(windows["input"], r);

                params.setSrcWindow(r);
            }

            if (windows.contains("output")) {
                utpl::NormRect r = params.getDstWindow();

                _parseNormRect(windows["output"], r);

                params.setDstWindow(r);
            }
        }
    }
}

static void _parseCondition(nlohmann::json &json, utpl::transcoder::BlackoutCondition &condition) {
    if (json.contains("blankWhenNoSegmentationDescriptors")) {
        condition.setBlankWhenNoDescriptors(json["blankWhenNoSegmentationDescriptors"].get<bool>());
    }

    // Same rule, named for rule sets that do something other than blanking - ad insertion reuses
    // this condition shape to decide whether a bare splice_insert counts as a trigger.
    if (json.contains("matchWhenNoSegmentationDescriptors")) {
        condition.setBlankWhenNoDescriptors(json["matchWhenNoSegmentationDescriptors"].get<bool>());
    }

    if (json.contains("segments")) {
        for (auto &segmentation : json["segments"]) {
            utpl::transcoder::BlackoutSegment s;

            if (segmentation.contains("name")) {
                s.setName(segmentation["name"].get<std::string>());
            }

            if (segmentation.contains("start")) {
                s.setTypeOfBegin(
                    std::stol(segmentation["start"].get<std::string>(), nullptr, 0)
                );
            }

            if (segmentation.contains("stop")) {
                s.setTypeOfEnd(
                    std::stol(segmentation["stop"].get<std::string>(), nullptr, 0)
                );
            }

            if (segmentation.contains("priority")) {
                s.setPriority(segmentation["priority"].get<uint8_t>());
            }

            condition.addSegment(s);
        }
    }
}

static void _parseBlackout(nlohmann::json &json, utpl::transcoder::BlackoutParameters &params) {
    if (json.contains("enable")) {
        params.setEnabled(json["enable"].get<bool>());
    }

    if (json.contains("restriction")) {
        auto restriction = json["restriction"];

        if (restriction.contains("ignore")) {
            auto ignoreStr = restriction["ignore"].get<std::string>();

            if (ignoreStr == "NONE") {
                params.setRestrictionIgnore(utpl::transcoder::BlackoutRestrictionIgnoreMode::NONE);

            } else if (ignoreStr == "NO_REGIONAL_BLACKOUT") {
                params.setRestrictionIgnore(utpl::transcoder::BlackoutRestrictionIgnoreMode::NO_REGIONAL_BLACKOUT);

            } else if (ignoreStr == "WEB_DELIVERY_ALLOWED") {
                params.setRestrictionIgnore(utpl::transcoder::BlackoutRestrictionIgnoreMode::WEB_DELIVERY_ALLOWED);

            } else {
                ERR(("Not supported restriction ignore mode '{}'", ignoreStr));
            }
        }
    }

    if (json.contains("conditions")) {
        auto condition = json["conditions"];
        
        {
            utpl::transcoder::BlackoutConditions conditions;

            {
                utpl::transcoder::BlackoutCondition c;

                _parseCondition(condition["timeSignal"], c);

                conditions.setTimeSignalCondition(c);
            }

            {
                utpl::transcoder::BlackoutCondition c;

                _parseCondition(condition["spliceInsert"], c);

                conditions.setSpliceInsertCondition(c);
            }

            params.setConditions(conditions);
        }
    }
}

static void _parseLogger(nlohmann::json &logger, utpl::LoggerParameters &loggerParams) {
    auto targetLevel = utpl::LogLevel::NONE;

    if (logger.contains("level")) {
        auto levelStr = logger["level"].get<std::string>();

        if (levelStr == "trace") {
            targetLevel = utpl::LogLevel::TRACE;

        } else if (levelStr == "debug") {
            targetLevel = utpl::LogLevel::DEBUG;

        } else if (levelStr == "log") {
            targetLevel = utpl::LogLevel::LOG;

        } else if (levelStr == "warning") {
            targetLevel = utpl::LogLevel::WARNING;

        } else if (levelStr == "error") {
            targetLevel = utpl::LogLevel::ERROR;

        } else if (levelStr == "none") {
            targetLevel = utpl::LogLevel::NONE;

        } else {
            ERR(("Not supported log level value '{}'", levelStr));
        }
    }

    loggerParams.setLevel(targetLevel);

    if (logger.contains("console")) {
        utpl::LoggerOutput output;

        auto &console = logger["console"];
        {
            if (console.contains("enable")) {
                output.setEnabled(console["enable"].get<bool>());
            }

            if (console.contains("colors")) {
                output.setColor(console["colors"].get<bool>());
            }

            output.setType(utpl::LoggerOutputType::STDOUT);

            if (console.contains("stderr")) {
                if (console["stderr"].get<bool>()) {
                    output.setType(utpl::LoggerOutputType::STDERR);
                }
            }
        }

        loggerParams.addOutput(output);
    }

    if (logger.contains("file")) {
        utpl::LoggerOutput output;

        output.setType(utpl::LoggerOutputType::FILE);

        auto &file = logger["file"];
        {
            if (file.contains("enable")) {
                output.setEnabled(file["enable"].get<bool>());
            }

            if (file.contains("colors")) {
                output.setColor(file["colors"].get<bool>());
            }

            if (file.contains("path")) {
                output.setPath(file["path"].get<std::string>());
            }
        }

        loggerParams.addOutput(output);
    }
}

static void _parseCredentials(nlohmann::json &credJson, utpl::Credentials &credentials) {
    if (credJson.contains("username")) {
        credentials.setUsername(credJson["username"].get<std::string>());
    }

    if (credJson.contains("password")) {
        credentials.setPassword(credJson["password"].get<std::string>());
    }
}

nlohmann::json app::BaseMultiversionJsonParser::begin(const std::string &path) {
    nlohmann::json ret;

    try {
        std::ifstream stream(path);

        ret = nlohmann::json::parse(stream);

        {
            this->_version.from_string(ret["version"].get<std::string>());

            if (! semver::range::satisfies(this->_version, ">= " VERSION_MIN " <= " VERSION_MAX)) {
                throw utpl::InvalidArgumentException("License in version " + this->_version.to_string() + " is not supported by this transcoder version");
            }
        }

    } catch (const utpl::Exception &ex) {
        throw;

    } catch (const std::exception &ex) {
        throw utpl::InvalidArgumentException("Unable to parse config file: '" + std::string(ex.what()) + "'");
    }
    
    return ret;
}

app::ApplicationConfig app::MultiVersionConfigJsonParser::parse(const std::string &path) {
    app::ApplicationConfig ret;

    try {
        auto json = this->begin(path);

        // Parse transcoder section
        {
            TranscoderParameters transcoderParams;
            
            {
                utpl::transcoder::EngineParameters engineParams;

                auto &transcoder = json["transcoder"];
                {
                    auto &inputs = transcoder["inputs"];

                    if (! inputs.is_array() || inputs.empty()) {
                        throw utpl::InvalidArgumentException("transcoder.inputs must be a non-empty array");
                    }

                    // The first entry is the programme source. Any further entry is an alternate
                    // source the channel can cut to - an ad break playlist, for example - and
                    // requires a configuration new enough to describe its role.
                    if (inputs.size() > 1 && this->getVersion() < semver::version(1, 5, 0)) {
                        throw utpl::InvalidArgumentException(
                            "transcoder.inputs holds more than one entry, which requires configuration version 1.5.0 or newer"
                        );
                    }

                    size_t inputIndex = 0;

                    for (auto &input : inputs) {
                        utpl::transcoder::InputParameters inputParams;

                        if (this->getVersion() < semver::version(1, 1, 0)) {
                            auto &input = inputs[0];

                            inputParams.addUri(input["uri"].get<std::string>());

                        } else {
                            auto &input = inputs[0];

                            if (! input.contains("transport")) {
                                throw utpl::InvalidArgumentException("transcoder.input[].transport node does not exist");
                            }

                            {
                                auto &transportJson = input["transport"];

                                std::unique_ptr<utpl::transcoder::InputTransportParameters> base;

                                {
                                    auto &uriNode = transportJson["uri"];

                                    if (uriNode.is_string()) {
                                        inputParams.addUri(uriNode.get<std::string>());

                                    // version 1.2.0
                                    } else {
                                        for (const auto &singleUri : uriNode) {
                                            inputParams.addUri(singleUri.get<std::string>());
                                        }
                                    }
                                }

                                if (transportJson.contains("srt")) {
                                    auto t = std::make_unique<utpl::transcoder::SrtInputTransportParameters>();

                                    {
                                        auto &srtJson = transportJson["srt"];

                                        if (srtJson.contains("latency")) {
                                            t->setLatency(srtJson["latency"].get<utpl::Timestamp>());
                                        }

                                        if (srtJson.contains("mode")) {
                                            auto modeString = utpl::utils::toLower(srtJson["mode"].get<std::string>());

                                            if (modeString == "caller") {
                                                t->setMode(utpl::transcoder::SrtProtocolMode::CALLER);
                                                
                                            } else if (modeString == "listener") {
                                                t->setMode(utpl::transcoder::SrtProtocolMode::LISTENER);

                                            } else {
                                                ERR(("Not supported SRT protocol mode '{}'", modeString));
                                            }
                                        }

                                        if (srtJson.contains("bufferSize")) {
                                            t->setBufferSize(srtJson["bufferSize"].get<size_t>());
                                        }
                                    }

                                    base = std::move(t); inputParams.addTransportParameters(base);
                                }
                                
                                if (transportJson.contains("udp")) {
                                    auto t = std::make_unique<utpl::transcoder::UdpInputTransportParameters>();

                                    {
                                        auto &udpJson = transportJson["udp"];

                                        if (udpJson.contains("multicast")) {
                                            auto &multicastJson = udpJson["multicast"];
                                            if (! multicastJson.is_object()) {
                                                ERR(("Expected UDP multicast node to be an object, but it is not."));

                                            } else {
                                                utpl::transcoder::UdpInputTransportParameters::MulticastParameters multicast;

                                                if (multicastJson.contains("interfaces")) {
                                                    auto &interfacesJson = multicastJson["interfaces"];
                                                    if (! interfacesJson.is_array()) {
                                                        ERR(("Expected UDP multicast interface node to be an array, but it is not."));
                                                        
                                                    } else {
                                                        for (auto &iface : interfacesJson) {
                                                            multicast.addInterface(iface.get<std::string>());
                                                        }
                                                    }
                                                }

                                                t->setMulticastParameters(multicast);
                                            }
                                        }
                                    }

                                    base = std::move(t); inputParams.addTransportParameters(base);
                                }

                                if (transportJson.contains("hls")) {
                                    auto t = std::make_unique<utpl::transcoder::HlsInputTransportParameters>();

                                    {
                                        auto hls = transportJson["hls"];

                                        if (hls.contains("bandwith")) {
                                            t->setBandwith(hls["bandwith"].get<std::size_t>());
                                        }

                                        if (hls.contains("credentials")) {
                                            utpl::Credentials credentials;

                                            _parseCredentials(hls["credentials"], credentials);
                                            
                                            t->setCredentials(credentials);
                                        }

                                        if (hls.contains("scte")) {
                                            utpl::transcoder::ScteInputParameters scteParams;

                                            auto &scte = hls["scte"];
                
                                            if (scte.contains("source")) {
                                                auto scte35Source = utpl::utils::toLower(scte["source"].get<std::string>());
    
                                                if (scte35Source == "segments") {
                                                    scteParams.setSource(utpl::transcoder::ScteSource::SEGMENTS);
    
                                                } else if (scte35Source == "manifest") {
                                                    scteParams.setSource(utpl::transcoder::ScteSource::MANIFEST);
    
                                                } else {
                                                    ERR(("Not supported SCTE-35 source value '{}'", scte35Source));
                                                }
                                            }

                                            t->setScteParameters(scteParams);
                                        }
                                    }

                                    base = std::move(t); inputParams.addTransportParameters(base);
                                }

                                if (transportJson.contains("dash")) {
                                    auto t = std::make_unique<utpl::transcoder::DashInputTransportParameters>();

                                    {
                                        auto dash = transportJson["dash"];

                                        if (dash.contains("bandwidth")) {
                                            auto bandwidth = dash["bandwidth"].get<uint64_t>();

                                            if (
                                                bandwidth < utpl::transcoder::DashInputTransportParameters::MIN_BANDWIDTH ||
                                                bandwidth > utpl::transcoder::DashInputTransportParameters::MAX_BANDWIDTH
                                            ) {
                                                throw utpl::InvalidArgumentException("DASH bandwidth must be between 1000 and 4294967295 bits per second");
                                            }

                                            t->setBandwidth(bandwidth);
                                        }

                                        if (dash.contains("presentationDelay")) {
                                            t->setPresentationDelay(dash["presentationDelay"].get<uint64_t>());
                                        }

                                        if (dash.contains("scte")) {
                                            utpl::transcoder::ScteInputParameters scteParams;

                                            auto &scte = dash["scte"];

                                            if (scte.contains("source")) {
                                                auto scte35Source = utpl::utils::toLower(scte["source"].get<std::string>());

                                                // In-band 'emsg' boxes only: neither GStreamer DASH demuxer exposes the
                                                // MPD EventStream, so "manifest" would silently ingest nothing.
                                                if (scte35Source != "segments") {
                                                    throw utpl::InvalidArgumentException("Not supported DASH SCTE-35 source value '" + scte35Source + "', only 'segments' is supported");
                                                }

                                                scteParams.setSource(utpl::transcoder::ScteSource::SEGMENTS);
                                            }

                                            t->setScteParameters(scteParams);
                                        }
                                    }

                                    base = std::move(t); inputParams.addTransportParameters(base);
                                }

                                if (transportJson.contains("file")) {
                                    auto t = std::make_unique<utpl::transcoder::FileInputTransportParameters>();

                                    base = std::move(t); inputParams.addTransportParameters(base);
                                }

                                if (transportJson.contains("playlist")) {
                                    auto t = std::make_unique<utpl::transcoder::PlaylistTransportInputParameters>();

                                    base = std::move(t); inputParams.addTransportParameters(base);
                                }

                                if (transportJson.contains("ndi")) {
                                    DBG(("Skandha: NDI Input parameters detected"));
                                    auto t = std::make_unique<utpl::transcoder::NdiInputTransportParameters>();
                                    
                                    auto ndiJson = transportJson["ndi"];
                                    
                                    if(ndiJson.contains("urlAddress")) {
                                        t->setUrlAddress(ndiJson["urlAddress"]);
                                    }

                                    if(ndiJson.contains("ndiName")) {
                                        t->setNdiName(ndiJson["ndiName"]);
                                    }

                                    base = std::move(t); inputParams.addTransportParameters(base);
                                }

                                {
                                    const std::string where = "transcoder.inputs[" + std::to_string(inputIndex) + "].transport";

                                    const bool mxlUri = ! inputParams.getUris().empty() &&
                                        utpl::utils::toLower(inputParams.getUris()[0].substr(0, 6)) == "mxl://";

                                    if (transportJson.contains("mxl")) {
                                        auto t = _parseMxlInput(transportJson["mxl"], inputParams.getUris(), where);

                                        base = std::move(t); inputParams.addTransportParameters(base);

                                    } else if (mxlUri) {
                                        throw utpl::InvalidArgumentException(where + ".uri is an MXL domain: " + where + ".mxl must name the flows");
                                    }
                                }
                            }

                            if (input.contains("selector")) {
                                auto selector = input["selector"];

                                if (selector.contains("program")) {
                                    inputParams.getStreamSelector().setProgramNumber(selector["program"].get<int>());
                                }
                            }
                        }

                        if (input.contains("role")) {
                            const auto roleString = utpl::utils::toLower(input["role"].get<std::string>());

                            if (roleString == "primary") {
                                inputParams.setRole(utpl::transcoder::InputRole::PRIMARY);

                            } else if (roleString == "secondary") {
                                inputParams.setRole(utpl::transcoder::InputRole::SECONDARY);

                            } else {
                                throw utpl::InvalidArgumentException("Not supported input role '" + roleString + "'");
                            }
                        }

                        if (input.contains("offAir")) {
                            const auto offAirString = utpl::utils::toLower(input["offAir"].get<std::string>());

                            if (offAirString == "drop") {
                                inputParams.setOffAirBehavior(utpl::transcoder::InputOffAirBehavior::DROP);

                            } else if (offAirString == "pause") {
                                inputParams.setOffAirBehavior(utpl::transcoder::InputOffAirBehavior::PAUSE);

                            } else {
                                throw utpl::InvalidArgumentException("Not supported input offAir behavior '" + offAirString + "'");
                            }
                        }

                        // Optional copy of the decoded source into MXL flows.
                        if (input.contains("mxlTap") && ! input["mxlTap"].is_null()) {
                            const auto &tapJson = input["mxlTap"];

                            const auto tap = _parseMxlFlows(tapJson, "transcoder.inputs[" + std::to_string(inputIndex) + "].mxlTap");

                            inputParams.setMxlTap(tap);

                            // A tap and an input on the same flow would have the engine write what it reads.
                            const auto *mxlInput = dynamic_cast<const utpl::transcoder::MxlInputTransportParameters *>(
                                inputParams.getTransportParameters(utpl::Protocol::MXL)
                            );

                            if (mxlInput && ! inputParams.getUris().empty() && _mxlDomainOfUri(inputParams.getUris()[0]) == tap.getDomain()) {
                                for (const auto &flowId : { tap.getVideoFlowId(), tap.getAudioFlowId() }) {
                                    if (! flowId.empty() && (flowId == mxlInput->getVideoFlowId() || flowId == mxlInput->getAudioFlowId())) {
                                        throw utpl::InvalidArgumentException(
                                            "transcoder.inputs[" + std::to_string(inputIndex) + "].mxlTap writes flow " + flowId + ", which the same input reads"
                                        );
                                    }
                                }
                            }
                        }

                        if (inputIndex == 0) {
                            if (inputParams.getRole() != utpl::transcoder::InputRole::PRIMARY) {
                                throw utpl::InvalidArgumentException("transcoder.inputs[0] is the programme source and must have role 'primary'");
                            }

                            engineParams.setInputParameters(inputParams);

                        } else {
                            engineParams.addInputParameters(inputParams);
                        }

                        inputIndex++;
                    }

                    for (auto &output : transcoder["outputs"]) {
                        utpl::transcoder::OutputParameters outputParams;

                        {
                            utpl::transcoder::OutputType type = utpl::transcoder::OutputType::SINGLE;

                            if (output.contains("type")) {
                                const auto &typeString = utpl::utils::toLower(output["type"].get<std::string>());

                                if (typeString == "single") {
                                    type = utpl::transcoder::OutputType::SINGLE;

                                } else if (typeString == "group") {
                                    type = utpl::transcoder::OutputType::GROUP;

                                } else if (typeString == "mxl") {
                                    type = utpl::transcoder::OutputType::MXL;

                                } else {
                                    type = utpl::transcoder::OutputType::UNKNOWN;

                                    ERR(("Not supported Output type '{}'", typeString));
                                }
                            }

                            outputParams.setType(type);
                        }

                        // The programme, written to MXL flows. It has no transport, container or stream list of
                        // its own: the "mxl" block says which flows, and the streams are made from it.
                        if (outputParams.getType() == utpl::transcoder::OutputType::MXL) {
                            const std::string where = "transcoder.outputs[" + std::to_string(engineParams.getOutputParameters().size()) + "].mxl";

                            if (output.contains("name")) {
                                outputParams.setName(output["name"].get<std::string>());
                            }

                            if (! output.contains("mxl")) {
                                throw utpl::InvalidArgumentException(where + " is required for an output of type 'mxl'");
                            }

                            const auto &mxlJson = output["mxl"];
                            const auto  mxl     = _parseMxlFlows(mxlJson, where);

                            outputParams.setMxl(mxl);

                            if (! mxl.getVideoFlowId().empty()) {
                                utpl::transcoder::StreamParameters streamParams;

                                streamParams.setCodec(utpl::Codec::MXL);
                                streamParams.setVideoParameters(
                                    mxlJson.contains("video") && mxlJson["video"].is_object()
                                        ? _parseMxlVideo(mxlJson["video"], where + ".video")
                                        : utpl::transcoder::VideoParameters()
                                );

                                outputParams.addStream(streamParams);
                            }

                            if (! mxl.getAudioFlowId().empty()) {
                                utpl::transcoder::StreamParameters streamParams;
                                utpl::transcoder::AudioParameters  audioParams;

                                audioParams.setRate(48000);
                                audioParams.setChannels(mxl.getAudioChannels());

                                streamParams.setCodec(utpl::Codec::MXL);
                                streamParams.setAudioParameters(audioParams);

                                outputParams.addStream(streamParams);
                            }

                            engineParams.addOutputParameters(outputParams);

                            continue;
                        }

                        // Legacy
                        if (this->getVersion() < semver::version(1, 1, 0)) {
                            utpl::transcoder::OutputTransport transport;

                            transport.setUri(output["uri"].get<std::string>());

                            outputParams.addTransport(transport);

                        } else if (! output.contains("transports")) {
                            if (this->getVersion() < semver::version(1, 4, 0)) {
                                throw utpl::InvalidArgumentException("Expected a 'transports' stream node, but no corresponding value was found in the JSON.");    
                            }
                            
                        } else {
                            auto transportsJson = output["transports"];

                            for (auto &transportJson : transportsJson) {
                                utpl::transcoder::OutputTransport transport;

                                transport.setUri(transportJson["uri"].get<std::string>());
                                DBG(("Skandha: Output URI: {}", transportJson["uri"].get<std::string>()));
                                
                                {
                                    std::unique_ptr<utpl::transcoder::OutputTransportParameters> base;

                                    if (transportJson.contains("s3")) {
                                        DBG(("Skandha: S3 Output Paramters present in config JSON"));
                                        auto t = std::make_unique<utpl::transcoder::S3OutputTransportParameters>();

                                        {
                                            auto &s3Json = transportJson["s3"];
                                            if (s3Json.contains("bucket")) {
                                                t->setBucket(s3Json["bucket"].get<std::string>());
                                            }

                                            if (s3Json.contains("accessKey")) {
                                                t->setAccessKey(s3Json["accessKey"].get<std::string>());
                                            }

                                            if (s3Json.contains("secretAccessKey")) {
                                                t->setSecretAccessKey(s3Json["secretAccessKey"].get<std::string>());
                                            }

                                            if (s3Json.contains("region")) {
                                                t->setRegion(s3Json["region"].get<std::string>());
                                            }

                                            if (s3Json.contains("keyPrefix")) {
                                                t->setKeyPrefix(s3Json["keyPrefix"].get<std::string>());
                                            }
                                        }

                                        base = std::move(t); transport.addTransportParameters(base);
                                    }

                                    if (transportJson.contains("srt")) {
                                        auto t = std::make_unique<utpl::transcoder::SrtOutputTransportParameters>();

                                        {
                                            auto &srtJson = transportJson["srt"];

                                            if (srtJson.contains("latency")) {
                                                t->setLatency(srtJson["latency"].get<utpl::Timestamp>());
                                            }

                                            if (srtJson.contains("mode")) {
                                                auto modeString = utpl::utils::toLower(srtJson["mode"].get<std::string>());

                                                if (modeString == "caller") {
                                                    t->setMode(utpl::transcoder::SrtProtocolMode::CALLER);
                                                    
                                                } else if (modeString == "listener") {
                                                    t->setMode(utpl::transcoder::SrtProtocolMode::LISTENER);

                                                } else {
                                                    ERR(("Not supported SRT protocol mode '{}'", modeString));
                                                }
                                            }

                                            if (srtJson.contains("bufferSize")) {
                                                t->setBufferSize(srtJson["bufferSize"].get<size_t>());
                                            }
                                        }

                                        base = std::move(t); transport.addTransportParameters(base);
                                    }
                                    
                                    if (transportJson.contains("udp")) {
                                        auto t = std::make_unique<utpl::transcoder::UdpOutputTransportParameters>();

                                        {
                                            auto &udpJson = transportJson["udp"];

                                            if (udpJson.contains("multicast")) {
                                                auto &multicastJson = udpJson["multicast"];
                                                if (! multicastJson.is_object()) {
                                                    ERR(("Expected UDP multicast node to be an object, but it is not."));

                                                } else {
                                                    utpl::transcoder::UdpOutputTransportParameters::MulticastParameters multicast;

                                                    if (multicastJson.contains("interface")) {
                                                        multicast.setInterface(multicastJson["interface"].get<std::string>());
                                                    }

                                                    if (multicastJson.contains("ttl")) {
                                                        multicast.setTtl(multicastJson["ttl"].get<uint8_t>());

                                                        if (multicast.getTtl() == 0) {
                                                            ERR(("TTL must be greater than 0 - ignoring the provided value and using the default."));

                                                            multicast.setTtl(utpl::transcoder::UdpOutputTransportParameters::MulticastParameters::DEFAULT_TTL);
                                                        }
                                                    }

                                                    t->setMulticastParameters(multicast);
                                                }
                                            }

                                            if (udpJson.contains("unicast")) {
                                                auto &unicastJson = udpJson["unicast"];
                                                if (! unicastJson.is_object()) {
                                                    ERR(("Expected UDP unicast node to be an object, but it is not."));

                                                } else {
                                                    utpl::transcoder::UdpOutputTransportParameters::UnicastParameters unicast;

                                                    if (unicastJson.contains("ttl")) {
                                                        unicast.setTtl(unicastJson["ttl"].get<uint8_t>());

                                                        if (unicast.getTtl() == 0) {
                                                            ERR(("TTL must be greater than 0 - ignoring the provided value and using the default."));

                                                            unicast.setTtl(utpl::transcoder::UdpOutputTransportParameters::UnicastParameters::DEFAULT_TTL);
                                                        }
                                                    }

                                                    t->setUnicastParameters(unicast);
                                                }
                                            }
                                        }

                                        base = std::move(t); transport.addTransportParameters(base);
                                    }
                                    
                                    {
                                        auto t = std::make_unique<utpl::transcoder::HlsOutputTransportParameters>();

                                        if (transportJson.contains("hls")) {
                                            auto hlsJson = transportJson["hls"];

                                            if (hlsJson.contains("numSegments")) {
                                                t->setMaxSegments(hlsJson["numSegments"].get<int>());
                                            }

                                            if (hlsJson.contains("segmentLength")) {
                                                t->setMaxSegmentDuration(hlsJson["segmentLength"].get<int>() * 1000);
                                            }
                                            
                                            if (hlsJson.contains("credentials")) {
                                                utpl::Credentials credentials;

                                                _parseCredentials(hlsJson["credentials"], credentials);
                                                
                                                t->setCredentials(credentials);
                                            }

                                            if (hlsJson.contains("singleDirectory")) {
                                                t->setSingleDir(hlsJson["singleDirectory"].get<bool>());
                                            }

                                            if (hlsJson.contains("vodMode")) {
                                                t->setVodMode(hlsJson["vodMode"].get<bool>());
                                            }

                                            if (hlsJson.contains("transferMode")) {
                                                auto mode = t->getTransferType();

                                                {
                                                    auto modeStr = hlsJson["transferMode"].get<std::string>();

                                                    if (modeStr == "http_put") {
                                                        mode = utpl::transcoder::HlsOutputTransportParameters::TransferType::HTTP_PUT;

                                                    } else if (modeStr == "http_dav") {
                                                        mode = utpl::transcoder::HlsOutputTransportParameters::TransferType::HTTP_DAV;

                                                    } else {
                                                        ERR(("Not supported HLS transferMode '{}'", modeStr));
                                                    }
                                                }
                                                
                                                t->setTransferType(mode);
                                            }

                                            if (hlsJson.contains("transferFraming")) {
                                                auto mode = t->getTransferFraming();

                                                {
                                                    auto modeStr = hlsJson["transferFraming"].get<std::string>();

                                                    if (modeStr == "chunked") {
                                                        mode = utpl::transcoder::HlsOutputTransportParameters::TransferFraming::CHUNKED;

                                                    } else if (modeStr == "fixed") {
                                                        mode = utpl::transcoder::HlsOutputTransportParameters::TransferFraming::FIXED;

                                                    } else {
                                                        ERR(("Not supported HLS transferFraming '{}'", modeStr));
                                                    }
                                                }

                                                t->setTransferFraming(mode);
                                            }

                                            if (hlsJson.contains("timeout")) {
                                                t->setTimeout(hlsJson["timeout"].get<utpl::Timestamp>());
                                            }

                                            if (hlsJson.contains("retry")) {
                                                auto &retriesJson = hlsJson["retry"];

                                                if (retriesJson.contains("count")) {
                                                    t->setRetryCount(retriesJson["count"].get<utpl::Timestamp>());
                                                }

                                                if (retriesJson.contains("interval")) {
                                                    t->setRetryInterval(retriesJson["interval"].get<size_t>());
                                                }
                                            }

                                            if (hlsJson.contains("adMarkers")) {
                                                auto &adMarkersJson = hlsJson["adMarkers"];

                                                for (auto marker : adMarkersJson) {
                                                    auto markerStr = marker.get<std::string>();

                                                    if (markerStr == "elemental") {
                                                        t->addAdMarker(utpl::hls::ScteMarker::ELEMENTAL);

                                                    } else if (markerStr == "enhanced") {
                                                        t->addAdMarker(utpl::hls::ScteMarker::ENHANCED); 

                                                    } else {
                                                        WARN(("Not supported Ad marker type: '{}'", markerStr));
                                                    }
                                                }
                                            }

                                            if (hlsJson.contains("authentication")) {
                                                auto auth = t->getAuthentication();

                                                {
                                                    auto authStr = hlsJson["authentication"].get<std::string>();

                                                    if (authStr == "basic") {
                                                        auth = utpl::Authentication::BASIC;

                                                    } else if (authStr == "digest") {
                                                        auth = utpl::Authentication::DIGEST;
                                                    }
                                                }

                                                t->setAuthentication(auth);
                                            }

                                            if (hlsJson.contains("baseFileName")) {
                                                t->setBaseFileName(hlsJson["baseFileName"].get<std::string>());
                                            }

                                            base = std::move(t); transport.addTransportParameters(base);
                                        }
                                    }

                                    // CMAF DASH output. Defaults apply when the URI is dash+... without a "dash" object.
                                    {
                                        utpl::Protocol subprotocol;

                                        if (transportJson.contains("dash") || utpl::parseProtocol(transport.getUri(), subprotocol) == utpl::Protocol::DASH) {
                                            using DashParameters = utpl::transcoder::DashOutputTransportParameters;

                                            auto t = std::make_unique<DashParameters>();

                                            if (transportJson.contains("dash")) {
                                                auto &dashJson = transportJson["dash"];

                                                if (dashJson.contains("segmentDuration")) {
                                                    auto duration = dashJson["segmentDuration"].get<utpl::Timestamp>();

                                                    if (duration < DashParameters::MIN_SEGMENT_DURATION) {
                                                        throw utpl::InvalidArgumentException("DASH segmentDuration must be at least " + std::to_string(DashParameters::MIN_SEGMENT_DURATION) + " ms");
                                                    }

                                                    t->setSegmentDuration(duration);
                                                }

                                                if (dashJson.contains("numSegments")) {
                                                    auto segments = dashJson["numSegments"].get<int>();

                                                    if (segments < DashParameters::MIN_SEGMENTS) {
                                                        throw utpl::InvalidArgumentException("DASH numSegments must be at least " + std::to_string(DashParameters::MIN_SEGMENTS));
                                                    }

                                                    t->setWindowSegments(segments);
                                                }

                                                if (dashJson.contains("vodMode")) {
                                                    t->setVodMode(dashJson["vodMode"].get<bool>());
                                                }

                                                if (dashJson.contains("presentationDelay")) {
                                                    t->setPresentationDelay(dashJson["presentationDelay"].get<utpl::Timestamp>());
                                                }

                                                if (dashJson.contains("minBufferTime")) {
                                                    t->setMinBufferTime(dashJson["minBufferTime"].get<utpl::Timestamp>());
                                                }

                                                if (dashJson.contains("minimumUpdatePeriod")) {
                                                    t->setMinimumUpdatePeriod(dashJson["minimumUpdatePeriod"].get<utpl::Timestamp>());
                                                }

                                                if (dashJson.contains("utcTimingUrl")) {
                                                    t->setUtcTimingUrl(dashJson["utcTimingUrl"].get<std::string>());
                                                }

                                                if (dashJson.contains("baseFileName")) {
                                                    t->setBaseFileName(dashJson["baseFileName"].get<std::string>());
                                                }

                                                if (dashJson.contains("credentials")) {
                                                    utpl::Credentials credentials;

                                                    _parseCredentials(dashJson["credentials"], credentials);

                                                    t->setCredentials(credentials);
                                                }

                                                if (dashJson.contains("authentication")) {
                                                    auto authStr = dashJson["authentication"].get<std::string>();

                                                    if (authStr == "basic") {
                                                        t->setAuthentication(utpl::Authentication::BASIC);

                                                    } else if (authStr == "digest") {
                                                        t->setAuthentication(utpl::Authentication::DIGEST);

                                                    } else {
                                                        throw utpl::InvalidArgumentException("Not supported DASH authentication '" + authStr + "'");
                                                    }
                                                }

                                                if (dashJson.contains("transferMode")) {
                                                    auto modeStr = dashJson["transferMode"].get<std::string>();

                                                    if (modeStr == "http_put") {
                                                        t->setTransferType(DashParameters::TransferType::HTTP_PUT);

                                                    } else if (modeStr == "http_dav") {
                                                        t->setTransferType(DashParameters::TransferType::HTTP_DAV);

                                                    } else {
                                                        throw utpl::InvalidArgumentException("Not supported DASH transferMode '" + modeStr + "'");
                                                    }
                                                }

                                                if (dashJson.contains("transferFraming")) {
                                                    auto framingStr = dashJson["transferFraming"].get<std::string>();

                                                    if (framingStr == "chunked") {
                                                        t->setTransferFraming(DashParameters::TransferFraming::CHUNKED);

                                                    } else if (framingStr == "fixed") {
                                                        t->setTransferFraming(DashParameters::TransferFraming::FIXED);

                                                    } else {
                                                        throw utpl::InvalidArgumentException("Not supported DASH transferFraming '" + framingStr + "'");
                                                    }
                                                }

                                                if (dashJson.contains("timeout")) {
                                                    t->setTimeout(dashJson["timeout"].get<utpl::Timestamp>());
                                                }

                                                if (dashJson.contains("retry")) {
                                                    auto &retryJson = dashJson["retry"];

                                                    if (retryJson.contains("count")) {
                                                        t->setRetryCount(retryJson["count"].get<size_t>());
                                                    }

                                                    if (retryJson.contains("interval")) {
                                                        t->setRetryInterval(retryJson["interval"].get<utpl::Timestamp>());
                                                    }
                                                }
                                            }

                                            base = std::move(t); transport.addTransportParameters(base);
                                        }
                                    }

                                    if (transportJson.contains("file")) {
                                        auto t = std::make_unique<utpl::transcoder::FileOutputTransportParameters>();

                                        base = std::move(t); transport.addTransportParameters(base);
                                    }
                                }

                                outputParams.addTransport(transport);
                            }
                        }

                        if (output.contains("name")) {
                            outputParams.setName(output["name"].get<std::string>());
                        }

                        if (! output.contains("container")) {
                            if (this->getVersion() < semver::version(1, 4, 0)) {
                                throw utpl::InvalidArgumentException("Expected a 'container' stream node, but no corresponding value was found in the JSON.");
                            }

                        } else {
                            auto containerStr = output["container"].get<std::string>();

                            if (containerStr == "mpegts") {
                                outputParams.setContainer(utpl::Container::MPEGTS);

                            } else if (containerStr == "es") {
                                outputParams.setContainer(utpl::Container::ES);

                            } else if (containerStr == "fmp4") {
                                outputParams.setContainer(utpl::Container::FMP4);
                            
                            } else {
                                ERR(("Not supported container value '{}'", containerStr));

                                outputParams.setContainer(utpl::Container::UNKNOWN);
                            }
                        }

                        if (output.contains("scte")) {
                            utpl::transcoder::ScteOutputParameters scteParams;

                            auto &scte = output["scte"];

                            if (scte.contains("pid")) {
                                scteParams.setPid(scte["pid"].get<int16_t>());
                            }

                            if (scte.contains("mode")) {
                                auto modeStr = scte["mode"].get<std::string>();

                                if (modeStr == "passthrough") {
                                    scteParams.setMode(utpl::transcoder::ScteOutputMode::PASSTHROUGH);
                                    
                                } else {
                                    scteParams.setMode(utpl::transcoder::ScteOutputMode::REMOVAL);
                                }
                            }

                            if (scte.contains("nullInterval")) {
                                scteParams.setNullInterval(scte["nullInterval"].get<int>());
                            }

                            outputParams.setScteParameters(scteParams);
                        }

                        // Streams
                        if (! output.contains("streams")) {
                            throw utpl::InvalidArgumentException("Expected a 'streams' output node, but no corresponding value was found in the JSON.");

                        } else {
                            auto &streams = output["streams"];

                            for (auto &stream : streams) {
                                utpl::transcoder::StreamParameters streamParams;

                                {
                                    const auto &typeStr = utpl::utils::toLower(stream["type"].get<std::string>());

                                    if (typeStr == "audio" || typeStr == "video") {
                                        const auto &codecStr = stream["codec"].get<std::string>();

                                        if (codecStr == "passthrough") {
                                            streamParams.setCodec(utpl::Codec::PASSTHROUGH);

                                        } else if (codecStr == "es") {
                                            streamParams.setCodec(utpl::Codec::ES);

                                        } else if (codecStr == "mp1") {
                                            streamParams.setCodec(utpl::Codec::MP1);

                                        } else if (codecStr == "mp2") {
                                            streamParams.setCodec(utpl::Codec::MP2);

                                        } else if (codecStr == "mp3") {
                                            streamParams.setCodec(utpl::Codec::MP3);

                                        } else if (codecStr == "aac") {
                                            streamParams.setCodec(utpl::Codec::AAC);

                                        } else if (codecStr == "ac3") {
                                            streamParams.setCodec(utpl::Codec::AC3);
                                        
                                        } else if (codecStr == "eac3") {
                                            streamParams.setCodec(utpl::Codec::EAC3);

                                        } else if (codecStr == "h264") {
                                            streamParams.setCodec(utpl::Codec::H264);

                                        } else if (codecStr == "h265") {
                                            streamParams.setCodec(utpl::Codec::H265);

                                        } else {
                                            streamParams.setCodec(utpl::Codec::UNKNOWN);
                                        }

                                        if (stream.contains(codecStr)) {
                                            auto &encoderParams = stream[codecStr];

                                            if (encoderParams.contains("encoder")) {
                                                utpl::transcoder::GstEncoderParameters params(encoderParams["encoder"].get<std::string>());
                                                if (encoderParams.contains("properties")) {
                                                    auto &props = encoderParams["properties"];

                                                    for (const auto &prop : props.items()) {
                                                        if (! prop.value().is_string()) {
                                                            WARN(("Expected string value for property '{}' value - value will be automatically converted", prop.key()));
                                                        }

                                                        params.addProperty(prop.key(), prop.value());
                                                    }
                                                }

                                                if (encoderParams.contains("caps")) {
                                                    auto &caps = encoderParams["caps"];

                                                    for (const auto &cap : caps.items()) {
                                                        if (! cap.value().is_string()) {
                                                            WARN(("Expected string value for capability '{}' value - value will be automatically converted", cap.key()));
                                                        }

                                                        params.addCapability(cap.key(), cap.value());
                                                    }
                                                }

                                                streamParams.setEncoderParameters(params);
                                            }
                                        }

                                        if (typeStr == "audio") {
                                            utpl::transcoder::AudioParameters audioParams;

                                            {
                                                auto &audio = stream["audio"];

                                                if (audio.contains("channels")) {
                                                    audioParams.setChannels(audio["channels"].get<int>());
                                                }

                                                if (audio.contains("rate")) {
                                                    audioParams.setRate(audio["rate"].get<int>());
                                                }

                                                if (audio.contains("bitrate")) {
                                                    audioParams.setBitrate(audio["bitrate"].get<int>());
                                                }

                                                if (audio.contains("stereoSource")) {
                                                    audioParams.setStereoSource(audio["stereoSource"].get<bool>());
                                                }

                                                if (audio.contains("inputLayout")) {
                                                    auto str = audio["inputLayout"].get<std::string>();

                                                    utpl::transcoder::AudioInputLayout inputLayout;

                                                    if (str == "metadata") {
                                                        inputLayout = utpl::transcoder::AudioInputLayout::METADATA;

                                                    } else if (str == "auto") {
                                                        inputLayout = utpl::transcoder::AudioInputLayout::AUTO;

                                                    } else if (str == "stereo") {
                                                        inputLayout = utpl::transcoder::AudioInputLayout::STEREO;

                                                    } else {
                                                        throw utpl::InvalidArgumentException("Not supported audio input layout '" + str + "'");
                                                    }

                                                    // stereoSource is the older spelling of inputLayout "stereo"
                                                    if (
                                                        audio.contains("stereoSource") &&
                                                        audio["stereoSource"].get<bool>() != (inputLayout == utpl::transcoder::AudioInputLayout::STEREO)
                                                    ) {
                                                        throw utpl::InvalidArgumentException(
                                                            "audio.stereoSource contradicts audio.inputLayout '" + str + "' - set only inputLayout"
                                                        );
                                                    }

                                                    audioParams.setInputLayout(inputLayout);
                                                }
                                            }

                                            streamParams.setAudioParameters(audioParams);

                                        } else {
                                            utpl::transcoder::VideoParameters videoParams;

                                            {
                                                auto &video = stream["video"];

                                                if (video.contains("width")) {
                                                    videoParams.setWidth(video["width"].get<int>());
                                                }

                                                if (video.contains("height")) {
                                                    videoParams.setHeight(video["height"].get<int>());
                                                }

                                                if (video.contains("rate")) {
                                                    videoParams.setFramerate(utpl::Fraction(video["rate"].get<int>(), 1));
                                                }

                                                if (video.contains("format")) {
                                                    utpl::PixelFormat format = utpl::parsePixelFormat(video["format"].get<std::string>());

                                                    if (format == utpl::PixelFormat::UNKNOWN) {
                                                        ERR(("Unknown pixel format specified! '{}'", video["format"].get<std::string>()));

                                                    } else {
                                                        videoParams.setPixelFormat(format);
                                                    }
                                                }

                                                if (video.contains("interlace")) {
                                                    auto str = video["interlace"].get<std::string>();

                                                    if (str == "progressive") {
                                                        videoParams.setInterlace(utpl::Interlace::PROGRESSIVE);

                                                    } else if (str == "interleaved") {
                                                        // Fields are woven from frames at twice 'rate', so the rate must be known.
                                                        // x265enc does not field-code, so H.265 would come out mislabelled.
                                                        if (! videoParams.getFramerate()) {
                                                            throw utpl::InvalidArgumentException("Interleaved video requires 'rate' to be set.");
                                                        }

                                                        if (streamParams.getCodec() != utpl::Codec::H264) {
                                                            throw utpl::InvalidArgumentException("Interleaved video is supported only for the 'h264' codec.");
                                                        }

                                                        videoParams.setInterlace(utpl::Interlace::INTERLEAVED);

                                                    } else {
                                                        throw utpl::InvalidArgumentException("Not supported video interlace '" + str + "'");
                                                    }
                                                }

                                                if (video.contains("fieldOrder")) {
                                                    auto str = video["fieldOrder"].get<std::string>();

                                                    if (str == "tff") {
                                                        videoParams.setFieldOrder(utpl::FieldOrder::TOP_FIELD_FIRST);

                                                    } else if (str == "bff") {
                                                        videoParams.setFieldOrder(utpl::FieldOrder::BOTTOM_FIELD_FIRST);

                                                    } else {
                                                        throw utpl::InvalidArgumentException("Not supported video field order '" + str + "'");
                                                    }
                                                }
                                            }

                                            streamParams.setVideoParameters(videoParams);
                                        }

                                    } else if (typeStr == "link") {
                                        utpl::transcoder::LinkParameters linkParameters;

                                        if (! stream.contains("link") || ! stream["link"].is_object()) {
                                            throw utpl::InvalidArgumentException("Link stream type expects a 'link' object, but it is missing or not an object.");

                                        } else {
                                            const auto &linkJson = stream["link"];
                                            
                                            auto linkTypeString = utpl::utils::toLower(linkJson["type"].get<std::string>());

                                            if (linkTypeString == "output") {
                                                const auto &outputJson = linkJson["output"];

                                                {
                                                    utpl::transcoder::OutputLinkParameters outputLinkParameters;

                                                    if (! outputJson.contains("name")) {
                                                        throw utpl::InvalidArgumentException("Output link stream type expects a 'name' element, but it is missing.");
                                                        
                                                    } else {
                                                        outputLinkParameters.setOutputName(outputJson["name"].get<std::string>());
                                                    }

                                                    if (outputJson.contains("modifier")) {
                                                        outputLinkParameters.setOutputNameModifier(outputJson["modifier"].get<std::string>());
                                                    }

                                                    linkParameters.setOutputParameters(outputLinkParameters);
                                                }

                                            } else {
                                                throw utpl::InvalidArgumentException("Not supported type of link '" + linkTypeString + "'");        
                                            }
                                        }

                                        streamParams.setLinkParameters(linkParameters);

                                    } else {
                                        throw utpl::InvalidArgumentException("Not supported type of stream '" + typeStr + "'");
                                    }
                                }

                                if (stream.contains("pid")) {
                                    if (! streamParams.hasAudioParameters() && ! streamParams.hasVideoParameters()) {
                                        throw utpl::InvalidArgumentException("Stream 'pid' is supported only on audio and video streams.");
                                    }

                                    streamParams.setPid(_parseStreamPid(stream["pid"]));
                                }

                                outputParams.addStream(streamParams);
                            }

                            _checkOutputPids(outputParams, output);
                        }

                        engineParams.addOutputParameters(outputParams);
                    }

                    // link outputs to their groups
                    for (auto &groupOutput : engineParams.getOutputParameters()) {
                        if (groupOutput.getType() == utpl::transcoder::OutputType::GROUP) {
                            for (const auto &stream : groupOutput.getStreams()) {
                                if (stream.hasLinkParameters()) {
                                    const auto &linkParameters = stream.getLinkParameters();

                                    if (linkParameters.hasOutputParameters()) {
                                        const auto &outputParameters = linkParameters.getOutputParameters();

                                        auto outputIt = std::find_if(
                                            engineParams.getOutputParameters().begin(), 
                                            engineParams.getOutputParameters().end(), 
                                            [&outputParameters](utpl::transcoder::OutputParameters &item) {
                                                return item.getName() == outputParameters.getOutputName();
                                            }
                                        );

                                        if (outputIt == engineParams.getOutputParameters().end())  {
                                            throw utpl::InvalidArgumentException(
                                                "Group output '" + groupOutput.getName() + "' has a linkage to not existing single output '" + outputParameters.getOutputName() + "'"
                                            );
                                            
                                        } else {
                                            outputIt->setGroupName(groupOutput.getName());
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (transcoder.contains("squeeze")) {
                        utpl::transcoder::PredefinedSqueezes squeezes;

                        auto &squeeze = transcoder["squeeze"];
                        if (squeeze.is_array()) {
                            for (auto &s : squeeze) {
                                if (s.is_object()) {
                                    utpl::transcoder::SqueezeParameters params;

                                    {
                                        std::string name;

                                        if (s.contains("name")) {
                                            auto &n = s["name"];
                                            if (! n.is_null()) {
                                                name = n.get<std::string>();
                                            }
                                        }

                                        if (! name.empty()) {
                                            _parseSqueeze(s, params);           

                                            squeezes.addSqueeze(name, params);
                                        }
                                    }
                                }
                            }
                        }

                        engineParams.setPredefinedSqueezes(squeezes);
                    }

                    if (transcoder.contains("scte")) {
                        utpl::transcoder::ScteParameters scteParams;

                        auto &scte = transcoder["scte"];
                        if (scte.contains("pois")) {
                            utpl::transcoder::PoisParameters poisParams;

                            auto &pois = scte["pois"];
                            if (! pois.is_null()) {
                                if (pois.contains("enable")) {
                                    poisParams.setEnabled(pois["enable"].get<bool>());
                                }

                                if (pois.contains("server")) {
                                    auto &server = pois["server"];

                                    if (server.contains("uri")) {
                                        poisParams.setEndpoint(server["uri"].get<std::string>());        
                                    }

                                    if (server.contains("acquisitionPointId")) {
                                        poisParams.setAcquisitionPointId(server["acquisitionPointId"].get<std::string>());
                                    }

                                    if (pois.contains("timeout")) {
                                        poisParams.setTimeout(pois["timeout"].get<int>());
                                    }
                                }
                            }

                            scteParams.setPoisParameters(poisParams);
                        }

                        engineParams.setScteParameters(scteParams);
                    }

                    if (transcoder.contains("blanking")) {
                        utpl::transcoder::BlackoutParameters blankingParams;

                        _parseBlackout(transcoder["blanking"], blankingParams);

                        engineParams.setBlankingParameters(blankingParams);
                    }

                    if (transcoder.contains("blackout")) {
                        utpl::transcoder::BlackoutParameters blackoutParams;

                        _parseBlackout(transcoder["blackout"], blackoutParams);

                        engineParams.setBlackoutParameters(blackoutParams);
                    }

                    if (transcoder.contains("adInsertion")) {
                        if (this->getVersion() < semver::version(1, 5, 0)) {
                            throw utpl::InvalidArgumentException(
                                "transcoder.adInsertion requires configuration version 1.5.0 or newer"
                            );
                        }

                        utpl::transcoder::AdInsertionParameters adParams;

                        auto &adJson = transcoder["adInsertion"];

                        if (adJson.contains("enable")) {
                            adParams.setEnabled(adJson["enable"].get<bool>());
                        }

                        if (adJson.contains("maxBreakDuration")) {
                            adParams.setMaxBreakDuration(adJson["maxBreakDuration"].get<utpl::Timestamp>());
                        }

                        if (adJson.contains("outputScte")) {
                            const auto modeString = utpl::utils::toLower(adJson["outputScte"].get<std::string>());

                            if (modeString == "passthrough") {
                                adParams.setOutputScte(utpl::transcoder::AdInsertionOutputScte::PASSTHROUGH);

                            } else if (modeString == "suppress") {
                                adParams.setOutputScte(utpl::transcoder::AdInsertionOutputScte::SUPPRESS);

                            } else {
                                throw utpl::InvalidArgumentException("Not supported ad insertion outputScte mode '" + modeString + "'");
                            }
                        }

                        // The trigger is described exactly like a blackout rule set: which
                        // segmentation types open and close a section, and which delivery
                        // restrictions to honor.
                        if (adJson.contains("trigger")) {
                            utpl::transcoder::BlackoutParameters trigger;

                            _parseBlackout(adJson["trigger"], trigger);

                            // The trigger has no independent enable of its own.
                            trigger.setEnabled(true);

                            adParams.setTrigger(trigger);
                        }

                        engineParams.setAdInsertionParameters(adParams);
                    }

                }

                transcoderParams.setEngineParameters(engineParams);
            }

            ret.setTranscoderParameters(transcoderParams);
        }

        // Parse application section
        if (json.contains("application")) {
            app::ApplicationParameters appParams;

            auto appConfig = json["application"];

            if (appConfig.contains("licenseServer")) {
                auto licServJson = appConfig["licenseServer"];

                if (licServJson.contains("uri")) {
                    appParams.setLicenseServerUri(licServJson["uri"].get<std::string>());
                }
            }

            if (appConfig.contains("gstreamer")) {
                auto gstConfig = appConfig["gstreamer"];

                if (gstConfig.contains("plugins")) {
                    auto gstPlugins = gstConfig["plugins"];

                    if (gstPlugins.is_array()) {
                        for (auto &gstElement : gstPlugins) {
                            GstRank rank = GST_RANK_NONE;

                            {
                                const auto &val = gstElement["rank"].get<std::string>();

                                if (val == "NONE") {
                                    rank = GST_RANK_NONE;

                                } else if (val == "MARGINAL") {
                                    rank = GST_RANK_MARGINAL;

                                } else if (val == "SECONDARY") {
                                    rank = GST_RANK_SECONDARY;

                                } else if (val == "PRIMARY") {
                                    rank = GST_RANK_PRIMARY;

                                } else {
                                    throw utpl::InvalidArgumentException("Invalid rank value '" + val + "'");
                                }
                            }

                            appParams.addGstPluginParameters(
                                utpl::transcoder::GstPluginParameters(
                                    gstElement["name"].get<std::string>(), rank
                                )
                            );
                        }
                    }
                }
            }

            ret.setApplicationParameters(appParams);
        }


    } catch (const utpl::Exception &ex) {
        throw;

    } catch (const std::exception &ex) {
        ERR(("Caught an exception while parsing the configuration file. Maximum supported version: {}, minimum: {}, parsed config version: '{}' (error: '{}')",
            VERSION_MAX, VERSION_MIN, this->getVersion().to_string(), ex.what()
        ));

        throw utpl::InvalidArgumentException("Unable to parse config file: '" + std::string(ex.what()) + "'");
    }

    return ret;
}

app::DebugConfig app::MultiVersionDebugConfigJsonParser::parse(const std::string &path) {
    app::DebugConfig ret;

    try {
        auto json = this->begin(path);

        {
            DebugParameters debugParams;

            auto &application = json["application"];
            {
                if (application.contains("logger")) {
                    auto &logger = application["logger"];

                    if (logger.contains("transcoder")) {
                        utpl::LoggerParameters loggerParams;

                        loggerParams.setBasicPattern(false);

                        _parseLogger(logger["transcoder"], loggerParams);

                        debugParams.setTranscoderLogger(loggerParams); 
                    }
                    

                    if (logger.contains("gstreamer")) {
                        utpl::LoggerParameters loggerParams;

                        loggerParams.setBasicPattern(false);

                        _parseLogger(logger["gstreamer"], loggerParams);

                        debugParams.setGstreamerLogger(loggerParams);
                    }
                }
            }

            ret.setDebugParameters(debugParams);
        }

    } catch (const utpl::Exception &ex) {
        throw;

    } catch (const std::exception &ex) {
        throw utpl::InvalidArgumentException("Unable to parse config file: '" + std::string(ex.what()) + "'");
    }

    return ret;
}
