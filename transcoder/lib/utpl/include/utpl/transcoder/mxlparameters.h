#ifndef __UTPL_TRANSCODER_MXLPARAMETERS_H__
#define __UTPL_TRANSCODER_MXLPARAMETERS_H__

#include <string>

namespace utpl {
    namespace transcoder {
        // Optional copy of an input's decoded media into MXL (Media eXchange Layer) flows, so other
        // processes can read the clean source. A tap never changes what the channel does: if it
        // cannot run, it stops itself and the channel carries on. See doc/codebase/41 and 42.
        //
        // The video flow carries the first decoded video stream as v210 at the source size and
        // frame rate; the audio flow carries the first decoded audio stream as Float32 at 48 kHz.
        class MxlTapParameters {
            public:
                static const int DEFAULT_AUDIO_CHANNELS = 2;
                static const int MAX_AUDIO_CHANNELS     = 8; // what mxlsink accepts

                MxlTapParameters() {
                    this->_audioChannels = DEFAULT_AUDIO_CHANNELS;
                }

                // A tap with no domain or no flow does nothing.
                bool isEnabled() const {
                    return ! this->_domain.empty() && (! this->_videoFlowId.empty() || ! this->_audioFlowId.empty());
                }

                const std::string &getDomain() const {
                    return this->_domain;
                }

                void setDomain(const std::string &domain) {
                    this->_domain = domain;
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

                // An output only: an MXL data flow carrying the SCTE-35 sections of the programme (ST 291 ancillary data, one grain
                // per video frame), written from the video flow's own side pipeline. Needs a video flow.
                const std::string &getDataFlowId() const {
                    return this->_dataFlowId;
                }

                void setDataFlowId(const std::string &id) {
                    this->_dataFlowId = id;
                }

                int getAudioChannels() const {
                    return this->_audioChannels;
                }

                void setAudioChannels(int channels) {
                    this->_audioChannels = channels;
                }

                // A flow id is a UUID in its canonical text form (8-4-4-4-12 hexadecimal digits).
                static bool isValidFlowId(const std::string &id) {
                    if (id.size() != 36) {
                        return false;
                    }

                    for (size_t i = 0; i < id.size(); i++) {
                        const char c = id[i];

                        if (i == 8 || i == 13 || i == 18 || i == 23) {
                            if (c != '-') {
                                return false;
                            }

                        } else if (! ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                            return false;
                        }
                    }

                    return true;
                }

            private:
                std::string _domain;
                std::string _videoFlowId;
                std::string _audioFlowId;
                std::string _dataFlowId;
                int         _audioChannels;
        };
    }
}

#endif /* __UTPL_TRANSCODER_MXLPARAMETERS_H__ */
