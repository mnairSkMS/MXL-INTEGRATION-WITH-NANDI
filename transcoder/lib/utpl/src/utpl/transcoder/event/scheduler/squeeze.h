#ifndef __UTPL_TRANSCODER_EVENT_SCHEDULER_SQUEEZE_H__
#define __UTPL_TRANSCODER_EVENT_SCHEDULER_SQUEEZE_H__

#include "utpl/transcoder/event/scheduler.h"

namespace utpl {
    namespace transcoder {
        class EventScheduler::ScheduledSqueezeEvent : public EventScheduler::ScheduledEvent {
            public:
                ScheduledSqueezeEvent(
                    EventProvider *provider, 
                    Channel *channel, 
                    const EventSqueeze &event, 
                    const PredefinedSqueezes *squeezeConfig
                );

                Event *getEvent() override;

                void handleStart() override;
                void handleStop() override;

                void update(Event *event) override;
                bool cancel() override;

            private:
                EventSqueeze event;
                const PredefinedSqueezes *squeezeConfig;
                bool fired = false;
        };
    }
}

#endif /* __UTPL_TRANSCODER_EVENT_SCHEDULER_SQUEEZE_H__ */