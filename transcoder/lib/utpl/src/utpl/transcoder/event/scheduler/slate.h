#ifndef __UTPL_TRANSCODER_EVENT_SCHEDULER_SLATE_H__
#define __UTPL_TRANSCODER_EVENT_SCHEDULER_SLATE_H__

#include <mutex>

#include "utpl/transcoder/event/scheduler.h"

namespace utpl {
    namespace transcoder {
        class EventScheduler::ScheduledSlateEvent : public EventScheduler::ScheduledEvent {
            public:
                ScheduledSlateEvent(
                    EventProvider *provider, 
                    Channel *channel, 
                    const EventSlate &event
                );

                ~ScheduledSlateEvent();

                Event *getEvent() override;

                void handleStart() override;
                void handleStop() override;

                void update(Event *event) override;
                bool cancel() override;

            private:
                void _start();

            private:
                EventSlate event;

                std::shared_ptr<utpl::transcoder::Input> input;
                std::shared_ptr<utpl::transcoder::Input> originalInput;

                // Held only while a slate whose media carries no audio is on air. Releasing
                // is the destructor's job, so no code path can leak the mute.
                Channel::AudioBlankHold audioBlank;

                bool inputProbed = false;
                bool inputAdded  = false;
                bool missedLogged = false;
                std::recursive_mutex startGuard;
        };
    }
}

#endif /* __UTPL_TRANSCODER_EVENT_SCHEDULER_SLATE_H__ */
