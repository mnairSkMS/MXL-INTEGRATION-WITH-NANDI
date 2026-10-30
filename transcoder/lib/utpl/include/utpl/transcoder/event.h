#ifndef __UTPL_TRANSCODER_EVENT_H__
#define __UTPL_TRANSCODER_EVENT_H__

#include <cstdint>
#include <vector>

#include "utpl/common/types.h"
#include "utpl/common/time.h"


namespace utpl {
    namespace transcoder {
        class Event {
            public:
                using Id = int64_t;

            public:
                Event(Id id, utpl::Timestamp startTimestamp, utpl::Timestamp duration);
                virtual ~Event();

                Id getId() const;
                utpl::Timestamp getTimestamp() const;
                utpl::Timestamp getDuration() const;

                virtual std::string toString() const;

                bool operator==(const Event &other) const {
                    return typeid(*this) == typeid(other) && isEqual(other);
                }

                bool operator!=(const Event &other) const {
                    return ! (*this == other);
                }

            private:
                virtual bool isEqual(const Event &other) const = 0;

            private:
                Id              _id;
                utpl::Timestamp _start;
                utpl::Timestamp _duration;
        };

        class EventSqueeze : public Event {
            public:
                EventSqueeze(Id id, utpl::Timestamp startTimestamp, utpl::Timestamp duration, const std::string &name);

                const std::string &getName() const;

                utpl::Timestamp getFadeInDuration() const;
                utpl::Timestamp getFadeOutDuration() const;
                void setFadeDuration(utpl::Timestamp in, utpl::Timestamp out);

                const utpl::NormRect &getSrcWindow() const;
                const utpl::NormRect &getDstWindow() const;
                void setWindows(const utpl::NormRect &srcWindow, const utpl::NormRect &dstWindow);

                std::string toString() const override;

            private:
                bool isEqual(const Event &other) const override {
                    const auto &o = static_cast<const EventSqueeze &>(other);
                    return
                        (this->_name      == o._name) &&
                        (this->_fadeIn    == o._fadeIn) &&
                        (this->_fadeOut   == o._fadeOut) &&
                        (this->_srcWindow == o._srcWindow) &&
                        (this->_dstWindow == o._dstWindow);
                }

            private:
                std::string     _name;
                utpl::Timestamp _fadeIn;
                utpl::Timestamp _fadeOut;
                utpl::NormRect  _srcWindow;
                utpl::NormRect  _dstWindow;
        };

        class EventOverlay : public Event {
            public:
                static constexpr Timestamp UNLIMITED_DURATION = -1;

            public:
                EventOverlay(Id id, utpl::Timestamp startTime, utpl::Timestamp duration);

                const utpl::NormRect &getGeometry() const;
                void setGeometry(const utpl::NormRect &rect);

                float getOpacity() const;
                void setOpacity(float opacity);

                utpl::Timestamp getFadeInDuration() const;
                utpl::Timestamp getFadeOutDuration() const;
                void setFadeDuration(utpl::Timestamp in, utpl::Timestamp out);

                const Fraction &getRate() const;
                void setRate(const Fraction &rate);

                const std::vector<std::string> &getUris() const;
                void setUris(const std::vector<std::string> &uris);

                // When unset, the compositor assigns the normal foreground order.
                // An explicit value can place an overlay behind the programme layer.
                bool hasZOrder() const;
                int getZOrder() const;
                void setZOrder(int order);

                std::string toString() const override;

            private:
                bool isEqual(const Event &other) const override {
                    const auto &o = static_cast<const EventOverlay &>(other);

                    return
                        (
                            this->_uris.size() == o._uris.size() &&
                            std::equal(this->_uris.begin(), this->_uris.end(), o._uris.begin())
                        ) &&
                        this->_geometry == o._geometry &&
                        this->_opacity  == o._opacity &&
                        this->_fadeIn   == o._fadeIn &&
                        this->_fadeOut  == o._fadeOut &&
                        this->_rate     == o._rate &&
                        this->_hasZOrder == o._hasZOrder &&
                        this->_zOrder   == o._zOrder;
                }

            private:
                std::vector<std::string> _uris;
                utpl::NormRect           _geometry;
                float                    _opacity;
                utpl::Timestamp          _fadeIn;
                utpl::Timestamp          _fadeOut;
                Fraction                 _rate;
                bool                     _hasZOrder;
                int                      _zOrder;
        };

        class EventSlate : public Event {
            public:
                EventSlate(Id id, utpl::Timestamp startTime, utpl::Timestamp duration);

                void setUri(const std::string &uri);
                const std::string &getUri() const;

                std::string toString() const override;

            private:
                bool isEqual(const Event &other) const override {
                    const auto &o = static_cast<const EventSlate &>(other);

                    return
                        this->_uri == o._uri;
                }

            private:
                std::string _uri;
        };

        class EventSpliceInsert : public Event {
            public:
                /**
                 * Optional segmentation descriptor carried alongside the splice command.
                 *
                 * SCTE-35 puts descriptors on the splice_info_section, not on the command,
                 * so a splice_insert may carry a segmentation_descriptor exactly as a
                 * time_signal does. One is emitted only when a UPID is supplied.
                 */
                struct Segmentation {
                    std::vector<uint8_t> upid;
                    uint8_t              upidType          = 0x0F;  // URI: a plain string
                    uint8_t              segmentationTypeId = 0x34;  // Provider PO Start

                    bool isEmpty() const {
                        return this->upid.empty();
                    }

                    bool operator==(const Segmentation &o) const {
                        return
                            (this->upid               == o.upid)      &&
                            (this->upidType           == o.upidType)  &&
                            (this->segmentationTypeId == o.segmentationTypeId);
                    }
                };

                EventSpliceInsert(
                    Id id,
                    utpl::Timestamp startTime,
                    uint32_t eventId,
                    utpl::Timestamp eventDuration,
                    bool autoReturn,
                    Segmentation segmentation,
                    std::vector<uint8_t> rawSection = {}
                );

                /**
                 * Convenience overload for a break with no segmentation descriptor.
                 *
                 * Segmentation cannot carry a default argument here: its members use
                 * default member initializers, which are not complete until the end of the
                 * enclosing class, so a second constructor is the way to keep the short
                 * form available.
                 */
                EventSpliceInsert(
                    Id id,
                    utpl::Timestamp startTime,
                    uint32_t eventId,
                    utpl::Timestamp eventDuration,
                    bool autoReturn = true
                );

                /**
                 * Empty unless a UPID was supplied, in which case the section carries a
                 * matching segmentation_descriptor.
                 */
                const Segmentation &getSegmentation() const {
                    return this->_segmentation;
                }

                uint32_t getEventId() const {
                    return this->_eventId;
                }

                /**
                 * break_duration_auto_return for the splice_insert this event builds.
                 *
                 * true  - the break closes itself when the duration elapses.
                 * false - it stays open until a matching return-to-network is signalled.
                 *
                 * Has no effect when a raw section is supplied: the flag is then whatever
                 * that section carries.
                 */
                bool isAutoReturn() const {
                    return this->_autoReturn;
                }

                utpl::Timestamp getDuration() const {
                    return this->_duration;
                }

                /**
                 * A complete, pre-authored splice_info_section supplied by the operator.
                 * When set it replaces the field-by-field section builder: the payload is
                 * taken from these bytes and only the splice time is rewritten.
                 */
                const std::vector<uint8_t> &getRawSection() const {
                    return this->_rawSection;
                }

                bool hasRawSection() const {
                    return ! this->_rawSection.empty();
                }

                std::string toString() const override;

            private:
                bool isEqual(const Event &other) const override {
                    const auto &o = static_cast<const EventSpliceInsert &>(other);
                    return
                        (this->_duration   == o._duration)   &&
                        (this->_eventId    == o._eventId)    &&
                        (this->_autoReturn == o._autoReturn) &&
                        (this->_segmentation == o._segmentation) &&
                        (this->_rawSection == o._rawSection);
                }

            private:
                utpl::Timestamp      _duration;
                uint32_t             _eventId;
                bool                 _autoReturn;
                Segmentation         _segmentation;
                std::vector<uint8_t> _rawSection;
        };

        class EventReturnToNetwork : public Event {
            public:
                EventReturnToNetwork(
                    Id id,
                    utpl::Timestamp startTime,
                    uint32_t eventId,
                    std::vector<uint8_t> rawSection = {}
                );

                uint32_t getEventId() const {
                    return this->_eventId;
                }

                /**
                 * A complete, pre-authored splice_info_section supplied by the operator.
                 * When set it replaces the field-by-field section builder: the payload is
                 * taken from these bytes and only the splice time is rewritten.
                 */
                const std::vector<uint8_t> &getRawSection() const {
                    return this->_rawSection;
                }

                bool hasRawSection() const {
                    return ! this->_rawSection.empty();
                }

                std::string toString() const override;

            private:
                bool isEqual(const Event &other) const override {
                    const auto &o = static_cast<const EventReturnToNetwork &>(other);

                    return
                        (this->_eventId    == o._eventId) &&
                        (this->_rawSection == o._rawSection);
                }

            private:
                uint32_t             _eventId;
                std::vector<uint8_t> _rawSection;
        };

        class EventTimeSignal : public Event {
            public:
                enum DeviceRestriction {
                    GROUP_0,
                    GROUP_1,
                    GROUP_2,
                    NONE
                };

                struct SegmentationDescriptor {
                    uint32_t eventId;
                    bool     isEventCancelled;

                    bool hasSegmentationDuration;
                    bool webDeliveryAllowed;
                    bool noRegionalBlackout;
                    bool archiveAllowed;
                    DeviceRestriction deviceRestriction;
                    uint64_t segmentationDuration;

                    uint8_t upidType;
                    std::vector<uint8_t> upid;
                    uint8_t segmentationTypeId;

                    uint8_t segmentNum;
                    uint8_t segmentsExpected;
                    uint8_t subSegmentNum;
                    uint8_t subSegmentsExpected;

                    SegmentationDescriptor();
                    bool isEqual(const SegmentationDescriptor &other) const;
                };

            public:
                EventTimeSignal(
                    Id id,
                    utpl::Timestamp startTime,
                    std::vector<SegmentationDescriptor> &&descriptors,
                    std::vector<uint8_t> rawSection = {}
                );

                const std::vector<SegmentationDescriptor> &getSegmentationDescriptors() const;

                /**
                 * A complete, pre-authored splice_info_section supplied by the operator.
                 * When set it replaces the field-by-field section builder: the payload is
                 * taken from these bytes and only the splice time is rewritten.
                 */
                const std::vector<uint8_t> &getRawSection() const {
                    return this->_rawSection;
                }

                bool hasRawSection() const {
                    return ! this->_rawSection.empty();
                }

                std::string toString() const override;

                bool isEqual(const Event &other) const override;

            private:
                std::vector<SegmentationDescriptor> _descriptors;
                std::vector<uint8_t>                _rawSection;
        };
    }
}

#endif /* __UTPL_TRANSCODER_EVENT_H__ */
