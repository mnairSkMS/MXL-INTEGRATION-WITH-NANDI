#include "utpl/common/debug.h"

#include "squeeze.h"

using namespace utpl;


transcoder::EventScheduler::ScheduledSqueezeEvent::ScheduledSqueezeEvent(
    EventProvider *provider, 
    Channel *channel, 
    const EventSqueeze &event, 
    const PredefinedSqueezes *squeezeConfig
) : ScheduledEvent(
    provider, channel, Channel::TimedCallbackSource::COMPOSITOR, false, event.getFadeOutDuration() > 0
), event(event) {
    this->squeezeConfig = squeezeConfig;
}

transcoder::Event *transcoder::EventScheduler::ScheduledSqueezeEvent::getEvent() {
    return &this->event;
}

void transcoder::EventScheduler::ScheduledSqueezeEvent::handleStart() {
    utpl::NormRect dstWindow = ORIGINAL_WINDOW_NORM;
    utpl::NormRect srcWindow = ORIGINAL_WINDOW_NORM;

    auto &name  = event.getName();
    bool  known = true;

    {
        if (name.empty()) {
            LOG(("A custom squeeze event is being used - since the predefined squeeze name is empty, the window configuration will be derived from the event."));

            srcWindow = event.getSrcWindow();
            dstWindow = event.getDstWindow();

        } else {
            auto *params = this->squeezeConfig->getSqueezeParametersByName(name);
            if (! params) {
                ERR(("Transcoder configuration does not contain predefined squeeze of name '{}'", name));

                logMissed(this->event, fmt::format("no predefined squeeze named '{}' in the engine config", name));

                known = false;

            } else {
                srcWindow = params->getSrcWindow();
                dstWindow = params->getDstWindow();
            }
        }
    }

    this->getChannel()->squeeze(srcWindow, dstWindow, event.getFadeInDuration());

    if (known) {
        this->fired = true;

        // The squeeze is driven from a compositor-timed callback, so this is when the window
        // starts moving; it reaches its target fade_in_ms later.
        logFired(this->event, utpl::Time::getUTCTimestamp(), this->getCurrentRunningTime(), fmt::format(
            "squeeze=\"{}\" fade_in_ms={} duration_ms={}", name.empty() ? "custom" : name, event.getFadeInDuration(), event.getDuration()
        ));
    }
}

void transcoder::EventScheduler::ScheduledSqueezeEvent::handleStop() {
    this->getChannel()->squeeze(ORIGINAL_WINDOW_NORM, ORIGINAL_WINDOW_NORM, event.getFadeOutDuration());

    if (this->fired) {
        logEnded(this->event, utpl::Time::getUTCTimestamp(), this->getCurrentRunningTime(),
            fmt::format("fade_out_ms={}", event.getFadeOutDuration())
        );
    }

    this->setState(State::HANDLED);
}

void transcoder::EventScheduler::ScheduledSqueezeEvent::update(Event *event) {
    
}

bool transcoder::EventScheduler::ScheduledSqueezeEvent::cancel() {
    return ScheduledEvent::cancel();
}