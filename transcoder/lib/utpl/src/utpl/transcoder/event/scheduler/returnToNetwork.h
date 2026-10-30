#ifndef __UTPL_TRANSCODER_EVENT_SCHEDULER_RETURNTONETWORK_H__
#define __UTPL_TRANSCODER_EVENT_SCHEDULER_RETURNTONETWORK_H__

#include "utpl/transcoder/event/scheduler.h"

namespace utpl {
    namespace transcoder {
        class EventScheduler::ScheduledReturnToNetworkEvent : public EventScheduler::ScheduledEvent {
            public:
                ScheduledReturnToNetworkEvent(
                    EventProvider *provider, 
                    Channel *channel, 
                    const EventReturnToNetwork &event,
                    utpl::Timestamp prerollTimeMs
                );
                ~ScheduledReturnToNetworkEvent();

                Event *getEvent() override;

                void handleStart() override;
                void handleStop() override;

                void update(Event *event) override;
                bool cancel() override;

            private:
                EventReturnToNetwork _event;
                pipeline::TimedCallbackId _prerollScteCallbackId;
                utpl::Timestamp _prerollTime;
                std::mutex _guard;
        };
    }
}

#endif /* __UTPL_TRANSCODER_EVENT_SCHEDULER_RETURNTONETWORK_H__ */