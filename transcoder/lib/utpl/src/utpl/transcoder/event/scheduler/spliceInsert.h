#ifndef __UTPL_TRANSCODER_EVENT_SCHEDULER_SPLICEINSERT_H__
#define __UTPL_TRANSCODER_EVENT_SCHEDULER_SPLICEINSERT_H__

#include "utpl/transcoder/event/scheduler.h"

namespace utpl {
    namespace transcoder {
        class EventScheduler::ScheduledSpliceInsertEvent : public EventScheduler::ScheduledEvent {
            public:
                ScheduledSpliceInsertEvent(
                    EventProvider *provider, 
                    Channel *channel, 
                    const EventSpliceInsert &event,
                    utpl::Timestamp prerollTimeMs
                );
                ~ScheduledSpliceInsertEvent();

                Event *getEvent() override;

                void handleStart() override;
                void handleStop() override;

                void update(Event *event) override;
                bool cancel() override;

            private:
                EventSpliceInsert _event;
                pipeline::TimedCallbackId _prerollScteCallbackId;
                utpl::Timestamp _prerollTime;
                std::mutex _guard;
        };
    }
}

#endif /* __UTPL_TRANSCODER_EVENT_SCHEDULER_SPLICEINSERT_H__ */