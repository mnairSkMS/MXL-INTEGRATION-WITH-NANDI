#include <sstream>
#include <iomanip>
#include <SQLiteCpp/SQLiteCpp.h>

#include "utpl/common/debug.h"
#include "utpl/common/utils.h"

#include "application/event/provider/sqlite.h"
#include "application/event/provider/sqlite/dao.h"


app::SqliteEventProvider::SqliteEventProvider(const std::string &dbPath) : _dbPath(dbPath) {
}

std::vector<std::unique_ptr<utpl::transcoder::Event>> app::SqliteEventProvider::getNextEvents(utpl::Timestamp notBefore, utpl::Timestamp notAfter) {
    std::vector<std::unique_ptr<utpl::transcoder::Event>> ret;

    try {
        SQLite::Database db(this->_dbPath, SQLite::OPEN_READONLY);

        {
            EventsDao eventsDao(db);

            std::vector<EventModel> events;

            if (eventsDao.getInTimeRange(events, notBefore, notAfter)) {
                std::vector<EventModel *> toBeUpdated;

                for (auto &event : events) {
                    std::unique_ptr<utpl::transcoder::Event> mappedEvent;

                    // Skip processed events
                    if (event.getReadTime() != 0) {
                        continue;
                    }

                    toBeUpdated.push_back(&event);

                    switch (event.getType()) {
                        case EventModel::Type::IMAGE_OVERLAY:
                            mappedEvent = this->_parseOverlay(event);
                            break;

                        case EventModel::Type::SQUEEZE:
                            mappedEvent = this->_parseSqueeze(event);
                            break;

                        case EventModel::Type::SLATE:
                            mappedEvent = this->_parseSlate(event);
                            break;

                        case EventModel::Type::SCTE35_TIMESIGNAL:
                            mappedEvent = this->_parseTimeSignal(event);
                            break;

                        case EventModel::Type::SCTE35_SPLICEINSERT:
                            mappedEvent = this->_parseSpliceInsert(event);
                            break;

                        case EventModel::Type::SCTE35_RETURNTONETWORK:
                            mappedEvent = this->_parseReturnToNetwork(event);
                            break;

                        default:
                            WARN(("Found not supported event type {} - skipping", (int) event.getType()));
                    }

                    if (mappedEvent) {
                        ret.push_back(std::move(mappedEvent));
                    }
                }
                
                if (! toBeUpdated.empty()) {
                    utpl::Timestamp currentTime = utpl::Time::getUTCTimestamp();

                    {
                        SQLite::Database rwDb(this->_dbPath, SQLite::OPEN_READWRITE);

                        try {
                            SQLite::Transaction transaction(rwDb);

                            EventsDao dao(rwDb);

                            for (auto *event : toBeUpdated) {
                                event->setReadTime(currentTime);
                            }

                            for (auto *event : toBeUpdated) {
                                dao.updateReadTime(*event);
                            }

                            transaction.commit();

                        } catch (const std::exception &ex) {
                            this->_handleSqliteException(ex);
                        }
                    }
                }
            }
        }

    } catch (const std::exception &ex) {
        this->_handleSqliteException(ex);
    }

    return ret;
}

void app::SqliteEventProvider::markProcessed(const utpl::transcoder::Event &event, const std::string &errorMessage) {
    
}

bool app::SqliteEventProvider::isValid(const utpl::transcoder::Event &event) {
    try {
        SQLite::Database db(this->_dbPath, SQLite::OPEN_READONLY);

        {
            EventsDao eventsDao(db);

            EventModel model;

            if (eventsDao.getById(event.getId(), model)) {
                return true;
            }
        }

    } catch (const std::exception &ex) {
        this->_handleSqliteException(ex);
    }

    return false;
}

void app::SqliteEventProvider::start() {
    
}

void app::SqliteEventProvider::stop() {
    
}

void app::SqliteEventProvider::_handleSqliteException(const std::exception &ex) {
    ERR(("Cought an error from SQLite: {}", ex.what()));
}

template<typename T>
static T _get(const nlohmann::json &json, const std::string &key, const T &defaultValue) {
    if (json.contains(key)) {
        auto &val = json[key];
        if (! val.is_null()) {
            return val.get<T>();
        }
    }

    return defaultValue;
}

/**
 * Decodes an optional operator-supplied splice_info_section from the event details.
 *
 * The REST layer has already validated the section, so anything reaching here that fails
 * to decode means the row was written by some other route; log it and fall back to the
 * field-by-field builder rather than dropping the event.
 */
static std::vector<uint8_t> _getRawSection(const nlohmann::json &json) {
    auto encoded = _get<std::string>(json, "scte35_base64", std::string());

    if (encoded.empty()) {
        return {};
    }

    auto decoded = utpl::utils::fromBase64(encoded);

    if (decoded.empty()) {
        ERR(("Unable to decode scte35_base64 ({} characters) - ignoring it", encoded.size()));
    }

    return decoded;
}


std::unique_ptr<utpl::transcoder::EventOverlay> app::SqliteEventProvider::_parseOverlay(const EventModel &model) {
    auto id = model.getId();
    auto startTs = model.getTime();

    auto &details = model.getDetails();
    if (details.contains("image_overlay")) {
        auto &overlay = details["image_overlay"];

        if (! overlay.is_null()) {
            auto event = std::make_unique<utpl::transcoder::EventOverlay>(
                id, startTs, _get<int64_t>(overlay, "duration", utpl::transcoder::EventOverlay::UNLIMITED_DURATION)
            );

            if (overlay.contains("geometry_norm")) {
                auto &geometry = overlay["geometry_norm"];

                if (! geometry.is_null()) {
                    auto rect = event->getGeometry();

                    rect.x(_get<double>(geometry, "x", rect.x()));
                    rect.y(_get<double>(geometry, "y", rect.y()));

                    rect.width (_get<double>(geometry, "width",  rect.width()));
                    rect.height(_get<double>(geometry, "height", rect.height()));

                    event->setGeometry(rect);
                }
            }

            {
                auto fadeIn  = _get<int>(overlay, "fade_in",  event->getFadeInDuration());
                auto fadeOut = _get<int>(overlay, "fade_out", event->getFadeOutDuration());

                event->setFadeDuration(fadeIn, fadeOut);
            }

            event->setOpacity(_get<float>(overlay, "opacity", event->getOpacity()));

            if (overlay.contains("z_order") && !overlay["z_order"].is_null()) {
                event->setZOrder(_get<int>(overlay, "z_order", 0));
            }

            {
                auto delay = _get<int>(overlay, "delay_between_images", 0);
                if (delay) {
                    event->setRate(utpl::Fraction(delay, 1000));
                }
            }

            {
                std::vector<std::string> uris;

                if (overlay.contains("image_urls")) {
                    auto &urls = overlay["image_urls"];
                    if (! urls.is_null()) {
                        for (auto element : overlay["image_urls"]) {
                            uris.push_back(element.get<std::string>());
                        }
                    }
                }

                event->setUris(uris);
            }

            return event;
        }
    }

    return nullptr;
}

std::unique_ptr<utpl::transcoder::EventSqueeze> app::SqliteEventProvider::_parseSqueeze(const EventModel &model) {
    auto id = model.getId();
    auto startTs = model.getTime();
    int64_t duration = 0;

    auto &details = model.getDetails();
    if (details.contains("squeeze")) {
        auto &squeeze = details["squeeze"];
        if (! squeeze.is_null()) {
            std::string squeezeName;

            if (squeeze.contains("squeeze_name")) {
                auto &name = squeeze["squeeze_name"];
                if (! name.is_null()) {
                    squeezeName = name.get<std::string>();
                }
            }

            auto event = std::make_unique<utpl::transcoder::EventSqueeze>(
                id, startTs, _get<int64_t>(squeeze, "duration", 0), squeezeName
            );

            event->setFadeDuration(
                _get<int>(squeeze, "fade_in", event->getFadeInDuration()),
                _get<int>(squeeze, "fade_out", event->getFadeOutDuration())
            );

            {
                utpl::NormRect src = event->getSrcWindow();
                utpl::NormRect dst = event->getDstWindow();

                if (squeeze.contains("input_window_norm")) {
                    auto &input = squeeze["input_window_norm"];
                    if (! input.is_null()) {
                        src.x(_get<double>(input, "x", src.x()));
                        src.y(_get<double>(input, "y", src.y()));
                        src.width(_get<double>(input, "width", src.width()));
                        src.height(_get<double>(input, "height", src.height()));
                    }
                }

                if (squeeze.contains("output_window_norm")) {
                    auto &output = squeeze["output_window_norm"];
                    if (! output.is_null()) {
                        dst.x(_get<double>(output, "x", dst.x()));
                        dst.y(_get<double>(output, "y", dst.y()));
                        dst.width(_get<double>(output, "width", dst.width()));
                        dst.height(_get<double>(output, "height", dst.height()));
                    }
                }

                event->setWindows(src, dst);
            }
            
            return event;
        }
    }

    return nullptr;
}

std::unique_ptr<utpl::transcoder::EventSlate> app::SqliteEventProvider::_parseSlate(const EventModel &model) {
    auto id = model.getId();
    auto startTs = model.getTime();

    auto &details = model.getDetails();
    if (details.contains("slate")) {
        auto &slate = details["slate"];

        if (! slate.is_null()) {
            auto event = std::make_unique<utpl::transcoder::EventSlate>(id, startTs, _get<int64_t>(slate, "duration", 0));

            event->setUri(slate["url"].get<std::string>());
            
            return event;
        }
    }

    return nullptr;
}

std::unique_ptr<utpl::transcoder::EventTimeSignal> app::SqliteEventProvider::_parseTimeSignal(const EventModel &model) {
    auto id = model.getId();
    auto startTs = model.getTime();
    int64_t duration = 0;

    auto &details = model.getDetails();
    if (details.contains("scte35_timesignal")) {
        auto &time = details["scte35_timesignal"];

        if (! time.is_null()) {
            std::vector<utpl::transcoder::EventTimeSignal::SegmentationDescriptor> descriptors;

            if (time.contains("scte35_descriptors")) {
                auto &des = time["scte35_descriptors"];

                if (! des.is_null() && des.is_array()) {
                    for (auto &d : des) {
                        utpl::transcoder::EventTimeSignal::SegmentationDescriptor newdescriptor;

                        if (d.contains("delivery_restrictions")) {
                            auto &dr = d["delivery_restrictions"];
                            if (dr.is_object()) {
                                {
                                    const auto &aaf = _get<std::string>(dr, "archive_allowed_flag", "");

                                    if (! aaf.empty()) {
                                        if (aaf == "ARCHIVE_NOT_ALLOWED") {
                                            newdescriptor.archiveAllowed = false;

                                        } else if (aaf == "ARCHIVE_ALLOWED") {
                                            newdescriptor.archiveAllowed = true;

                                        } else {
                                            ERR(("Not supported value for 'archive_allowed_flag': {}", aaf));
                                        }
                                    }
                                }

                                {
                                    const auto devR = _get<std::string>(dr, "device_restrictions", "");

                                    if (! devR.empty()) {
                                        if (devR == "NONE") {
                                            newdescriptor.deviceRestriction = utpl::transcoder::EventTimeSignal::DeviceRestriction::NONE;

                                        } else if (devR == "RESTRICT_GROUP0") {
                                            newdescriptor.deviceRestriction = utpl::transcoder::EventTimeSignal::DeviceRestriction::GROUP_0;

                                        } else if (devR == "RESTRICT_GROUP1") {
                                            newdescriptor.deviceRestriction = utpl::transcoder::EventTimeSignal::DeviceRestriction::GROUP_1;

                                        } else if (devR == "RESTRICT_GROUP2") {
                                            newdescriptor.deviceRestriction = utpl::transcoder::EventTimeSignal::DeviceRestriction::GROUP_2;

                                        } else {
                                            ERR(("Not supported value for 'device_restrictions': {}", devR));
                                        }
                                    }
                                }

                                {
                                    const auto nrb = _get<std::string>(dr, "no_regional_blackout_flag", "");

                                    if (! nrb.empty()) {
                                        if (nrb == "REGIONAL_BLACKOUT") {
                                            newdescriptor.noRegionalBlackout = false;

                                        } else if (nrb == "NO_REGIONAL_BLACKOUT") {
                                            newdescriptor.noRegionalBlackout = true;

                                        } else {
                                            ERR(("Not supported value for 'no_regional_blackout_flag': {}", nrb));
                                        }
                                    }
                                }

                                {
                                    const auto nrb = _get<std::string>(dr, "web_delivery_allowed_flag", "");

                                    if (! nrb.empty()) {
                                        if (nrb == "WEB_DELIVERY_NOT_ALLOWED") {
                                            newdescriptor.webDeliveryAllowed = false;

                                        } else if (nrb == "WEB_DELIVERY_ALLOWED") {
                                            newdescriptor.webDeliveryAllowed = true;

                                        } else {
                                            ERR(("Not supported value for 'web_delivery_allowed_flag': {}", nrb));
                                        }
                                    }
                                }
                            }
                        }

                        newdescriptor.eventId = _get<uint32_t>(d, "segmentation_event_id", newdescriptor.eventId);

                        {
                            const auto sci = _get<std::string>(d, "segmentation_cancel_indicator", "");

                            if (! sci.empty()) {
                                if (sci == "SEGMENTATION_EVENT_NOT_CANCELED") {
                                    newdescriptor.isEventCancelled = false;

                                } else if (sci == "SEGMENTATION_EVENT_CANCELED") {
                                    newdescriptor.isEventCancelled = true;

                                } else {
                                    ERR(("Not supported value for 'segmentation_cancel_indicator': {}", sci));
                                }
                            }
                        }

                        if (d.contains("segmentation_duration")) {
                            newdescriptor.segmentationDuration    = _get<uint64_t>(d, "segmentation_duration", newdescriptor.segmentationDuration);
                            newdescriptor.hasSegmentationDuration = true;
                        }

                        newdescriptor.upidType = _get<uint8_t>(d, "segmentation_upid_type", newdescriptor.upidType);

                        if (d.contains("segmentation_upid")) {
                            // The UPID is stored as the literal ASCII text supplied to the
                            // REST layer, which validates it. Its characters are the UPID
                            // bytes: no hex decoding, no prefix stripping, no pair parsing.
                            std::string upidString = _get<std::string>(d, "segmentation_upid", "");

                            if (! upidString.empty()) {
                                newdescriptor.upid.assign(upidString.begin(), upidString.end());
                            }
                        }

                        newdescriptor.segmentationTypeId = _get<uint8_t>(d, "segmentation_type_id", newdescriptor.segmentationTypeId);
                        newdescriptor.segmentNum         = _get<uint8_t>(d, "segment_num",          newdescriptor.segmentNum);
                        newdescriptor.segmentsExpected   = _get<uint8_t>(d, "segments_expected",    newdescriptor.segmentsExpected);

                        newdescriptor.subSegmentNum       = _get<uint8_t>(d, "sub_segment_num",       newdescriptor.subSegmentNum);
                        newdescriptor.subSegmentsExpected = _get<uint8_t>(d, "sub_segments_expected", newdescriptor.subSegmentsExpected);

                        descriptors.push_back(newdescriptor);
                    }
                }
            }

            return std::make_unique<utpl::transcoder::EventTimeSignal>(
                id, startTs, std::move(descriptors), _getRawSection(time)
            );
        }
    }

    return nullptr;
}

std::unique_ptr<utpl::transcoder::EventSpliceInsert> app::SqliteEventProvider::_parseSpliceInsert(const EventModel &model) {
    auto id = model.getId();
    auto startTs = model.getTime();

    auto &details = model.getDetails();
    if (details.contains("scte35_spliceinsert")) {
        auto &splice = details["scte35_spliceinsert"];

        if (! splice.is_null()) {
            utpl::transcoder::EventSpliceInsert::Segmentation segmentation;

            {
                // Literal ASCII, as the REST layer validates and stores it: these
                // characters are the UPID bytes.
                auto upid = _get<std::string>(splice, "segmentation_upid", "");

                if (! upid.empty()) {
                    segmentation.upid.assign(upid.begin(), upid.end());

                    segmentation.upidType =
                        _get<uint8_t>(splice, "segmentation_upid_type", segmentation.upidType);
                    segmentation.segmentationTypeId =
                        _get<uint8_t>(splice, "segmentation_type_id", segmentation.segmentationTypeId);
                }
            }

            return std::make_unique<utpl::transcoder::EventSpliceInsert>(
                id, startTs,
                _get<uint32_t>(splice, "splice_event_id", 0),
                _get<utpl::Timestamp>(splice, "duration", 0),
                // Defaults to true: that is what the scheduler hardcoded before the field
                // existed, so an event authored without it keeps its old behaviour.
                _get<bool>(splice, "auto_return", true),
                std::move(segmentation),
                _getRawSection(splice)
            );
        }
    }

    return nullptr;
}

std::unique_ptr<utpl::transcoder::EventReturnToNetwork> app::SqliteEventProvider::_parseReturnToNetwork(const EventModel &model) {
    auto id = model.getId();
    auto startTs = model.getTime();

    auto &details = model.getDetails();
    if (details.contains("scte35_returntonetwork")) {
        auto &splice = details["scte35_returntonetwork"];

        if (! splice.is_null()) {
            return std::make_unique<utpl::transcoder::EventReturnToNetwork>(
                id, startTs,
                _get<uint32_t>(splice, "splice_event_id", 0),
                _getRawSection(splice)
            );
        }
    }

    return nullptr;
}
