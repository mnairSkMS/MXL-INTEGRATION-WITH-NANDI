#ifndef __UTPL_TRANSCODER_EVENT_SCHEDULER_OVERLAY_H__
#define __UTPL_TRANSCODER_EVENT_SCHEDULER_OVERLAY_H__

#include <mutex>

#include "utpl/transcoder/event/scheduler.h"

namespace utpl {
    namespace transcoder {
        class EventScheduler::ScheduledOverlayEvent : public EventScheduler::ScheduledEvent {
            public:
                ScheduledOverlayEvent(
                    EventProvider *provider, 
                    Channel *channel, 
                    const EventOverlay &event
                );

                ~ScheduledOverlayEvent();

                Event *getEvent() override;

                void handleStart() override;
                void handleStop() override;

                void update(Event *event) override;
                bool cancel() override;

            private:
                void _start();

            private:
                EventOverlay event;

                utpl::pipeline::TimedCallbackId fadeOutTimerId;

                std::shared_ptr<utpl::transcoder::ImageInputStream> stream;
                std::shared_ptr<utpl::transcoder::Compositor::Layer> overlay;
                std::mutex startGuard;
        };
    }
}

#endif /* __UTPL_TRANSCODER_EVENT_SCHEDULER_OVERLAY_H__ */