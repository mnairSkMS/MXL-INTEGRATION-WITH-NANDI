#include <tsduck/tsduck.h>
#include <tsduck/tsSpliceSegmentationDescriptor.h>

#include <gst/mpegts/gst-scte-section.h>

#include "utpl/common/utils/gstreamer.h"
#include "utpl/common/debug.h"

#include "utpl/transcoder/scte/eventTracker.h"

using namespace utpl;


static std::string _eventTypeToString(uint8_t type) {
    switch (type) {
        case GST_MTS_SCTE_SPLICE_COMMAND_INSERT:    return "SPLICE_INSERT";
        case GST_MTS_SCTE_SPLICE_COMMAND_BANDWIDTH: return "BANDWIDTH_RESERVATION";
        case GST_MTS_SCTE_SPLICE_COMMAND_NULL:      return "SPLICE_NULL";
        case GST_MTS_SCTE_SPLICE_COMMAND_PRIVATE:   return "PRIVATE_COMMAND";
        case GST_MTS_SCTE_SPLICE_COMMAND_SCHEDULE:  return "SPLICE_SCHEDULE";
        case GST_MTS_SCTE_SPLICE_COMMAND_TIME:      return "TIME_SIGNAL";
        default:
            return "UNKNOWN";
    }
}


transcoder::ScteEventTracker::ScteEventTracker(Channel *channel) {
    this->_channel = channel;
}

transcoder::ScteEventTracker::~ScteEventTracker() {
    std::unique_lock eventLock(this->_eventsGuard);

    // Timed callbacks hold a raw pointer back into this object, so none of them may outlive it.
    for (auto &ev : this->_events) {
        if (ev.startCallback) {
            this->_channel->cancelTimedCallback(ev.startCallback);
        }

        if (ev.stopCallback) {
            this->_channel->cancelTimedCallback(ev.stopCallback);
        }
    }

    this->_events.clear();
}

bool transcoder::ScteEventTracker::hasEvents() {
    std::unique_lock eventLock(this->_eventsGuard);

    return ! this->_events.empty();
}

void transcoder::ScteEventTracker::_updateState() {
    BlackoutSegmentationPriority eventPriority = BLACKOUT_SEGMENTATION_PRIORITY_MAX;
    uint32_t                     eventId       = 0;

    bool active = false;

    DBG(("Updating SCTE event state"));

    for (auto &ev : this->_events) {
        if (ev.isActive()) {
            if (ev.priority >= eventPriority) {
                if (ev.priority > eventPriority) {
                    eventPriority = ev.priority;

                    active = false;
                }

                if (ev.shouldApply) {
                    eventId = ev.id;
                    active  = true;
                }
            }
        }
    }

    if (active) {
        DBG(("SCTE event state is active due to event ID: {}, priority {}", eventId, eventPriority));
    }

    this->onStateResolved(active, eventId, eventPriority);
}

void transcoder::ScteEventTracker::_removeEvent(Event *event) {
    DBG(("Cancelling and removing event ID {}", event->id));

    if (event->startCallback) {
        this->_channel->cancelTimedCallback(event->startCallback);

        event->startCallback = nullptr;
    }

    if (event->stopCallback) {
        this->_channel->cancelTimedCallback(event->stopCallback);

        event->stopCallback = nullptr;
    }

    for (auto it = this->_events.begin(); it != this->_events.end(); it++) {
        if (it->id == event->id) {
            this->_events.erase(it);

            this->_updateState();
            break;
        }
    }

    DBG(("Number of active events after removal {}", this->_events.size()));
}

void transcoder::ScteEventTracker::_scheduleEventStart(Event *event) {
    event->startCallback = this->_channel->addTimedCallback(
        event->startTime, 0, Channel::TimedCallbackSource::PROCESSOR, [this](pipeline::TimedCallbackId id, GstClockTime time) {
            std::unique_lock lock(this->_eventsGuard);

            std::vector<Event>::iterator evIt;

            for (evIt = this->_events.begin(); evIt != this->_events.end(); evIt++) {
                if (evIt->startCallback == id) {
                    break;
                }
            }

            if (evIt == this->_events.end()) {
                ERR(("Event has not been found for callbackID {}", (void *) id));

            } else {
                evIt->startCallback = nullptr;

                this->_updateState();
            }

            return false;
        }
    );
}

void transcoder::ScteEventTracker::_scheduleEventStop(Event *event) {
    utpl::Timestamp startTime = event->startTime ? event->startTime : utpl::Time::getUTCTimestamp();

    startTime += event->duration;

    this->_scheduleEventStopAt(event, startTime);
}

void transcoder::ScteEventTracker::_scheduleEventStopAt(Event *event, utpl::Timestamp stopTime) {
    if (event->stopCallback) {
        this->_channel->cancelTimedCallback(event->stopCallback);
    }

    event->stopCallback = this->_channel->addTimedCallback(
        stopTime, 0, Channel::TimedCallbackSource::PROCESSOR, [this](pipeline::TimedCallbackId id, GstClockTime time) {
            std::unique_lock lock(this->_eventsGuard);

            std::vector<Event>::iterator evIt;

            for (evIt = this->_events.begin(); evIt != this->_events.end(); evIt++) {
                if (evIt->stopCallback == id) {
                    break;
                }
            }

            if (evIt == this->_events.end()) {
                ERR(("Event has not been found for callbackID {}", (void *) id));

            } else {
                evIt->stopCallback = nullptr;

                this->_events.erase(evIt);

                this->_updateState();
            }

            return false;
        }
    );
}

transcoder::ScteEventTracker::Event *transcoder::ScteEventTracker::_scheduleEvent(Event *event) {
    bool refreshState = false;

    DBG(("Adding new entry with ID {} to monitored events list", event->id));

    if (event->startTime == 0) {
        DBG(("Starting event {} immediately.", event->id));

        refreshState = true;

    } else {
        this->_scheduleEventStart(event);
    }

    if (event->duration && ! this->_ignoreSignalledDuration) {
        this->_scheduleEventStop(event);

    } else if (this->_maxEventDuration) {
        // Nothing in the signal will end this event, so bound it. A later end cue reschedules
        // the same stop callback, so the bound never shortens a break that ends properly.
        auto base = event->startTime ? event->startTime : utpl::Time::getUTCTimestamp();

        DBG(("Event {} has no signalled end - bounding it to {} ms", event->id, this->_maxEventDuration));

        this->_scheduleEventStopAt(event, base + this->_maxEventDuration);
    }

    if (refreshState) {
        this->_updateState();
    }

    return event;
}

void transcoder::ScteEventTracker::handleEvent(
    const ScteTriggerParameters &params,
    const Scte35Data &scteData
) {
    auto *section = (GstMpegtsSection *) scteData.getSection();

    if (! section) {
        ERR(("Event section is null! - it should never happen. There is no information to process - skipping."));

    } else {
        auto *sit = gst_mpegts_section_get_scte_sit(section);

        std::unique_lock eventLock(this->_eventsGuard);

        bool honorWebDelivery;
        bool honorRegionalBlackout;

        switch (params.getRestrictionIgnore()) {
            case BlackoutRestrictionIgnoreMode::NONE:
                honorWebDelivery      = true;
                honorRegionalBlackout = true;
                break;

            case BlackoutRestrictionIgnoreMode::NO_REGIONAL_BLACKOUT:
                honorWebDelivery      = true;
                honorRegionalBlackout = false;
                break;

            case BlackoutRestrictionIgnoreMode::WEB_DELIVERY_ALLOWED:
                honorWebDelivery      = false;
                honorRegionalBlackout = true;
                break;

            default:
                {
                    ERR(("Unsupported ignore mode specified in parameters! {} - forcing all flags to be honored", params.getRestrictionIgnore()));

                    honorWebDelivery      = true;
                    honorRegionalBlackout = true;
                }
                break;
        }

        do {
            std::vector<ts::SpliceSegmentationDescriptor> segmentationDescriptors;

            const BlackoutCondition *conditions = nullptr;

            uint64_t eventId = -1ull;

            // Collect segmentation descriptors
            if (sit->descriptors) {
                ts::DuckContext duckCtx;

                for (int i = 0; i < sit->descriptors->len; i++) {
                    auto *gstDescriptor = reinterpret_cast<GstMpegtsDescriptor *>(g_ptr_array_index(sit->descriptors, i));

                    if (gstDescriptor->tag == ts::DID_SPLICE_SEGMENT) {
                        ts::Descriptor genericDescriptor(gstDescriptor->data, gstDescriptor->length + 2);

                        if (! genericDescriptor.isValid()) {
                            ERR(("Descriptor at {} position of event type {} does not contain a valid descriptor!", 
                                i, _eventTypeToString(sit->splice_command_type)
                            ));

                        } else {
                            ts::SpliceSegmentationDescriptor segmentationDescriptor(duckCtx, genericDescriptor);
                            
                            if (segmentationDescriptor.isValid()) {
                                segmentationDescriptors.push_back(segmentationDescriptor);
                            }
                        }
                    }
                }
            }

            DBG(("Event of type {} has {} segmentation descriptors (total descriptors: {})",
                _eventTypeToString(sit->splice_command_type), segmentationDescriptors.size(), sit->descriptors->len
            ));

            GstClockTime baseTime = scteData.getRunningTime();

            
            {
                auto *processor = this->_channel->getVideoProcessor();
                if (processor) {
                    auto compositor = processor->getCompositor();
                    if (compositor) {
                        baseTime = compositor->getVideoLayer()->getRunningTime();
                    }
                }
            }

            // Check for cancelment
            switch (sit->splice_command_type) {
                case GST_MTS_SCTE_SPLICE_COMMAND_INSERT:
                    {
                        conditions = &params.getConditions().getSpliceInsertCondition();

                        auto *splice = reinterpret_cast<GstMpegtsSCTESpliceEvent *>(g_ptr_array_index(sit->splices, 0));

                        if (
                            segmentationDescriptors.empty() &&
                            ! conditions->shouldBlankWhenNoDescriptors()
                        ) {
                            LOG(("Event of type {} has no segmentation descriptors and such events are not supported by current configuration - skipping",
                                _eventTypeToString(sit->splice_command_type)
                            ));

                            break;
                        }

                        eventId = splice->splice_event_id;

                        // Verify if any segmentation descriptor relates to splice
                        if (! segmentationDescriptors.empty()) {
                            bool anyRelates = false;

                            for (auto &desc : segmentationDescriptors) {
                                if (desc.segmentation_event_id == splice->splice_event_id) {
                                    anyRelates = true;
                                    break;
                                }
                            }

                            if (! anyRelates) {
                                WARN(("The case when {} event has segmentation descriptors that not relate to splice is not supported!",
                                    _eventTypeToString(sit->splice_command_type)
                                ));

                                break;

                            } else {
                                LOG(("Received event of type {}, skipping splice section - segmentation descriptors will be analysed instead",
                                    _eventTypeToString(sit->splice_command_type)
                                ));

                                break;
                            }
                        }

                        // Handle legacy event
                        {
                            Event *currentEvent = nullptr;

                            for (auto &ev : this->_events) {
                                if (ev.id == splice->splice_event_id) {
                                    currentEvent = &ev;
                                    break;
                                }
                            }

                            // Legacy event was cancelled
                            if (splice->splice_event_cancel_indicator) {
                                if (currentEvent) {
                                    LOG(("Event of type {}, ID {} was cancelled", 
                                        _eventTypeToString(sit->splice_command_type), splice->splice_event_id
                                    ));

                                    _removeEvent(currentEvent);

                                } else {
                                    LOG(("Event of type {} is cancelled but there is no currently active event with ID: {}", 
                                        _eventTypeToString(sit->splice_command_type), splice->splice_event_id
                                    ));
                                }

                            // Legacy event schedule or update
                            } else {
                                if (currentEvent) {
                                    if (splice->out_of_network_indicator) {
                                        Timestamp newDuration  = 0;
                                        Timestamp newStartTime = Time::getUTCTimestamp();

                                        LOG(("Event of type {} (OUT) is already scheduled and an another OUT event of ID {} occurred - update purpose",
                                            _eventTypeToString(sit->splice_command_type), splice->splice_event_id
                                        ));

                                        if (splice->duration_flag && splice->break_duration_auto_return) {
                                            newDuration = _durationToTimestamp(splice->break_duration);
                                        }

                                        if (splice->program_splice_time_specified) {
                                            newStartTime = _getEventStartTime(scteData, eventId, baseTime);
                                        }

                                        {
                                            bool reschedule = false;

                                            if (currentEvent->isActive()) {
                                                newStartTime = 0;

                                                if (currentEvent->duration != newDuration) {
                                                    reschedule = true;
                                                }

                                            } else {
                                                if (
                                                    (currentEvent->startTime != newStartTime) ||
                                                    (currentEvent->duration  != newDuration)
                                                ) {
                                                    reschedule = true;
                                                }
                                            }

                                            if (reschedule) {
                                                _removeEvent(currentEvent);
                                                
                                                DBG(("The currently scheduled event of type {}, ID: {} has changed - rescheduling",
                                                    _eventTypeToString(sit->splice_command_type), splice->splice_event_id
                                                ));

                                                currentEvent = this->_scheduleEvent(
                                                    &this->_events.emplace_back(Event(
                                                        splice->splice_event_id,
                                                        BLACKOUT_SEGMENTATION_PRIORITY_MIN, 
                                                        newStartTime,
                                                        newDuration,
                                                        true
                                                    ))
                                                );

                                            } else {
                                                DBG(("The currently scheduled event of type {}, ID: {} has not changed - skipping rescheduling",
                                                    _eventTypeToString(sit->splice_command_type), splice->splice_event_id
                                                ));
                                            }
                                        }

                                    } else {
                                        Timestamp currentTime = Time::getUTCTimestamp();
                                        Timestamp stopTime    = currentTime;
                                        
                                        if (splice->program_splice_time_specified) {
                                            stopTime = _getEventStartTime(scteData, eventId, baseTime);
                                        }

                                        if (stopTime == currentTime) {
                                            _removeEvent(currentEvent);    

                                        } else {
                                            // Fake duration
                                            currentEvent->duration = stopTime - currentEvent->startTime;

                                            this->_scheduleEventStop(currentEvent);
                                        }
                                    }

                                } else {
                                    if (splice->out_of_network_indicator) {
                                        Timestamp duration = 0;

                                        if (splice->duration_flag && splice->break_duration_auto_return) {
                                            duration = _durationToTimestamp(splice->break_duration);
                                        }

                                        currentEvent = this->_scheduleEvent(
                                            &this->_events.emplace_back(Event(
                                                eventId,
                                                BLACKOUT_SEGMENTATION_PRIORITY_MIN, 
                                                _getEventStartTime(scteData, eventId, baseTime),
                                                duration,
                                                true
                                            ))
                                        );
                                    }
                                }
                            }            
                        }
                    }
                    break;

                case GST_MTS_SCTE_SPLICE_COMMAND_TIME:
                    {
                        conditions = &params.getConditions().getTimeSignalCondition();

                        if (segmentationDescriptors.empty()) {
                            WARN(("Received event of type {} which has no segmentation descriptors - this is abnormal situation!",
                                _eventTypeToString(sit->splice_command_type)
                            ));
                        }
                    }
                    break;

                default:
                    {
                        LOG(("Received not supported by the module event type: {}", _eventTypeToString(sit->splice_command_type)));
                    }
            }

            if (! conditions) {
                break;
            }

            // Handle descriptors
            for (const auto &descr : segmentationDescriptors) {
                Event *currentEvent = nullptr;

                // Find corresponding event
                for (auto &ev : this->_events) {
                    if (ev.id == descr.segmentation_event_id) {
                        currentEvent = &ev;
                        break;
                    }
                }

                // Check for event cancellation
                if (descr.segmentation_event_cancel) {
                    if (currentEvent) {
                        _removeEvent(currentEvent);

                    } else {
                        LOG(("Reveived segmentation descriptor with segmentation_event_cancel flag set but there is no currently active event with ID: {}",
                            descr.segmentation_event_id
                        ));
                    }

                } else {
                    bool isStart = false;
                    bool isEnd   = false;
                    const BlackoutSegment *segmentCond = nullptr;

                    for (auto &cond : conditions->getSegments()) {
                        if (descr.segmentation_type_id == cond.getTypeOfBegin()) {
                            isStart     = true;
                            segmentCond = &cond;

                        } else if (descr.segmentation_type_id == cond.getTypeOfEnd()) {
                            isEnd       = true;
                            segmentCond = &cond;
                        }

                        if (segmentCond) {
                            break;
                        }
                    }

                    if (isStart ^ isEnd) {
                        // Detected start or end of section.
                        if (currentEvent) {
                            if (isEnd) {
                                Timestamp stopTime    = _getEventStartTime(scteData, eventId, baseTime);
                                Timestamp currentTime = Time::getUTCTimestamp();

                                if (stopTime == currentTime) {
                                    _removeEvent(currentEvent);    

                                } else {
                                    // Fake duration
                                    currentEvent->duration = stopTime - currentEvent->startTime;

                                    this->_scheduleEventStop(currentEvent);
                                }

                            } else {
                                utpl::Timestamp newDuration = 0;

                                // Repeated start
                                DBG(("Have pending ({}) or active ({}) event with ID {}. Checking descriptor for update/break operation.",
                                    currentEvent->isPending(), currentEvent->isActive(), currentEvent->id
                                ));

                                if (descr.segmentation_duration.has_value()) {
                                    newDuration = _durationToTimestamp(descr.segmentation_duration.value());
                                }

                                if (currentEvent->duration != newDuration) {
                                    GstClockTime duration  = 0;

                                    if (descr.segmentation_duration.has_value()) {
                                        duration = _durationToTimestamp(descr.segmentation_duration.value());
                                    }

                                    LOG(("Event ID: {} duration has changed ({} -> {}) - rescheduling it!", 
                                        descr.segmentation_event_id, currentEvent->duration, duration
                                    ));

                                    _removeEvent(currentEvent);

                                    currentEvent = this->_scheduleEvent(
                                        &this->_events.emplace_back(Event(
                                            descr.segmentation_event_id,
                                            segmentCond->getPriority(), 
                                            _getEventStartTime(scteData, eventId, baseTime),
                                            duration,

                                            (honorRegionalBlackout && ! descr.no_regional_blackout) ||
                                            (honorWebDelivery      && ! descr.web_delivery_allowed)
                                        ))
                                    );
                                }
                            }

                        } else {
                            // New event
                            if (isEnd) {
                                LOG(("Received event of ID {}, containing segmentation descriptor of type ID: {} which signals END of section but there is no active segment!",
                                    descr.segmentation_event_id, descr.segmentation_type_id
                                ));

                            } else {
                                GstClockTime duration  = 0;

                                if (descr.segmentation_duration.has_value()) {
                                    duration = _durationToTimestamp(descr.segmentation_duration.value());
                                }

                                this->_scheduleEvent(
                                    &this->_events.emplace_back(Event(
                                        descr.segmentation_event_id,
                                        segmentCond->getPriority(), 
                                        _getEventStartTime(scteData, eventId, baseTime),
                                        duration,

                                        (honorRegionalBlackout && ! descr.no_regional_blackout) ||
                                        (honorWebDelivery      && ! descr.web_delivery_allowed)
                                    ))
                                );
                            }
                        }

                    } else {
                        LOG(("Configuration does not contain segmentation description for segmentation_type_id={}, skipping", descr.segmentation_type_id));
                    }
                }
            }

        } while (0);
    }
}

utpl::Timestamp transcoder::ScteEventTracker::_durationToTimestamp(utpl::Timestamp duration90Khz) {
    return GST_TIME_AS_MSECONDS(
        GST_UTILS_MPEGTIME_TO_GSTTIME(duration90Khz)
    );
}

utpl::Timestamp transcoder::ScteEventTracker::_getEventStartTime(const Scte35Data &data, uint64_t eventId, GstClockTime baseTime) {
    utpl::Timestamp ret = utpl::Time::getUTCTimestamp();

    {
        auto *sit = gst_mpegts_section_get_scte_sit((GstMpegtsSection *) data.getSection());

        if (eventId != -1ull) {
            if (sit->splices->len) {
                auto *event = reinterpret_cast<GstMpegtsSCTESpliceEvent *>(g_ptr_array_index(sit->splices, 0));

                if (! event->program_splice_time_specified) {
                    LOG(("Have an immediate splice_insert event"));    

                } else {
                    pipeline::ScteRunningTimeMap map(data.getTimingMap());
                    
                    ret += GST_TIME_AS_MSECONDS(map.getSpliceEventTime(eventId) - baseTime);
                }
            }

        } else {
            if (! sit->splice_time_specified) {
                LOG(("Have an immediate time_signal event"));

            } else {
                pipeline::ScteRunningTimeMap map(data.getTimingMap());

                ret += GST_TIME_AS_MSECONDS(map.getTimeSignalTime() - baseTime);
            }
        }
    }

    return ret;
}
