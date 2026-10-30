#ifndef __UTPL_TRANSCODER_SCTE_EVENT_TRACKER_H__
#define __UTPL_TRANSCODER_SCTE_EVENT_TRACKER_H__

#include <mutex>
#include <vector>

#include "utpl/transcoder/types.h"
#include "utpl/transcoder/channel.h"

namespace utpl {
    namespace transcoder {
        // The rule set that decides which SCTE-35 events a tracker reacts to: which segmentation
        // type starts and ends a section, at which priority, and which delivery restrictions are
        // honored. Blackout, blanking and ad insertion all describe their triggers this way.
        using ScteTriggerParameters = BlackoutParameters;

        // Turns a stream of SCTE-35 sections into a single resolved on/off state.
        //
        // It owns the whole event lifecycle: it reads splice_insert commands and segmentation
        // descriptors, schedules a start at the splice point and a stop after the signalled
        // duration on the channel clock, applies cancellation and duration updates, and resolves
        // overlapping events by segmentation priority.
        //
        // A tracker may be fed from more than one rule set. Those rule sets then share one event
        // list and one priority resolution, which is what blanking and blackout have always done:
        // an event scheduled by either of them competes with the other on priority.
        //
        // Subclasses supply the action in onStateResolved().
        class ScteEventTracker {
            protected:
                static constexpr uint64_t EVENT_ID_NOT_SET = -1ULL;

                struct Event {
                    uint64_t                     id;
                    BlackoutSegmentationPriority priority;
                    Timestamp                    startTime;
                    Timestamp                    duration;
                    pipeline::TimedCallbackId    startCallback;
                    pipeline::TimedCallbackId    stopCallback;
                    bool                         shouldApply;

                    Event(
                        uint64_t id,
                        BlackoutSegmentationPriority priority,
                        Timestamp startTime,
                        Timestamp duration,
                        bool shouldApply
                    ) {
                        this->id          = id;
                        this->priority    = priority;
                        this->startTime   = startTime;
                        this->duration    = duration;
                        this->shouldApply = shouldApply;

                        this->startCallback = nullptr;
                        this->stopCallback  = nullptr;
                    }

                    bool isPending() const {
                        return this->startCallback != nullptr;
                    }

                    bool isActive() const {
                        return this->startCallback == nullptr;
                    }
                };

            public:
                ScteEventTracker(Channel *channel);
                virtual ~ScteEventTracker();

            protected:
                // Feeds one section through one rule set.
                void handleEvent(
                    const ScteTriggerParameters &params,
                    const Scte35Data &scteData
                );

                // Reports the resolved state after every change to the event list. It is called
                // on each evaluation, not only on transitions, so an implementation must be
                // idempotent. `eventId` and `priority` describe the winning event when active.
                virtual void onStateResolved(
                    bool active,
                    uint64_t eventId,
                    BlackoutSegmentationPriority priority
                ) = 0;

                Channel *getChannel() {
                    return this->_channel;
                }

                // By default a signalled break_duration / segmentation_duration also ends the
                // event. Ad insertion switches this off: a break there is closed by an explicit
                // cue-in and by nothing else, so a duration in the cue-out is descriptive only.
                // An end cue still schedules its stop, because that path is driven by the cue
                // rather than by the signalled duration.
                void setIgnoreSignalledDuration(bool ignore) {
                    this->_ignoreSignalledDuration = ignore;
                }

                // Upper bound for an event that nothing in the signal would ever end. It is
                // scheduled through the same stop path as a signalled duration, so a later end
                // cue simply replaces it. Zero disables the bound.
                void setMaxEventDuration(utpl::Timestamp duration) {
                    this->_maxEventDuration = duration;
                }

                // True while at least one event is scheduled or running. Lets an implementation
                // tell "nothing signalled" apart from "signalled, but resolved to inactive".
                bool hasEvents();

            private:
                Event *_scheduleEvent(Event *event);
                void _removeEvent(Event *event);

                void _updateState();

                static utpl::Timestamp _durationToTimestamp(utpl::Timestamp duration90Khz);
                static utpl::Timestamp _getEventStartTime(const Scte35Data &data, uint64_t eventId, GstClockTime baseTime);

                void _scheduleEventStart(Event *event);
                void _scheduleEventStop(Event *event);
                void _scheduleEventStopAt(Event *event, utpl::Timestamp stopTime);

            private:
                Channel *_channel;

                bool            _ignoreSignalledDuration = false;
                utpl::Timestamp _maxEventDuration = 0;

                std::vector<Event>   _events;
                std::recursive_mutex _eventsGuard;
        };
    }
}

#endif /* __UTPL_TRANSCODER_SCTE_EVENT_TRACKER_H__ */
