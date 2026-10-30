#ifndef __UTPL_TRANSCODER_STREAM_H__
#define __UTPL_TRANSCODER_STREAM_H__

#include <variant>
#include <map>

#include "utpl/common/types.h"
#include "utpl/common/utils.h"
#include "utpl/common/utils/class.h"

#include "utpl/pipeline/pad.h"
#include "utpl/pipeline/element.h"

#include "utpl/transcoder/types.h"

namespace utpl {
    namespace transcoder {
        class GstEncoderParameters {
            public:
                GstEncoderParameters() {
                }

                GstEncoderParameters(const std::string &name) : _encoderName(name) {
                }

                const std::string &getName() const {
                    return this->_encoderName;
                }

                void addProperty(const std::string &name, const std::string &value) {
                    this->_properties[name] = value;
                }

                const std::map<std::string, std::string> &getProperties() const {
                    return this->_properties;
                }

                void addCapability(const std::string &name, const std::string &value) {
                    this->_caps[name] = value;
                }

                const std::map<std::string, std::string> &getCapabilities() const {
                    return this->_caps;
                }

                bool operator==(const GstEncoderParameters &other) const {
                    if (this == &other) {
                        return true;
                    }

                    // Values count, not just names: two outputs asking for different bitrates must
                    // not share one encoder.
                    return
                        this->_encoderName == other._encoderName &&
                        this->_properties  == other._properties  &&
                        this->_caps        == other._caps;
                }

                bool operator!=(const GstEncoderParameters &other) const {
                    return ! (*this == other);
                }

            private:
                std::string _encoderName;
                std::map<std::string, std::string> _properties;
                std::map<std::string, std::string> _caps;
        };

        class StreamParameters {
            public:
                StreamParameters() {
                    this->_codec = Codec::UNKNOWN;
                    this->_pid   = PID_NULL;
                }
                
                void setAudioParameters(const AudioParameters &params) {
                    this->_dataParameters = params;
                }

                void setVideoParameters(const VideoParameters &params) {
                    this->_dataParameters = params;
                }

                void setCaptionParameters(const CaptionParameters &params) {
                    this->_dataParameters = params;
                }

                void setLinkParameters(const LinkParameters &params) {
                    this->_dataParameters = params;
                }

                bool hasAudioParameters() const {
                    return NOT_NULL(std::get_if<AudioParameters>(&this->_dataParameters));
                }

                bool hasVideoParameters() const {
                    return NOT_NULL(std::get_if<VideoParameters>(&this->_dataParameters));
                }

                bool hasCaptionParameters() const {
                    return NOT_NULL(std::get_if<CaptionParameters>(&this->_dataParameters));
                }

                bool hasLinkParameters() const {
                    return NOT_NULL(std::get_if<LinkParameters>(&this->_dataParameters));
                }

                const AudioParameters &getAudioParameters() const {
                    return std::get<AudioParameters>(this->_dataParameters);
                }

                AudioParameters &getAudioParameters() {
                    return std::get<AudioParameters>(this->_dataParameters);
                }

                const VideoParameters &getVideoParameters() const {
                    return std::get<VideoParameters>(this->_dataParameters);
                }

                VideoParameters &getVideoParameters() {
                    return std::get<VideoParameters>(this->_dataParameters);
                }

                const CaptionParameters &getCaptionParameters() const {
                    return std::get<CaptionParameters>(this->_dataParameters);
                }

                CaptionParameters &getCaptionParameters() {
                    return std::get<CaptionParameters>(this->_dataParameters);
                }

                LinkParameters &getLinkParameters() {
                    return std::get<LinkParameters>(this->_dataParameters);
                }

                const LinkParameters &getLinkParameters() const {
                    return std::get<LinkParameters>(this->_dataParameters);
                }

                Codec getCodec() const {
                    return this->_codec;
                }

                void setCodec(Codec codec) {
                    this->_codec = codec;
                }

                bool isPassthrough() const {
                    return this->_codec == Codec::PASSTHROUGH;
                }

                const GstEncoderParameters &getEncoderParameters() const {
                    return this->_encoderParameters;
                }

                void setEncoderParameters(const GstEncoderParameters &params) {
                    this->_encoderParameters = params;
                }

                // MPEG-TS PID the stream is muxed on. PID_NULL lets the muxer number it.
                // Not part of equality: it does not change what is encoded.
                Pid getPid() const {
                    return this->_pid;
                }

                void setPid(Pid pid) {
                    this->_pid = pid;
                }

                std::string toString() const;

                inline bool operator==(const StreamParameters &other) const {
                    if (
                        (other.hasAudioParameters()   && ! this->hasAudioParameters())   ||
                        (other.hasVideoParameters()   && ! this->hasVideoParameters())   ||
                        (other.hasCaptionParameters() && ! this->hasCaptionParameters()) ||
                        (other.hasLinkParameters()    && ! this->hasLinkParameters())
                    ) {
                        return false;
                    }

                    if (this->getCodec() != other.getCodec()) {
                        return false;
                    }

                    bool dataEqual = true;

                    if (other.hasAudioParameters()) {
                        dataEqual = this->getAudioParameters() == other.getAudioParameters();

                    } else if (other.hasVideoParameters()) {
                        dataEqual = this->getVideoParameters() == other.getVideoParameters();

                    } else if (other.hasCaptionParameters()) {
                        dataEqual = this->getCaptionParameters() == other.getCaptionParameters();

                    } else if (other.hasLinkParameters()) {
                        dataEqual = this->getLinkParameters() == other.getLinkParameters();
                    }

                    // Flowlines are shared between outputs whose parameters compare equal, so the
                    // encoder block has to take part for audio and video too.
                    return dataEqual && this->_encoderParameters == other.getEncoderParameters();
                }

                inline bool operator!=(const StreamParameters &other) const {
                    return ! (*this == other);
                }

                static StreamParameters fromCaps(const pipeline::Caps &caps);

            private:
                // INFO: a default-constructed variant holds a value of its first alternative,
                std::variant<std::monostate, AudioParameters, VideoParameters, CaptionParameters, LinkParameters> _dataParameters;
                GstEncoderParameters _encoderParameters;
                Codec _codec;
                Pid   _pid;
        };

        class Stream {
            UTPL_CLASS_INTERFACE(Stream) {};

            public:                
                virtual const StreamParameters &getStreamParameters() const = 0;

                virtual void link()   = 0;
                virtual void unlink() = 0;
        };

        class InputStream : public Stream {
            public:
                virtual pipeline::PadPtr requestPad(bool passthrough)     = 0;
                virtual void             releasePad(pipeline::PadPtr pad) = 0;

                // FIXME: This API should be removed. Analyze all usage points and replace them with appropriate APIs on related elements.
                virtual pipeline::PadPtr peekPad(bool passthrough) = 0;


                // FIXME: 
                virtual void synchronizeClock(pipeline::ElementPtr &src) {
                    auto clock = gst_element_get_clock((*src)->get());
                    if (clock) {
                        GstClockTime now = gst_clock_get_time(clock);
                        GstClockTime baseTime = gst_element_get_base_time(GST_ELEMENT_CAST((*src)->get()));

                        if (now > baseTime) {
                            now -= baseTime;

                        } else {
                            now = 0;
                        }

                        gst_object_unref (clock);

                        this->synchronizeClock(now);
                    }
                }
                // FIXME: 
                virtual void synchronizeClock(GstClockTime runningTime) {
                    // This is currently used only by the overlay input, and it should be refactored or implemented in a better way.
                }
        };

        class OutputStream : public Stream {
            public:
                virtual pipeline::PadPtr getPad() = 0;
        };

        class StreamSelector {
            public:
                static const int PROGRAM_NUMBER_UNDEFINED = -1;

            public:
                StreamSelector() {
                    this->_programNumber = PROGRAM_NUMBER_UNDEFINED;
                }

                bool fixedProgram() const {
                    return this->_programNumber != PROGRAM_NUMBER_UNDEFINED;
                }

                int getProgram() const {
                    return this->_programNumber;
                }

                void setProgramNumber(int programNumber) {
                    this->_programNumber = programNumber;
                }

            private:
                int _programNumber;
        };
    }
}

#endif /* __UTPL_TRANSCODER_STREAM_H__ */