#ifndef __UTPL_TRANSCODER_INPUT_H__
#define __UTPL_TRANSCODER_INPUT_H__

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <optional>

#include "utpl/common/utils/class.h"
#include "utpl/pipeline/element.h"
#include "utpl/transcoder/mxlparameters.h"
#include "utpl/transcoder/stream.h"

namespace utpl {
    namespace transcoder {
        class InputParameters;

        // A stable, ordered playlist entry supplied by the control plane.  Keeping this
        // value type in the media library avoids coupling PlaylistInput to SQLite/API types.
        struct PlaylistSnapshotItem {
            std::string id;
            std::string uri;
            std::string attributes;
        };

        class InputTransportParameters {
            public:
                InputTransportParameters() {
                    this->_protocol = Protocol::UNKNOWN;
                }

                InputTransportParameters(Protocol protocol) {
                    this->_protocol = protocol;
                }

                virtual ~InputTransportParameters() {
                }

                virtual std::unique_ptr<InputTransportParameters> clone() const = 0;

                virtual Protocol getProtocol() const {
                    return this->_protocol;
                }

                virtual void merge(const InputTransportParameters &other) {
                }

            private:
                Protocol _protocol;
        };

        class SrtInputTransportParameters : public InputTransportParameters {
            public:
                static const utpl::Timestamp DEFAULT_LATENCY = 125;
                static const SrtProtocolMode DEFAULT_MODE    = SrtProtocolMode::UNKNOWN;

            public:
                SrtInputTransportParameters() : SrtInputTransportParameters(SrtProtocolMode::UNKNOWN, DEFAULT_LATENCY, 0) {
                }

                SrtInputTransportParameters(SrtProtocolMode mode, utpl::Timestamp latency, size_t bufferSize) : InputTransportParameters(Protocol::SRT) {
                    this->_latency    = latency;
                    this->_mode       = mode;
                    this->_bufferSize = bufferSize;
                }

                SrtInputTransportParameters(const SrtInputTransportParameters *other) : InputTransportParameters(other->getProtocol()) {
                    this->_latency    = other->_latency;
                    this->_mode       = other->_mode;
                    this->_bufferSize = other->_bufferSize;
                }

                std::unique_ptr<InputTransportParameters> clone() const override {
                    return std::make_unique<SrtInputTransportParameters>(this);
                }

                utpl::Timestamp getLatency() const {
                    return this->_latency;
                }

                void setLatency(utpl::Timestamp ts) {
                    this->_latency = ts;
                }

                SrtProtocolMode getMode() const {
                    return this->_mode;
                }

                void setMode(SrtProtocolMode mode) {
                    this->_mode = mode;
                }

                size_t getBufferSize() const {
                    return this->_bufferSize;
                }

                void setBufferSize(size_t size) {
                    this->_bufferSize = size;
                }

                void merge(const InputTransportParameters &other) override {
                    auto *srtParams = reinterpret_cast<const SrtInputTransportParameters *>(&other);

                    if (srtParams) {
                        if (srtParams->getLatency() != DEFAULT_LATENCY) {
                            this->_latency = srtParams->getLatency();
                        }

                        if (srtParams->getBufferSize() != 0) {
                            this->_bufferSize = srtParams->getBufferSize();
                        }

                        if (srtParams->getMode() != SrtProtocolMode::UNKNOWN) {
                            this->_mode = srtParams->getMode();
                        }
                    }
                }

            private:
                utpl::Timestamp _latency;
                SrtProtocolMode _mode;
                size_t          _bufferSize;
        };

        class UdpInputTransportParameters : public InputTransportParameters {
            public:
                class MulticastParameters {
                    public:
                        const std::vector<std::string> &getInterfaces() const {
                            return this->_interfaces;
                        }

                        void clearInterfaces() {
                            this->_interfaces.clear();
                        }

                        void addInterface(const std::string &interface) {
                            this->_interfaces.push_back(interface);
                        }

                    private:
                        std::vector<std::string> _interfaces;
                };

            public:
                UdpInputTransportParameters() : InputTransportParameters(Protocol::UDP) {
                }

                std::unique_ptr<InputTransportParameters> clone() const override {
                    return std::make_unique<UdpInputTransportParameters>();
                }

                const MulticastParameters &getMulticastParameters() const {
                    return this->_multicast;
                }

                void setMulticastParameters(const MulticastParameters &parameters) {
                    this->_multicast = parameters;
                }

            private:
                MulticastParameters _multicast;
        };

        class FileInputTransportParameters : public InputTransportParameters {
            public:
                FileInputTransportParameters() : InputTransportParameters(Protocol::FILE) {
                }

                std::unique_ptr<InputTransportParameters> clone() const override {
                    return std::make_unique<FileInputTransportParameters>();
                }
        };

        class HlsInputTransportParameters : public InputTransportParameters {
            public:
                static const uint64_t MAX_BANDWITH = 4294967L * 1000L;
                static const uint64_t MIN_BANDWITH = 1000L;

            public:
                HlsInputTransportParameters() : HlsInputTransportParameters(MAX_BANDWITH) {
                }

                HlsInputTransportParameters(uint64_t bandwith) : InputTransportParameters(Protocol::HLS) {
                    this->_bandwith = bandwith;
                }

                HlsInputTransportParameters(const HlsInputTransportParameters *other) : InputTransportParameters(other->getProtocol()) {
                    this->_bandwith = other->_bandwith;
                    this->_credentials = other->_credentials;
                    this->_scteParameters = other->_scteParameters;
                    this->_authentication = other->_authentication;
                }

                std::unique_ptr<InputTransportParameters> clone() const override {
                    return std::make_unique<HlsInputTransportParameters>(this);
                }

                std::size_t getBandwith() const {
                    return this->_bandwith;
                }

                void setBandwith(std::size_t bandwith) {
                    this->_bandwith = bandwith;
                }

                const Credentials &getCredentials() const {
                    return this->_credentials;
                }

                void setCredentials(const Credentials &cred) {
                    this->_credentials = cred;
                }

                const ScteInputParameters &getScteParameters() const {
                    return this->_scteParameters;
                }

                void setScteParameters(const ScteInputParameters &scteParams) {
                    this->_scteParameters = scteParams;
                }

                void merge(const InputTransportParameters &other) override {
                    auto *hlsParams = reinterpret_cast<const HlsInputTransportParameters *>(&other);
                    
                    if (hlsParams) {
                        if (hlsParams->getBandwith() != MAX_BANDWITH) {
                            this->_bandwith = hlsParams->getBandwith();
                        }

                        if (! hlsParams->getCredentials().empty()) {
                            this->_credentials = hlsParams->getCredentials();
                        }

                        if (hlsParams->getScteParameters().getSource() != ScteInputParameters::DEFAULT_SOURCE) {
                            this->_scteParameters = hlsParams->getScteParameters();
                        }
                    }
                }

            private:
                std::size_t         _bandwith;
                Credentials         _credentials;
                ScteInputParameters _scteParameters;
                Authentication      _authentication;
        };

        // MPEG-DASH (CMAF/fMP4 or TS segments) pulled over HTTP(S) by dashdemux2.
        class DashInputTransportParameters : public InputTransportParameters {
            public:
                // dashdemux2 "connection-bitrate" range. The default pins the highest representation.
                static const uint64_t MAX_BANDWIDTH = 4294967295UL;
                static const uint64_t MIN_BANDWIDTH = 1000UL;

            public:
                DashInputTransportParameters() : InputTransportParameters(Protocol::DASH) {
                    this->_bandwidth         = MAX_BANDWIDTH;
                    this->_presentationDelay = 0;
                }

                DashInputTransportParameters(const DashInputTransportParameters *other) : InputTransportParameters(other->getProtocol()) {
                    this->_bandwidth         = other->_bandwidth;
                    this->_presentationDelay = other->_presentationDelay;
                    this->_scteParameters    = other->_scteParameters;
                }

                std::unique_ptr<InputTransportParameters> clone() const override {
                    return std::make_unique<DashInputTransportParameters>(this);
                }

                // Bits per second. The highest representation not above this value is used for the
                // whole session; the demuxer does not switch representations on measured throughput.
                uint64_t getBandwidth() const {
                    return this->_bandwidth;
                }

                void setBandwidth(uint64_t bandwidth) {
                    this->_bandwidth = bandwidth;
                }

                // Milliseconds behind the live edge when the MPD has no suggestedPresentationDelay;
                // 0 keeps the demuxer default (10 s).
                uint64_t getPresentationDelay() const {
                    return this->_presentationDelay;
                }

                void setPresentationDelay(uint64_t delay) {
                    this->_presentationDelay = delay;
                }

                const ScteInputParameters &getScteParameters() const {
                    return this->_scteParameters;
                }

                void setScteParameters(const ScteInputParameters &scteParams) {
                    this->_scteParameters = scteParams;
                }

                void merge(const InputTransportParameters &other) override {
                    auto *dashParams = reinterpret_cast<const DashInputTransportParameters *>(&other);

                    if (dashParams) {
                        if (dashParams->getBandwidth() != MAX_BANDWIDTH) {
                            this->_bandwidth = dashParams->getBandwidth();
                        }

                        if (dashParams->getPresentationDelay() != 0) {
                            this->_presentationDelay = dashParams->getPresentationDelay();
                        }

                        if (dashParams->getScteParameters().getSource() != ScteInputParameters::DEFAULT_SOURCE) {
                            this->_scteParameters = dashParams->getScteParameters();
                        }
                    }
                }

            private:
                uint64_t            _bandwidth;
                uint64_t            _presentationDelay;
                ScteInputParameters _scteParameters;
        };

        class PlaylistTransportInputParameters : public InputTransportParameters {
            public:
                PlaylistTransportInputParameters() : InputTransportParameters(Protocol::PLAYLIST) {
                }

                std::unique_ptr<InputTransportParameters> clone() const override {
                    return std::make_unique<PlaylistTransportInputParameters>();
                }
        };

        class NdiInputTransportParameters: public InputTransportParameters {
            public:
                NdiInputTransportParameters() : InputTransportParameters(Protocol::NDI) {

                }

                std::unique_ptr<InputTransportParameters> clone() const override {
                    return std::make_unique<NdiInputTransportParameters>();
                }

                std::string getNdiName() const {
                    return this->_ndiName;
                }

                void setNdiName(std::string ndiName) {
                    this->_ndiName = ndiName;
                }

                std::string getUrlAddress() const {
                    return this->_urlAddress;
                }

                void setUrlAddress(std::string urlAddress) {
                    this->_urlAddress = urlAddress;
                }

            private:
                std::string _ndiName;
                std::string _urlAddress;
        };

        // An input that reads MXL (Media eXchange Layer) flows written by another process. The input's URI
        // is the domain, "mxl:///run/mxl"; the flows are named here. One flow carries video (v210) and one
        // audio (Float32); either may be left out. See doc/codebase/41 and 42.
        class MxlInputTransportParameters: public InputTransportParameters {
            public:
                MxlInputTransportParameters() : InputTransportParameters(Protocol::MXL) {
                }

                std::unique_ptr<InputTransportParameters> clone() const override {
                    auto ret = std::make_unique<MxlInputTransportParameters>();

                    ret->_videoFlowId = this->_videoFlowId;
                    ret->_audioFlowId = this->_audioFlowId;
                    ret->_dataFlowId  = this->_dataFlowId;
                    ret->_delayMs     = this->_delayMs;

                    return ret;
                }

                const std::string &getVideoFlowId() const {
                    return this->_videoFlowId;
                }

                void setVideoFlowId(const std::string &id) {
                    this->_videoFlowId = id;
                }

                const std::string &getAudioFlowId() const {
                    return this->_audioFlowId;
                }

                void setAudioFlowId(const std::string &id) {
                    this->_audioFlowId = id;
                }

                // A data flow carrying SCTE-35 (the programme output's `mxl.data`, see mxlcarrier.h). Optional; it is read next to the
                // picture and sound flows and its cues go to the channel's SCTE-35 path, as if the source had carried them.
                const std::string &getDataFlowId() const {
                    return this->_dataFlowId;
                }

                void setDataFlowId(const std::string &id) {
                    this->_dataFlowId = id;
                }

                // The flows carry the time at which their pictures and sound were made, and the writer's own
                // processing has delayed them since. The channel plays them this many milliseconds after they
                // arrive: the room it gives the writer for jitter. It is added to the end-to-end delay of the input.
                // The default is the largest movement of lag seen between a Nandi programme output's picture and sound
                // (358 ms, with every core of the host busy; 185 to 260 ms otherwise) plus a margin: see 41-mxl-integration.md
                static constexpr uint32_t DEFAULT_DELAY_MS = 500;
                static constexpr uint32_t MAX_DELAY_MS     = 2000;

                uint32_t getDelayMs() const {
                    return this->_delayMs;
                }

                void setDelayMs(uint32_t delayMs) {
                    this->_delayMs = delayMs;
                }

            private:
                std::string _videoFlowId;
                std::string _audioFlowId;
                std::string _dataFlowId;
                uint32_t    _delayMs = DEFAULT_DELAY_MS;
        };

        // Role of an input inside a channel. PRIMARY is the programme source that is on air by
        // default. SECONDARY is a replacement source that something else may cut to - today the
        // SCTE-35 ad switcher, for the duration of an avail. A channel without an explicit role
        // behaves exactly as before: one PRIMARY input.
        enum class InputRole {
            PRIMARY,
            SECONDARY
        };

        // What an input does while it is not the on-air source.
        //
        //  * DROP  - keep running; the connector's input-selector discards its buffers. Required
        //            for a live source, otherwise returning to it would land behind the live edge.
        //  * PAUSE - freeze the branch and retain its position, so the next time it goes on air it
        //            resumes exactly where it stopped.
        enum class InputOffAirBehavior {
            DROP,
            PAUSE
        };

        class InputParameters {
            public:
                InputParameters() {
                    this->_role = InputRole::PRIMARY;
                }

                virtual ~InputParameters() {
                }

                InputParameters &operator=(const InputParameters &other) {
                    this->_parameters = other._parameters;
                    this->_uris       = other._uris;
                    this->_selector   = other._selector;
                    this->_role       = other._role;
                    this->_offAir     = other._offAir;
                    this->_mxlTap     = other._mxlTap;

                    return *this;
                }

                void addUri(const std::string &uri) {
                    this->_uris.push_back(uri);
                }

                const std::vector<std::string> &getUris() const {
                    return this->_uris;
                }

                void clearUris() {
                    this->_uris.clear();
                }

                void addTransportParameters(std::unique_ptr<InputTransportParameters> &parameters) {
                    this->_parameters.emplace(parameters->getProtocol(), std::move(parameters));
                }

                const InputTransportParameters *getTransportParameters(Protocol protocol) const {
                    auto it = this->_parameters.find(protocol);

                    if (it != this->_parameters.end()) {
                        return it->second.get();
                    }

                    return nullptr;
                }

                const std::map<Protocol, std::shared_ptr<InputTransportParameters>> &getAllTransportParameters() const {
                    return this->_parameters;
                }

                void mergeTransportParameters(const InputTransportParameters &other) {
                    auto it = this->_parameters.find(other.getProtocol());

                    if (it != this->_parameters.end()) {
                        auto *thisParams = it->second.get();

                        thisParams->merge(other);

                    } else {
                        auto protocolParameters = other.clone();

                        this->addTransportParameters(protocolParameters);
                    }
                }

                const StreamSelector &getStreamSelector() const {
                    return this->_selector;
                }

                StreamSelector &getStreamSelector() {
                    return this->_selector;
                }

                InputRole getRole() const {
                    return this->_role;
                }

                void setRole(InputRole role) {
                    this->_role = role;
                }

                // An unset off-air behavior follows the role: a replacement source freezes, a
                // programme source keeps running. Configuration may still override it explicitly.
                InputOffAirBehavior getOffAirBehavior() const {
                    if (this->_offAir.has_value()) {
                        return this->_offAir.value();
                    }

                    return this->_role == InputRole::SECONDARY
                        ? InputOffAirBehavior::PAUSE
                        : InputOffAirBehavior::DROP;
                }

                void setOffAirBehavior(InputOffAirBehavior behavior) {
                    this->_offAir = behavior;
                }

                const MxlTapParameters &getMxlTap() const {
                    return this->_mxlTap;
                }

                void setMxlTap(const MxlTapParameters &tap) {
                    this->_mxlTap = tap;
                }

            private:
                std::vector<std::string> _uris;

                std::map<Protocol, std::shared_ptr<InputTransportParameters>> _parameters;
                StreamSelector _selector;

                InputRole                          _role;
                std::optional<InputOffAirBehavior> _offAir;
                MxlTapParameters                   _mxlTap;
        };

        enum class InputState {
            UNKNOWN,
            CREATED,
            BUFFERING,
            BUFFERED
        };

        class InputTransportStatus {
            public:
                InputTransportStatus(const std::string &id, const std::string &uri, InputState state) : _uri(uri), _id(id) {
                    this->_state = state;
                }

                InputState getState() const {
                    return this->_state;
                }

                const std::string &getUri() const {
                    return this->_uri;
                }

                const std::string &getId() const {
                    return this->_id;
                }

            private:
                InputState  _state;
                std::string _uri;
                std::string _id;
        };

        class InputStatus {
            public:
                InputStatus() {
                }

                const std::vector<InputTransportStatus> &getTransportsStatus() const {
                    return this->_transportsStatus;
                }

                const std::string &getCurrentTransportId() const {
                    return this->_transportCurrent;
                }

                void setCurrentTransportId(const std::string &id) {
                    this->_transportCurrent = id;
                }

                InputTransportStatus &addTransport(const std::string &id, const std::string &uri, InputState state) {
                    this->_transportsStatus.push_back(InputTransportStatus(id, uri, state));

                    return *this->_transportsStatus.rbegin();
                }

            private:
                std::vector<InputTransportStatus> _transportsStatus;
                std::string                       _transportCurrent;
        };

        class Input {
            UTPL_CLASS_INTERFACE(Input) {}

            public:
                using ProbeCallback          = std::function<void(bool)>;
                using ErrorCallback          = std::function<bool(const std::string &message)>;
                using StateChangedCallback   = std::function<void(InputState newState)>;
                using StreamsChangedCallback = std::function<void(const std::vector<std::shared_ptr<InputStream>> &streams)>;

            public:
                virtual void prepare(const InputParameters &parameters) = 0;
                virtual void probe(ProbeCallback callback = {}) = 0;
                virtual void cleanup() = 0;

                virtual const std::vector<std::shared_ptr<InputStream>> &getStreams() const = 0;
                virtual const InputParameters &getInputParameters() const = 0;

                virtual void onScteData(ScteDataCallback callback) = 0;
                virtual void onStateChanged(StateChangedCallback callback) = 0;
                virtual void onStreamsChanged(StreamsChangedCallback callback) = 0;
                virtual void onError(ErrorCallback callback) = 0;

                virtual pipeline::ElementPtr getNode() = 0;
                
                virtual bool isLive() const = 0;
                virtual Protocol getProtocol() const = 0;
                virtual std::string getName() const = 0;
                virtual GstClockTime getDuration() const = 0;

                virtual const std::string &getId() const = 0;

                virtual InputStatus getStatus() = 0;

                virtual Error setCurrentStream(const std::string &streamId) = 0;

                // Playlist inputs may atomically accept an ordered, durable snapshot. Other
                // input types deliberately remain unaffected.
                virtual Error applyPlaylistSnapshot(
                    const std::vector<PlaylistSnapshotItem> &items,
                    uint64_t revision
                ) {
                    return Error::NOT_SUPPORTED;
                }

                // Freezes or resumes an input that is not on air, so that the next time it is
                // selected it continues from where it stopped instead of from wherever the wall
                // clock has moved to. Only inputs whose source can be suspended implement this;
                // a live input must keep running and let the connector drop its buffers, which is
                // why the default is NOT_SUPPORTED rather than a silent no-op.
                virtual Error setPlaybackActive(bool active) {
                    return Error::NOT_SUPPORTED;
                }
        };
    }
}

#endif /* __UTPL_TRANSCODER_INPUT_H__ */
