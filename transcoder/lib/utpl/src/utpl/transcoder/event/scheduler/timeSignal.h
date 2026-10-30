#ifndef __UTPL_TRANSCODER_EVENT_SCHEDULER_TIMESIGNAL_H__
#define __UTPL_TRANSCODER_EVENT_SCHEDULER_TIMESIGNAL_H__

#include "utpl/transcoder/event/scheduler.h"

namespace utpl {
    namespace transcoder {
        class EventScheduler::ScheduledTimeSignalEvent : public EventScheduler::ScheduledEvent {
            public:
                ScheduledTimeSignalEvent(
                    EventProvider *provider, 
                    Channel *channel, 
                    const EventTimeSignal &event,
                    utpl::Timestamp prerollTimeMs
                );
                ~ScheduledTimeSignalEvent();

                Event *getEvent() override;

                void handleStart() override;
                void handleStop() override;

                void update(Event *event) override;
                bool cancel() override;

            private:
                EventTimeSignal _event;
                pipeline::TimedCallbackId _prerollScteCallbackId;
                utpl::Timestamp _prerollTime;
                std::mutex _guard;
        };
    }
}

#endif /* __UTPL_TRANSCODER_EVENT_SCHEDULER_TIMESIGNAL_H__ */