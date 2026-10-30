#ifndef __UTPL_TRANSCODER_TYPES_H__
#define __UTPL_TRANSCODER_TYPES_H__

#include <functional>
#include <variant>

#include <gst/gst.h>
#include <gst/mpegts/gstmpegtssection.h>
#include <gst/mpegts/gst-scte-section.h>

#include "utpl/common/types.h"
#include "utpl/common/utils.h"
#include "utpl/common/time.h"

namespace utpl {
    namespace transcoder {
        #define ORIGINAL_WINDOW      utpl::Rectangle({ -1, -1, }, { -1, -1 })
        #define ORIGINAL_WINDOW_NORM utpl::NormRect()

        enum class SrtProtocolMode {
            UNKNOWN,

            CALLER,
            LISTENER
        };

        // How the source's channel layout is determined for the Dolby output mixer
        enum class AudioInputLayout {
            // The layout the source declares (its header / Dolby metadata)
            METADATA,
            // Determined from the audio itself: stereo carried in a wider declared layout (silent
            // center/LFE/surrounds) is upmixed, real surround content is passed through
            AUTO,
            // Always stereo: only the front left/right channels are used and upmixed
            STEREO
        };

        class AudioParameters {
            private:
                static const int DEFAULT_BITRATE  = 0;
                static const int DEFAULT_CHANNELS = 0;
                static const int DEFAULT_RATE     = 0;

            public:
                AudioParameters() {
                    this->_bitrate  = DEFAULT_BITRATE;
                    this->_channels = DEFAULT_CHANNELS;
                    this->_rate     = DEFAULT_RATE;

                    this->_inputLayout = AudioInputLayout::METADATA;
                }

                int getBitrate() const {
                    return this->_bitrate;
                }

                int getChannels() const {
                    return this->_channels;
                }

                int getRate() const {
                    return this->_rate;
                }

                AudioInputLayout getInputLayout() const {
                    return this->_inputLayout;
                }

                // The source carries stereo content in a wider layout (e.g. 5.1 with silent
                // center/LFE/surrounds), so only its front left/right channels are used.
                bool isStereoSource() const {
                    return this->_inputLayout == AudioInputLayout::STEREO;
                }

                void setBitrate(int bitrate) {
                    this->_bitrate = bitrate;
                }

                void setChannels(int channels) {
                    this->_channels = channels;
                }

                void setRate(int rate) {
                    this->_rate = rate;
                }

                void setInputLayout(AudioInputLayout inputLayout) {
                    this->_inputLayout = inputLayout;
                }

                void setStereoSource(bool stereoSource) {
                    this->_inputLayout = stereoSource ? AudioInputLayout::STEREO : AudioInputLayout::METADATA;
                }

                void apply(const AudioParameters &other) {
                    if (this->_bitrate == DEFAULT_BITRATE) {
                        this->_bitrate = other._bitrate;
                    }

                    if (this->_channels == DEFAULT_CHANNELS) {
                        this->_channels = other._channels;
                    }

                    if (this->_rate == DEFAULT_RATE) {
                        this->_rate = other._rate;
                    }
                }

                bool operator==(const AudioParameters &other) const { 
                    if (this == &other) {
                        return true;
                    }
                    
                    return \
                        this->_bitrate  == other._bitrate  &&
                        this->_channels == other._channels &&
                        this->_rate     == other._rate     &&
                        this->_inputLayout == other._inputLayout;
                }

                bool operator!=(const AudioParameters &other) const {
                    return ! (*this == other);
                }

            private:
                int _bitrate;
                int _channels;
                AudioInputLayout _inputLayout;
                int _rate;
        };

        class VideoParameters {
            private:
                static const int         DEFAULT_WIDTH        = 0;
                static const int         DEFAULT_HEIGHT       = 0;
                static const Interlace   DEFAULT_INTERLACE    = Interlace::UNKNOWN;
                static const FieldOrder  DEFAULT_FIELD_ORDER  = FieldOrder::TOP_FIELD_FIRST;
                static const PixelFormat DEFAULT_PIXEL_FORMAT = PixelFormat::I420;

            public:
                VideoParameters() {
                    this->_width       = DEFAULT_WIDTH;
                    this->_height      = DEFAULT_HEIGHT;
                    this->_interlace   = DEFAULT_INTERLACE;
                    this->_fieldOrder  = DEFAULT_FIELD_ORDER;
                    this->_pixelFormat = DEFAULT_PIXEL_FORMAT;
                }

                void setWidth(int width) {
                    this->_width = width;
                }

                int getWidth() const {
                    return this->_width;
                }

                void setHeight(int height) {
                    this->_height = height;
                }

                int getHeight() const {
                    return this->_height;
                }

                void setFramerate(const Fraction &framerate) {
                    this->_framerate = framerate;
                }

                const Fraction &getFramerate() const {
                    return this->_framerate;
                }

                void setInterlace(Interlace interlace) {
                    this->_interlace = interlace;
                }

                Interlace getInterlace() const {
                    return this->_interlace;
                }

                void setFieldOrder(FieldOrder fieldOrder) {
                    this->_fieldOrder = fieldOrder;
                }

                FieldOrder getFieldOrder() const {
                    return this->_fieldOrder;
                }

                void setPixelFormat(PixelFormat format) {
                    this->_pixelFormat = format;
                }

                PixelFormat getPixelFormat() const {
                    return this->_pixelFormat;
                }

                void apply(const VideoParameters &other) {
                    if (this->_width == DEFAULT_WIDTH) {
                        this->_width = other._width;
                    }

                    if (this->_height == DEFAULT_HEIGHT) {
                        this->_height = other._height;
                    }

                    if (! this->_framerate) {
                        this->_framerate = other._framerate;
                    }

                    // Interlace is deliberately not inherited: the connector deinterlaces every input,
                    // so an interlaced source must not make an output interlaced unless configured.

                    if (this->_pixelFormat == DEFAULT_PIXEL_FORMAT) {
                        this->_pixelFormat = other._pixelFormat;
                    }
                }

                bool operator==(const VideoParameters &other) const {
                    return \
                        this->_width       == other.getWidth()     &&
                        this->_height      == other.getHeight()    &&
                        this->_framerate   == other.getFramerate() &&
                        this->_interlace   == other.getInterlace()  &&
                        this->_fieldOrder  == other.getFieldOrder() &&
                        this->_pixelFormat == other.getPixelFormat();
                }

            private:
                int         _width;
                int         _height;
                Fraction    _framerate;
                Interlace   _interlace;
                FieldOrder  _fieldOrder;
                PixelFormat _pixelFormat;
                
        };

        class OutputLinkParameters {
            public:
                OutputLinkParameters() {
                }

                void setOutputName(const std::string &name) {
                    this->_name = name;
                }

                const std::string &getOutputName() const {
                    return this->_name;
                }

                void setOutputNameModifier(const std::string &mod) {
                    this->_modifier = mod;
                }

                const std::string &getOutputNameModifier() const {
                    return this->_modifier;
                }

                bool operator==(const OutputLinkParameters &other) const {
                    if (this == &other) {
                        return true;
                    }

                    return \
                        (this->_name     == other._name) &&
                        (this->_modifier == other._modifier);
                }

            private:
                std::string _name;
                std::string _modifier;
        };

        class LinkParameters {
            public:
                LinkParameters() {
                }

                bool hasOutputParameters() const {
                    return NOT_NULL(std::get_if<OutputLinkParameters>(&this->_dataParameters));
                }

                const OutputLinkParameters &getOutputParameters() const {
                    return std::get<OutputLinkParameters>(this->_dataParameters);
                }

                OutputLinkParameters &getOutputParameters() {
                    return std::get<OutputLinkParameters>(this->_dataParameters);
                }

                void setOutputParameters(const OutputLinkParameters &params) {
                    this->_dataParameters = params;
                }

                bool operator==(const LinkParameters &other) const {
                    if (other.hasOutputParameters() && ! this->hasOutputParameters()) {
                        return false;
                    }

                    if (this->hasOutputParameters()) {
                        return this->getOutputParameters() == other.getOutputParameters();
                    }

                    return true;
                }

            private:
                std::variant<std::monostate, OutputLinkParameters> _dataParameters;
        };

        class CaptionParameters {
            public:
                CaptionParameters() {
                }

                void apply(const CaptionParameters &other) {
                }

                bool operator==(const CaptionParameters &other) const {
                    return (this == &other);
                }
        };

        enum class ScteOutputMode {
            PASSTHROUGH,
            REMOVAL
        };

        class ScteOutputParameters {
            private:
                static const ScteOutputMode DEFAULT_MODE     = ScteOutputMode::REMOVAL;
                static const Pid            DEFAULT_PID      = PID_NULL;
                static const Timestamp      DEFAULT_INTERVAL = 0; // disabled

            public:
                ScteOutputParameters() {
                    this->_mode         = DEFAULT_MODE;
                    this->_pid          = DEFAULT_PID;
                    this->_nullInterval = DEFAULT_INTERVAL;
                }

                Pid getPid() const {
                    return this->_pid;
                }

                void setPid(Pid pid) {
                    this->_pid = pid;
                }

                ScteOutputMode getMode() const {
                    return this->_mode;
                }

                void setMode(ScteOutputMode mode) {
                    this->_mode = mode;
                }

                Timestamp getNullInterval() const {
                    return this->_nullInterval;
                }

                void setNullInterval(const utpl::Timestamp ts) {
                    this->_nullInterval = ts;
                }

                void apply(const ScteOutputParameters &other) {
                    if (this->_mode == DEFAULT_MODE) {
                        this->_mode = other._mode;
                    }

                    if (this->_pid == DEFAULT_PID) {
                        this->_pid = other._pid;
                    }

                    if (this->_nullInterval == DEFAULT_INTERVAL) {
                        this->_nullInterval = other._nullInterval;
                    }
                }

            private:
                ScteOutputMode  _mode;
                Pid             _pid;
                utpl::Timestamp _nullInterval;
        };

        enum class ScteSource {
            SEGMENTS,
            MANIFEST,
        };

        class ScteInputParameters {
            public:
                static const ScteSource DEFAULT_SOURCE = ScteSource::SEGMENTS;

            public:
                ScteInputParameters() {
                    this->_source = DEFAULT_SOURCE;
                }

                ScteSource getSource() const {
                    return this->_source;
                }

                void setSource(ScteSource source) {
                    this->_source = source;
                }

            private:
                ScteSource _source;
        };

        class PoisParameters {
            private:
                static const utpl::Timestamp DEFAULT_TIMEOUT = 200;
                static const bool            DEFAULT_ENABLED = false;

            public:
                PoisParameters() {
                    this->_enabled = DEFAULT_ENABLED;
                    this->_timeout = DEFAULT_TIMEOUT;
                }

                bool isEnabled() const {
                    return this->_enabled;
                }

                void setEnabled(bool enabled) {
                    this->_enabled = enabled;
                }

                void setEndpoint(const std::string &endpoint) {
                    this->_endpoint = endpoint;
                }

                const std::string &getEndpoint() const {
                    return this->_endpoint;
                }

                void setAcquisitionPointId(const std::string &id) {
                    this->_acquisitionPointId = id;
                }

                const std::string &getAcquisitionPointId() const {
                    return this->_acquisitionPointId;
                }

                void setTimeout(utpl::Timestamp timeout) {
                    this->_timeout = timeout;
                }

                utpl::Timestamp getTimeout() const {
                    return this->_timeout;
                }

                void apply(const PoisParameters &other) {
                    if (this->_enabled == DEFAULT_ENABLED) {
                        this->_enabled = other._enabled;
                    }

                    if (this->_timeout == DEFAULT_TIMEOUT) {
                        this->_timeout = other._timeout;
                    }

                    if (this->_endpoint.empty()) {
                        this->_endpoint = other._endpoint;
                    }

                    if (this->_acquisitionPointId.empty()) {
                        this->_acquisitionPointId = other._acquisitionPointId;
                    }
                }

            private:
                bool            _enabled;
                std::string     _endpoint;
                std::string     _acquisitionPointId;
                utpl::Timestamp _timeout;
        };

        constexpr utpl::Timestamp DEFAULT_SCTE_PREROLL_TIME_MS  = 4000;
        constexpr utpl::Timestamp DEFAULT_SCTE_NULL_INTERVAL_MS = 0;

        class ScteParameters {
            public:
                ScteParameters() {
                    this->_prerollTime  = DEFAULT_SCTE_PREROLL_TIME_MS;
                    this->_nullInterval = DEFAULT_SCTE_NULL_INTERVAL_MS;
                }

                utpl::Timestamp getPrerollTime() const {
                    return this->_prerollTime;
                }

                void setPrerollTime(utpl::Timestamp tsMs) {
                    this->_prerollTime = tsMs;
                }

                const PoisParameters &getPoisParameters() const {
                    return this->_poisParams;
                }

                void setPoisParameters(const PoisParameters &params) {
                    this->_poisParams = params;
                }

                void setNullInterval(utpl::Timestamp ts) {
                    this->_nullInterval = ts;
                }

                utpl::Timestamp getNullInterval() const {
                    return this->_nullInterval;
                }

                void apply(const ScteParameters &other) {
                    if (this->_prerollTime == DEFAULT_SCTE_PREROLL_TIME_MS) {
                        this->_prerollTime = other._prerollTime;
                    }

                    if (this->_nullInterval == DEFAULT_SCTE_NULL_INTERVAL_MS) {
                        this->_nullInterval = other._nullInterval;
                    }

                    this->_poisParams.apply(other.getPoisParameters());
                }

            private:
                PoisParameters  _poisParams;
                utpl::Timestamp _prerollTime;
                utpl::Timestamp _nullInterval;
        };

        class Scte35Data {
            public:
                Scte35Data() {
                    this->_section     = nullptr;
                    this->_timingMap   = nullptr;
                    this->_runningTime = GST_CLOCK_TIME_NONE;
                }

                Scte35Data(const Scte35Data &other) {
                    this->_section     = gst_mpegts_section_ref(other._section);
                    this->_timingMap   = gst_structure_copy(other._timingMap);
                    this->_runningTime = other._runningTime;
                }

                ~Scte35Data() {
                    if (this->_section) {
                        gst_mpegts_section_unref(this->_section);
                    }

                    if (this->_timingMap) {
                        gst_structure_free(this->_timingMap);
                    }
                }

                void setSection(GstMpegtsSection *section) {
                    if (this->_section) {
                        gst_mpegts_section_unref(this->_section);
                    }

                    this->_section = gst_mpegts_section_ref(section);
                }

                void setTimingMap(GstStructure *structure) {
                    if (this->_timingMap) {
                        gst_structure_free(this->_timingMap);
                    }

                    this->_timingMap = structure;
                }

                GstMpegtsSection *getSection() {
                    return this->_section;
                }

                const GstMpegtsSection *getSection() const {
                    return this->_section;
                }

                GstStructure *getTimingMap() {
                    return this->_timingMap;
                }

                const GstStructure *getTimingMap() const {
                    return this->_timingMap;
                }

                /**
                 * Returns the actual running time of the element that reported the event.
                 */
                GstClockTime getRunningTime() const {
                    return this->_runningTime;
                }

                void setRunningTime(GstClockTime time) {
                    this->_runningTime = time;
                }

                GstClockTime getSpliceTime() const {
                    GstClockTime ret = GST_CLOCK_TIME_NONE;

                    if (this->_section) {
                        auto *sit = gst_mpegts_section_get_scte_sit(this->_section);

                        if (sit) {
                            if (this->_timingMap) {
                                uint64_t eventTime = 0;
                                
                                if (sit->splice_command_type == GST_MTS_SCTE_SPLICE_COMMAND_INSERT) {
                                    if (sit->splices->len > 0) {
                                        GstMpegtsSCTESpliceEvent *event = (GstMpegtsSCTESpliceEvent *)g_ptr_array_index(sit->splices, 0);

                                        std::string fieldName = "event-" + std::to_string(event->splice_event_id) + "-splice-time";

                                        gst_structure_get_uint64(this->_timingMap, fieldName.c_str(), &eventTime);
                                    }

                                } else if (sit->splice_command_type == GST_MTS_SCTE_SPLICE_COMMAND_TIME) {
                                    gst_structure_get_uint64(this->_timingMap, "splice-time", &eventTime);
                                }

                                if (eventTime != 0) {
                                    ret = eventTime;
                                }
                            }
                        }
                    }

                    return ret;
                }

                bool isType(GstMpegtsSCTESpliceCommandType command) const {
                    if (this->_section) {
                        const auto *sit = gst_mpegts_section_get_scte_sit(this->_section);
                        if (sit) {
                            return sit->splice_command_type == command;
                        }
                    }

                    return false;
                }

                bool isTimeSignal() const {
                    return this->isType(GST_MTS_SCTE_SPLICE_COMMAND_TIME);    
                }

                bool isSpliceInsert() const {
                    return this->isType(GST_MTS_SCTE_SPLICE_COMMAND_INSERT);    
                }

            private:
                GstMpegtsSection *_section;
                GstStructure     *_timingMap;
                /**
                 * Timestamp when the SCTE event data was created or received.
                 */
                GstClockTime      _runningTime;
        };

        /**
         * The section as base64, for logging. This is the interchange format every SCTE-35
         * decoder accepts, so a logged event can be pasted straight into one.
         */
        inline std::string toBase64(const Scte35Data &data) {
            auto *section = const_cast<GstMpegtsSection *>(data.getSection());

            if (! section) {
                return {};
            }

            gsize dataSize = 0;

            auto *raw = gst_mpegts_section_packetize(section, &dataSize);
            if (! raw) {
                return {};
            }

            return utils::toBase64(raw, dataSize);
        }

        using ScteDataCallback = std::function<void(const Scte35Data &scteData)>;
        
        using BlackoutSegmentationPriority = std::uint8_t;

        constexpr BlackoutSegmentationPriority BLACKOUT_SEGMENTATION_PRIORITY_MIN = 16;
        constexpr BlackoutSegmentationPriority BLACKOUT_SEGMENTATION_PRIORITY_MAX = 1;

        enum BlackoutRestrictionIgnoreMode {
            NONE,
            WEB_DELIVERY_ALLOWED,
            NO_REGIONAL_BLACKOUT
        };

        class BlackoutSegment {
            public:
                BlackoutSegment() {
                    this->_typeBegin = 0x00; // Not indicated
                    this->_typeEnd   = 0x00; // Not indicated
                    this->_priority  = BLACKOUT_SEGMENTATION_PRIORITY_MIN;
                }

                const std::string &getName() const {
                    return this->_name;
                }

                void setName(const std::string &name) {
                    this->_name = name;
                }

                uint8_t getTypeOfBegin() const {
                    return this->_typeBegin;
                }

                void setTypeOfBegin(uint8_t type) {
                    this->_typeBegin = type;
                }

                uint8_t getTypeOfEnd() const {
                    return this->_typeEnd;
                }

                void setTypeOfEnd(uint8_t type) {
                    this->_typeEnd = type;
                }

                BlackoutSegmentationPriority getPriority() const {
                    return this->_priority;
                }

                void setPriority(BlackoutSegmentationPriority priority) {
                    this->_priority = priority;
                }

            private:
                std::string                  _name;
                uint8_t                      _typeBegin;
                uint8_t                      _typeEnd;
                BlackoutSegmentationPriority _priority;
        };

        class BlackoutCondition {
            public:
                BlackoutCondition() {
                    this->_blankWhenNoDescriptors = false;
                }

                void addSegment(const BlackoutSegment &segmentation) {
                    this->_segments.push_back(segmentation);
                }

                const std::vector<BlackoutSegment> &getSegments() const {
                    return this->_segments;
                }

                bool shouldBlankWhenNoDescriptors() const {
                    return this->_blankWhenNoDescriptors;
                }

                void setBlankWhenNoDescriptors(bool blank) {
                    this->_blankWhenNoDescriptors = blank;
                }

            private:
                bool                         _blankWhenNoDescriptors;
                std::vector<BlackoutSegment> _segments;
        };

        class BlackoutConditions {
            public:
                const BlackoutCondition &getSpliceInsertCondition() const {
                    return this->_spliceInsert;
                }

                void setSpliceInsertCondition(const BlackoutCondition &rules) {
                    this->_spliceInsert = rules;
                }

                const BlackoutCondition &getTimeSignalCondition() const {
                    return this->_timeSignal;
                }

                void setTimeSignalCondition(const BlackoutCondition &rules) {
                    this->_timeSignal = rules;
                }

            private:
                BlackoutCondition _timeSignal;
                BlackoutCondition _spliceInsert;
        };

        class BlackoutParameters {
            public:
                BlackoutParameters() {
                    this->_ignoreMode = BlackoutRestrictionIgnoreMode::NONE;
                    this->_enabled    = false;
                }

                bool isEnabled() const {
                    return this->_enabled;
                }

                void setEnabled(bool enabled) {
                    this->_enabled = enabled;
                }

                BlackoutRestrictionIgnoreMode getRestrictionIgnore() const {
                    return this->_ignoreMode;
                }

                void setRestrictionIgnore(BlackoutRestrictionIgnoreMode mode) {
                    this->_ignoreMode = mode;
                }

                const BlackoutConditions &getConditions() const {
                    return this->_conditions;
                }

                void setConditions(const BlackoutConditions &conditions) {
                    this->_conditions = conditions;
                }

            private:
                bool _enabled;
                BlackoutRestrictionIgnoreMode _ignoreMode;
                BlackoutConditions            _conditions;
        };

        // What the output carries on its SCTE PID while a locally inserted break is on air.
        enum class AdInsertionOutputScte {
            // Leave the source markers alone. Downstream still sees the original cues, exactly as
            // it does without ad insertion.
            PASSTHROUGH,

            // Drop the source markers for the duration of the break, so downstream cannot splice
            // over content that has already been replaced here.
            SUPPRESS
        };

        // Replaces the programme with an alternate source for the duration of a signalled avail.
        //
        // The trigger reuses the blackout rule shape: a list of segmentation type pairs that open
        // and close a section, plus the delivery restrictions to honor. A break is opened by a
        // cue-out and closed by a cue-in; signalled durations are deliberately not used to close
        // it, so `maxBreakDuration` is the only backstop against a lost cue-in.
        class AdInsertionParameters {
            public:
                static constexpr utpl::Timestamp DEFAULT_MAX_BREAK_DURATION = 600000;

                AdInsertionParameters() {
                    this->_enabled          = false;
                    this->_maxBreakDuration = DEFAULT_MAX_BREAK_DURATION;
                    this->_outputScte       = AdInsertionOutputScte::PASSTHROUGH;
                }

                bool isEnabled() const {
                    return this->_enabled;
                }

                void setEnabled(bool enabled) {
                    this->_enabled = enabled;
                }

                utpl::Timestamp getMaxBreakDuration() const {
                    return this->_maxBreakDuration;
                }

                void setMaxBreakDuration(utpl::Timestamp duration) {
                    this->_maxBreakDuration = duration;
                }

                AdInsertionOutputScte getOutputScte() const {
                    return this->_outputScte;
                }

                void setOutputScte(AdInsertionOutputScte mode) {
                    this->_outputScte = mode;
                }

                const BlackoutParameters &getTrigger() const {
                    return this->_trigger;
                }

                void setTrigger(const BlackoutParameters &trigger) {
                    this->_trigger = trigger;
                }

            private:
                bool                  _enabled;
                utpl::Timestamp       _maxBreakDuration;
                AdInsertionOutputScte _outputScte;
                BlackoutParameters    _trigger;
        };

        class GstPluginParameters {
            public:
                GstPluginParameters(const std::string &name, GstRank rank) : _name(name) {
                    this->_rank = rank;
                }

                const std::string &getName() const {
                    return this->_name;
                }

                GstRank getRank() const {
                    return this->_rank;
                }

            private:
                std::string _name;
                GstRank     _rank;
        };
    }
}

#endif /* __UTPL_TRANSCODER_TYPES_H__ */
