#ifndef __UTPL_TRANSCODER_OUTPUT_PARAMETERS_H__
#define __UTPL_TRANSCODER_OUTPUT_PARAMETERS_H__

#include <memory>
#include <map>
#include "utpl/common/debug.h"

#include "utpl/transcoder/stream.h"
#include "utpl/transcoder/types.h"

#include "utpl/hls/types.h"
#include "utpl/hls/streamer/uploader.h"

namespace utpl {
    namespace transcoder {
        class OutputTransportParameters {
            public:
                OutputTransportParameters() {
                    this->_protocol = Protocol::UNKNOWN;
                    this->_liveSource = false;
                }
                
                OutputTransportParameters(Protocol protocol) {
                    this->_protocol = protocol;
                }
                
                virtual ~OutputTransportParameters() {
                }
                
                virtual Protocol getProtocol() const {
                    return this->_protocol;
                }

            private:
                Protocol _protocol;
                bool     _liveSource;
        };

        class SrtOutputTransportParameters : public OutputTransportParameters {
            public:
                static const utpl::Timestamp DEFAULT_LATENCY = 125;
                static const SrtProtocolMode DEFAULT_MODE    = SrtProtocolMode::UNKNOWN;

            public:
                SrtOutputTransportParameters() : SrtOutputTransportParameters(SrtProtocolMode::UNKNOWN, 0, 0) {
                }

                SrtOutputTransportParameters(SrtProtocolMode mode, utpl::Timestamp latency, size_t bufferSize) : OutputTransportParameters(Protocol::SRT) {
                    this->_latency    = latency;
                    this->_mode       = mode;
                    this->_bufferSize = bufferSize;
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

            private:
                utpl::Timestamp _latency;
                SrtProtocolMode _mode;
                size_t          _bufferSize;
        };

        class S3OutputTransportParameters : public OutputTransportParameters {
            public:

                S3OutputTransportParameters() : OutputTransportParameters(Protocol::S3) {
                }

                void setBucket(std::string bucket) {
                    this->_bucket =  bucket;
                }

                const std::string getBucket() {
                    return this->_bucket;
                }

                void setAccessKey(std::string accessKey) {
                    this->_accessKey = accessKey;
                }

                const std::string getAccessKey() {
                    return this->_accessKey;
                }

                void setSecretAccessKey(std::string secretAccessKey) {
                    this->_secretAccessKey = secretAccessKey;
                }

                const std::string getSecretAccessKey() {
                    return this->_secretAccessKey;
                }

                void setRegion(std::string region) {
                    this->_region = region;
                }

                std::string getRegion() {
                    return this->_region;
                }

                void setKeyPrefix(std::string keyPrefix) {
                    this->_keyPrefix = keyPrefix;
                }

                std::string getKeyPrefix() {
                    return this->_keyPrefix;
                }

            private:
                std::string _bucket;
                std::string _accessKey;
                std::string _secretAccessKey;
                std::string _region;
                std::string _keyPrefix;
        };

        class UdpOutputTransportParameters : public OutputTransportParameters {
            public:
                class MulticastParameters {
                    public:
                        static const uint8_t DEFAULT_TTL = 1;

                    public:
                        MulticastParameters() {
                            this->_ttl = DEFAULT_TTL;
                        }

                        uint8_t getTtl() const {
                            return this->_ttl;
                        }

                        void setTtl(uint8_t ttl) {
                            this->_ttl = ttl;
                        }

                        const std::string getInterface() const {
                            return this->_interface;
                        }

                        void setInterface(const std::string &interface) {
                            this->_interface = interface;
                        }

                    private:
                        std::string _interface;
                        uint8_t     _ttl;
                };

                class UnicastParameters {
                    public:
                        static const uint8_t DEFAULT_TTL = 64;

                    public:
                        UnicastParameters() {
                            this->_ttl = DEFAULT_TTL;
                        }

                        uint8_t getTtl() const {
                            return this->_ttl;
                        }

                        void setTtl(uint8_t ttl) {
                            this->_ttl = ttl;
                        }

                    private:
                        uint8_t _ttl;
                };

            public:
                UdpOutputTransportParameters() : OutputTransportParameters(Protocol::UDP) {
                }

                const MulticastParameters &getMulticastParameters() const {
                    return this->_multicastParameters;
                }

                void setMulticastParameters(const MulticastParameters &parameters) {
                    this->_multicastParameters = parameters;
                }

                const UnicastParameters &getUnicastParameters() const {
                    return this->_unicastParameters;
                }

                void setUnicastParameters(const UnicastParameters &parameters) {
                    this->_unicastParameters = parameters;
                }

            private:
                UnicastParameters   _unicastParameters;
                MulticastParameters _multicastParameters;
        };

        class FileOutputTransportParameters : public OutputTransportParameters {
            public:
                FileOutputTransportParameters() : OutputTransportParameters(Protocol::FILE) {
                }
        };

        class HlsOutputTransportParameters : public OutputTransportParameters {
            public:
                enum class TransferType {
                    HTTP_PUT,
                    HTTP_DAV
                };

                enum class TransferFraming {
                    CHUNKED,
                    FIXED
                };

                static const int             MIN_SEGMENTS = 1;
                static const utpl::Timestamp MIN_SEGMENT_DURATION = 1000;

                static const int             DEFAULT_SEGMENTS         = 10;
                static const utpl::Timestamp DEFAULT_SEGMENT_DURATION = 10 * 1000;

                static const utpl::Timestamp DEFAULT_TIMEOUT        = 2000;
                static const utpl::Timestamp DEFAULT_RETRY_INTERVAL = 1000;
                static const size_t          DEFAULT_RETRY_COUNT    = hls::Uploader::RETRY_NONE;

            public:
                HlsOutputTransportParameters() : OutputTransportParameters(Protocol::HLS) {
                    this->_vodMode            = false;
                    this->_maxSegments        = DEFAULT_SEGMENTS;
                    this->_maxSegmentDuration = DEFAULT_SEGMENT_DURATION;
                    this->_transferType       = TransferType::HTTP_PUT;
                    this->_framingMode        = TransferFraming::FIXED;
                    this->_singleDir          = true;
                    this->_timeout            = DEFAULT_TIMEOUT;
                    this->_retryCount         = DEFAULT_RETRY_COUNT;
                    this->_retryInterval      = DEFAULT_RETRY_INTERVAL;
                    this->_authentication     = Authentication::BASIC;
                }

                void setMaxSegments(int maxSegments) {
                    this->_maxSegments = maxSegments;
                }

                int getMaxSegments() const {
                    return this->_maxSegments;
                }

                void setMaxSegmentDuration(utpl::Timestamp maxSegmentDuration) {
                    this->_maxSegmentDuration = maxSegmentDuration;
                }

                utpl::Timestamp getMaxSegmentDuration() const {
                    return this->_maxSegmentDuration;
                }

                bool isVodMode() const {
                    return this->_vodMode;
                }

                void setVodMode(bool vod) {
                    this->_vodMode = vod;
                }

                TransferType getTransferType() const {
                    return this->_transferType;
                }

                void setTransferType(TransferType type) {
                    this->_transferType = type;
                }

                TransferFraming getTransferFraming() const {
                    return this->_framingMode;
                }

                void setTransferFraming(TransferFraming mode) {
                    this->_framingMode = mode;
                }

                bool isSingleDir() const {
                    return this->_singleDir;
                }

                void setSingleDir(bool single) {
                    this->_singleDir = single;
                }

                const Credentials &getCredentials() const {
                    return this->_credentials;
                }

                void setCredentials(const Credentials &cred) {
                    this->_credentials = cred;
                }

                Timestamp getTimeout() const {
                    return this->_timeout;
                }

                void setTimeout(Timestamp timeout) {
                    this->_timeout = timeout;
                }

                size_t getRetryCount() const {
                    return this->_retryCount;
                }

                void setRetryCount(size_t retryCount) {
                    this->_retryCount = retryCount;
                }

                Timestamp getRetryInterval() const {
                    return this->_retryInterval;
                }

                void setRetryInterval(Timestamp interval) {
                    this->_retryInterval = interval;
                }

                const std::vector<hls::ScteMarker> &getAdMarkers() const {
                    return this->_adMarkers;
                }

                void addAdMarker(hls::ScteMarker marker) {
                    this->_adMarkers.push_back(marker);
                }

                void clearMarkers() {
                    this->_adMarkers.clear();
                }

                Authentication getAuthentication() const {
                    return this->_authentication;
                }

                void setAuthentication(Authentication auth) {
                    this->_authentication = auth;
                }

                const std::string &getBaseFileName() const {
                    return this->_baseFileName;
                }

                void setBaseFileName(const std::string &baseFileName) {
                    this->_baseFileName = baseFileName;
                }

            private:
                int              _maxSegments;
                utpl::Timestamp  _maxSegmentDuration;
                bool             _vodMode;
                TransferType     _transferType;
                TransferFraming  _framingMode;
                bool             _singleDir;
                Credentials      _credentials;
                Authentication   _authentication;
                std::string      _baseFileName;

                Timestamp _timeout;
                size_t    _retryCount;
                Timestamp _retryInterval;

                std::vector<hls::ScteMarker> _adMarkers;
        };

        /**
         * CMAF DASH output (dash+file: / dash+http(s): URIs), packaged by dashsink2.
         *
         * Upload settings mirror HLS because both go through the same hls::Uploader classes.
         * Durations are in milliseconds.
         */
        class DashOutputTransportParameters : public OutputTransportParameters {
            public:
                using TransferType    = HlsOutputTransportParameters::TransferType;
                using TransferFraming = HlsOutputTransportParameters::TransferFraming;

                static const int             MIN_SEGMENTS             = 2;
                static const utpl::Timestamp MIN_SEGMENT_DURATION     = 500;
                static const int             DEFAULT_SEGMENTS         = 30;
                static const utpl::Timestamp DEFAULT_SEGMENT_DURATION = 2000;

            public:
                DashOutputTransportParameters() : OutputTransportParameters(Protocol::DASH) {
                    this->_segmentDuration     = DEFAULT_SEGMENT_DURATION;
                    this->_windowSegments      = DEFAULT_SEGMENTS;
                    this->_vodMode             = false;
                    this->_presentationDelay   = 0;
                    this->_minBufferTime       = 0;
                    this->_minimumUpdatePeriod = 0;
                    this->_transferType        = TransferType::HTTP_PUT;
                    this->_framingMode         = TransferFraming::FIXED;
                    this->_authentication      = Authentication::BASIC;
                    this->_timeout             = HlsOutputTransportParameters::DEFAULT_TIMEOUT;
                    this->_retryCount          = HlsOutputTransportParameters::DEFAULT_RETRY_COUNT;
                    this->_retryInterval       = HlsOutputTransportParameters::DEFAULT_RETRY_INTERVAL;
                }

                // Target segment duration. Every stream is cut on the same running-time grid.
                utpl::Timestamp getSegmentDuration() const { return this->_segmentDuration; }
                void setSegmentDuration(utpl::Timestamp duration) { this->_segmentDuration = duration; }

                // Segments kept on the origin and advertised as timeShiftBufferDepth (live only).
                int getWindowSegments() const { return this->_windowSegments; }
                void setWindowSegments(int segments) { this->_windowSegments = segments; }

                // VOD: keep every segment and publish a static MPD when the output stops.
                bool isVodMode() const { return this->_vodMode; }
                void setVodMode(bool vod) { this->_vodMode = vod; }

                // suggestedPresentationDelay; 0 = three segment durations.
                utpl::Timestamp getPresentationDelay() const { return this->_presentationDelay; }
                void setPresentationDelay(utpl::Timestamp delay) { this->_presentationDelay = delay; }

                // minBufferTime; 0 = two segment durations.
                utpl::Timestamp getMinBufferTime() const { return this->_minBufferTime; }
                void setMinBufferTime(utpl::Timestamp time) { this->_minBufferTime = time; }

                // minimumUpdatePeriod; 0 = one segment duration.
                utpl::Timestamp getMinimumUpdatePeriod() const { return this->_minimumUpdatePeriod; }
                void setMinimumUpdatePeriod(utpl::Timestamp period) { this->_minimumUpdatePeriod = period; }

                // UTCTiming http-xsdate source; empty leaves UTCTiming out.
                const std::string &getUtcTimingUrl() const { return this->_utcTimingUrl; }
                void setUtcTimingUrl(const std::string &url) { this->_utcTimingUrl = url; }

                // Manifest is <baseFileName>.mpd (manifest.mpd when empty); objects are prefixed with it.
                const std::string &getBaseFileName() const { return this->_baseFileName; }
                void setBaseFileName(const std::string &name) { this->_baseFileName = name; }

                TransferType getTransferType() const { return this->_transferType; }
                void setTransferType(TransferType type) { this->_transferType = type; }

                TransferFraming getTransferFraming() const { return this->_framingMode; }
                void setTransferFraming(TransferFraming mode) { this->_framingMode = mode; }

                const Credentials &getCredentials() const { return this->_credentials; }
                void setCredentials(const Credentials &cred) { this->_credentials = cred; }

                Authentication getAuthentication() const { return this->_authentication; }
                void setAuthentication(Authentication auth) { this->_authentication = auth; }

                Timestamp getTimeout() const { return this->_timeout; }
                void setTimeout(Timestamp timeout) { this->_timeout = timeout; }

                size_t getRetryCount() const { return this->_retryCount; }
                void setRetryCount(size_t retryCount) { this->_retryCount = retryCount; }

                Timestamp getRetryInterval() const { return this->_retryInterval; }
                void setRetryInterval(Timestamp interval) { this->_retryInterval = interval; }

            private:
                utpl::Timestamp _segmentDuration;
                int             _windowSegments;
                bool            _vodMode;
                utpl::Timestamp _presentationDelay;
                utpl::Timestamp _minBufferTime;
                utpl::Timestamp _minimumUpdatePeriod;
                std::string     _utcTimingUrl;
                std::string     _baseFileName;
                TransferType    _transferType;
                TransferFraming _framingMode;
                Credentials     _credentials;
                Authentication  _authentication;
                Timestamp       _timeout;
                size_t          _retryCount;
                Timestamp       _retryInterval;
        };

        class OutputTransport {
            public:
                void setUri(const std::string &uri) {
                    this->_uri = uri;
                }
                
                const std::string &getUri() const {
                    return this->_uri;
                }
                
                void addTransportParameters(std::unique_ptr<OutputTransportParameters> &parameters) {
                    this->_parameters.emplace(parameters->getProtocol(), std::move(parameters));
                }
                
                const OutputTransportParameters *getTransportParameters(Protocol protocol) const {
                    auto it = this->_parameters.find(protocol);
                    
                    if (it != this->_parameters.end()) {
                        return it->second.get();
                    }

                    return nullptr;
                }
                
            private:
                std::string _uri;
                
                std::map<Protocol, std::shared_ptr<OutputTransportParameters>> _parameters;
        };
        
        enum class OutputType {
            UNKNOWN,

            SINGLE,
            GROUP
        };

        class OutputParameters {
            public:
                OutputParameters() {
                    this->_container = Container::UNKNOWN;
                    this->_type      = OutputType::UNKNOWN;
                }
                
                void setName(const std::string &name) {
                    this->_name = name;
                }

                const std::string &getName() const {
                    return this->_name;
                }

                void setType(OutputType type) {
                    this->_type = type;
                }

                OutputType getType() const {
                    return this->_type;
                }

                void setContainer(Container container) {
                    this->_container = container;
                }
                
                Container getContainer() const {
                    return this->_container;
                }
                
                const ScteOutputParameters &getScteParameters() const {
                    return this->_scteParameters;
                }
                
                void setScteParameters(const ScteOutputParameters &params) {
                    this->_scteParameters = params;
                }
                
                void addStream(const StreamParameters &parameters) {
                    this->_streams.push_back(parameters);
                }
                
                const std::vector<StreamParameters> &getStreams() const {
                    return this->_streams;
                }
                
                bool hasStreams() const {
                    return ! this->_streams.empty();
                }
                
                void addTransport(const OutputTransport &transport) {
                    this->_transports.push_back(transport);
                }
                
                const std::vector<OutputTransport> &getTransports() const {
                    return this->_transports;
                }

                void clearTransports() {
                    this->_transports.clear();
                }
                
                void setLiveSource(bool live) {
                    this->_liveSource = live;
                }
                
                bool hasLiveSource() const {
                    return this->_liveSource;
                }

                const std::string &getGroupName() const {
                    return this->_groupName;
                }

                void setGroupName(const std::string &groupName) {
                    this->_groupName = groupName;
                }
                
            private:
                std::string                   _name;
                OutputType                    _type;
                Container                     _container;
                ScteOutputParameters          _scteParameters;
                std::vector<StreamParameters> _streams;
                std::vector<OutputTransport>  _transports;
                bool                          _liveSource;
                std::string                   _groupName;
        };
    }
}

#endif /* __UTPL_TRANSCODER_OUTPUT_PARAMETERS_H__ */