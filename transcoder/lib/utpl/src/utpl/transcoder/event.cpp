#include "utpl/transcoder/event.h"
#include "utpl/transcoder/types.h"

#include "utpl/common/debug.h"

using namespace utpl;

transcoder::Event::Event(Id id, utpl::Timestamp startTimestamp, utpl::Timestamp duration) {
    this->_id       = id;
    this->_start    = startTimestamp;
    this->_duration = duration;
}

transcoder::Event::~Event() {
}

transcoder::Event::Id transcoder::Event::getId() const {
    return this->_id;
}

utpl::Timestamp transcoder::Event::getTimestamp() const {
    return this->_start;
}

utpl::Timestamp transcoder::Event::getDuration() const {
    return this->_duration;
}

std::string transcoder::Event::toString() const {
    return "Event id: " + std::to_string(this->_id) + ", start at: " + utpl::Time::toSqliteDateTime(this->_start) + ", ends at: " + utpl::Time::toSqliteDateTime(this->_start + this->_duration);
}


transcoder::EventSqueeze::EventSqueeze(Id id, utpl::Timestamp startTimestamp, utpl::Timestamp duration, const std::string &name) : Event(id, startTimestamp, duration) {
    this->_srcWindow = ORIGINAL_WINDOW_NORM;
    this->_dstWindow = ORIGINAL_WINDOW_NORM;
    this->_name      = name;
    this->_fadeIn    = 0;
    this->_fadeOut   = 0;
}

const std::string &transcoder::EventSqueeze::getName() const {
    return this->_name;
}

utpl::Timestamp transcoder::EventSqueeze::getFadeInDuration() const {
    return this->_fadeIn;
}

utpl::Timestamp transcoder::EventSqueeze::getFadeOutDuration() const {
    return this->_fadeOut;
}

void transcoder::EventSqueeze::setFadeDuration(utpl::Timestamp in, utpl::Timestamp out) {
    this->_fadeIn  = in;
    this->_fadeOut = out;
}

const utpl::NormRect &transcoder::EventSqueeze::getSrcWindow() const {
    return this->_srcWindow;
}

const utpl::NormRect &transcoder::EventSqueeze::getDstWindow() const {
    return this->_dstWindow;
}

void transcoder::EventSqueeze::setWindows(const utpl::NormRect &srcWindow, const utpl::NormRect &dstWindow) {
    this->_srcWindow = srcWindow;
    this->_dstWindow = dstWindow;
}


std::string transcoder::EventSqueeze::toString() const {
    return "[SQUEEZE] " + Event::toString() + ", name: '" + this->_name + "'" + ", fadeIn: " + std::to_string(this->_fadeIn) + ", fadeOut: " + std::to_string(this->_fadeOut);
}


transcoder::EventOverlay::EventOverlay(Id id, utpl::Timestamp startTime, utpl::Timestamp duration) : Event(id, startTime, duration) {
    this->_fadeIn  = 0;
    this->_fadeOut = 0;
    this->_opacity = 1.0;
    this->_hasZOrder = false;
    this->_zOrder = 0;
}

const utpl::NormRect &transcoder::EventOverlay::getGeometry() const {
    return this->_geometry;
}

void transcoder::EventOverlay::setGeometry(const utpl::NormRect &rect) {
    this->_geometry = rect;
}

float transcoder::EventOverlay::getOpacity() const {
    return this->_opacity;
}

void transcoder::EventOverlay::setOpacity(float opacity) {
    this->_opacity = opacity;
}

utpl::Timestamp transcoder::EventOverlay::getFadeInDuration() const {
    return this->_fadeIn;
}

utpl::Timestamp transcoder::EventOverlay::getFadeOutDuration() const {
    return this->_fadeOut;
}

void transcoder::EventOverlay::setFadeDuration(utpl::Timestamp in, utpl::Timestamp out) {
    this->_fadeIn  = in;
    this->_fadeOut = out;
}

const Fraction &transcoder::EventOverlay::getRate() const {
    return this->_rate;
}

void transcoder::EventOverlay::setRate(const Fraction &rate) {
    this->_rate = rate;
}

const std::vector<std::string> &transcoder::EventOverlay::getUris() const {
    return this->_uris;
}

void transcoder::EventOverlay::setUris(const std::vector<std::string> &uris) {
    this->_uris = uris;
}

bool transcoder::EventOverlay::hasZOrder() const {
    return this->_hasZOrder;
}

int transcoder::EventOverlay::getZOrder() const {
    return this->_zOrder;
}

void transcoder::EventOverlay::setZOrder(int order) {
    this->_zOrder = order;
    this->_hasZOrder = true;
}

std::string transcoder::EventOverlay::toString() const {
    return "[OVERLAY] " + Event::toString() + ", opacity: " + std::to_string(this->_opacity) + ", fadeIn: " + std::to_string(this->_fadeIn) + ", fadeOut: " + std::to_string(this->_fadeOut) + 
        ", rate: " + std::to_string(this->_rate.getNumerator()) + ", " + std::to_string(this->_rate.getDenominator()) +
        (this->_hasZOrder ? ", zOrder: " + std::to_string(this->_zOrder) : "");
}


transcoder::EventSpliceInsert::EventSpliceInsert(
    Id id,
    utpl::Timestamp startTime,
    uint32_t eventId,
    utpl::Timestamp eventDuration,
    bool autoReturn,
    Segmentation segmentation,
    std::vector<uint8_t> rawSection
) : Event(id, startTime, 0) {
    this->_eventId      = eventId;
    this->_duration     = eventDuration;
    this->_autoReturn   = autoReturn;
    this->_segmentation = std::move(segmentation);
    this->_rawSection   = std::move(rawSection);
}

transcoder::EventSpliceInsert::EventSpliceInsert(
    Id id,
    utpl::Timestamp startTime,
    uint32_t eventId,
    utpl::Timestamp eventDuration,
    bool autoReturn
) : EventSpliceInsert(id, startTime, eventId, eventDuration, autoReturn, Segmentation()) {
}

std::string transcoder::EventSpliceInsert::toString() const {
    return "[SPLICE_INSERT] " + Event::toString() + ", eventId: " + std::to_string(this->_eventId) + ", durationMs: " + std::to_string(this->_duration) + ", autoReturn: " + (this->_autoReturn ? "yes" : "no") + ", section: " + (this->hasRawSection() ? std::to_string(this->_rawSection.size()) + " raw bytes" : std::string("built"));
}


transcoder::EventTimeSignal::EventTimeSignal(
    Id id,
    utpl::Timestamp startTime,
    std::vector<SegmentationDescriptor> &&descriptors,
    std::vector<uint8_t> rawSection
) : Event(id, startTime, 0) {
    this->_descriptors = std::move(descriptors);
    this->_rawSection  = std::move(rawSection);
}

const std::vector<transcoder::EventTimeSignal::SegmentationDescriptor> &transcoder::EventTimeSignal::getSegmentationDescriptors() const {
    return this->_descriptors;
}

std::string transcoder::EventTimeSignal::toString() const {
    return "[TIME_SIGNAL] " + Event::toString() + ", have " + std::to_string(this->_descriptors.size()) + " descriptors" + ", section: " + (this->hasRawSection() ? std::to_string(this->_rawSection.size()) + " raw bytes" : std::string("built"));
}

bool transcoder::EventTimeSignal::isEqual(const Event &other) const {
    const auto &o = static_cast<const EventTimeSignal &>(other);

    if (this->_rawSection != o._rawSection) {
        return false;
    }

    if (this->getSegmentationDescriptors().size() != o.getSegmentationDescriptors().size()) {
        return false;
    }

    for (auto i = 0; i < this->getSegmentationDescriptors().size(); i++) {
        const auto &thisDesc = this->getSegmentationDescriptors()[i];
        const auto &oDesc    = o.getSegmentationDescriptors()[i];

        if (! thisDesc.isEqual(oDesc)) {
            return false;
        }
    }

    return true;
}


#define SEGMENTATION_UPID_TYPE_ADS 14 // Advertising information


transcoder::EventTimeSignal::SegmentationDescriptor::SegmentationDescriptor() {
    this->eventId             = 1;
    this->isEventCancelled    = false;

    this->hasSegmentationDuration = false;
    this->webDeliveryAllowed      = true;
    this->noRegionalBlackout      = true;
    this->archiveAllowed          = true;
    this->deviceRestriction       = DeviceRestriction::NONE;
    this->segmentationDuration    = 0;
    this->upidType                = SEGMENTATION_UPID_TYPE_ADS; // Taken from AWS java SDK
    this->segmentationTypeId      = 0;
    this->segmentNum              = 0;
    this->segmentsExpected        = 0;
    this->subSegmentNum           = 0;
    this->subSegmentsExpected     = 0;
}

bool transcoder::EventTimeSignal::SegmentationDescriptor::isEqual(const SegmentationDescriptor &other) const {
    if (
        this->eventId             == other.eventId &&
        this->isEventCancelled    == other.isEventCancelled &&

        this->hasSegmentationDuration == other.hasSegmentationDuration &&
        this->webDeliveryAllowed      == other.webDeliveryAllowed &&
        this->noRegionalBlackout      == other.noRegionalBlackout &&
        this->archiveAllowed          == other.archiveAllowed &&
        this->deviceRestriction       == other.deviceRestriction &&
        this->segmentationDuration    == other.segmentationDuration &&
        this->upidType                == other.upidType &&
        
        (
            this->upid.size() == other.upid.size() &&
            std::equal(this->upid.begin(), this->upid.end(), other.upid.begin())
        ) &&
        
        this->segmentationTypeId  == other.segmentationTypeId &&
        this->segmentNum          == other.segmentNum &&
        this->segmentsExpected    == other.segmentsExpected &&
        this->subSegmentNum       == other.subSegmentNum &&
        this->subSegmentsExpected == other.subSegmentsExpected
    ) {
        return true;
    }

    return false;
}


transcoder::EventReturnToNetwork::EventReturnToNetwork(
    Id id,
    utpl::Timestamp startTime,
    uint32_t eventId,
    std::vector<uint8_t> rawSection
) : Event(id, startTime, 0) {
    this->_eventId    = eventId;
    this->_rawSection = std::move(rawSection);
}

std::string transcoder::EventReturnToNetwork::toString() const {
    return "[RETURN_TO_NETWORK] " + Event::toString() + ", eventId: " + std::to_string(this->_eventId) + ", section: " + (this->hasRawSection() ? std::to_string(this->_rawSection.size()) + " raw bytes" : std::string("built"));
}


transcoder::EventSlate::EventSlate(Id id, utpl::Timestamp startTime, utpl::Timestamp duration) : Event(id, startTime, duration) {
}

void transcoder::EventSlate::setUri(const std::string &uri) {
    this->_uri = uri;
}

const std::string &transcoder::EventSlate::getUri() const {
    return this->_uri;
}

std::string transcoder::EventSlate::toString() const {
    return "[SLATE] " + Event::toString() + ", duration: " + std::to_string(this->getDuration());   
}
