#include <algorithm>

#include "utpl/logger.h"

#include "utpl/common/time.h"

#include "utpl/common/debug.h"
#include "utpl/common/types/exception.h"

#include "utpl/transcoder/stream/input/image.h"
#include "utpl/transcoder/event/scheduler.h"

#include "scheduler/squeeze.h"
#include "scheduler/overlay.h"
#include "scheduler/spliceInsert.h"
#include "scheduler/timeSignal.h"
#include "scheduler/returnToNetwork.h"
#include "scheduler/slate.h"

using namespace utpl;

#define NEW_EVENT_CHECK_TIME_WINDOW_MS (15 * 1000)
#define EVENT_CHECK_INTERVAL_MS (500)

static auto &logger = Logger::getLogger(UTPL_LOGGER_NAME_SCHEDULE);

transcoder::EventScheduler::ScheduledEvent::ScheduledEvent(
    EventProvider *provider, Channel *channel, Channel::TimedCallbackSource src, bool asyncStart, bool asyncStop
) {
    this->provider = provider;
    this->channel  = channel;

    this->asyncStart = asyncStart;
    this->asyncStop  = asyncStop;
    
    this->startCallbackId = nullptr;
    this->stopCallbackId  = nullptr;
    this->timedSource     = src;

    this->state = State::INIT;
}

static std::string _formatWallClock(utpl::Timestamp timestamp) {
    // ISO 8601 in UTC, so the value stays one whitespace-free token.
    auto text = utpl::Time::toSqliteDateTime(timestamp);

    std::replace(text.begin(), text.end(), ' ', 'T');

    return text + "Z";
}

static std::string _formatRunningTime(GstClockTime time) {
    if (! GST_CLOCK_TIME_IS_VALID(time)) {
        return "-";
    }

    const auto ms = GST_TIME_AS_MSECONDS(time);

    return fmt::format("{}:{:02}:{:02}.{:03}", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
}

const char *transcoder::EventScheduler::ScheduledEvent::typeName(const Event &event) {
    // The REST type names, not the C++ class names, so a log line matches GET /scheduled_events.
    if (dynamic_cast<const EventOverlay *>(&event))         return "image_overlay";
    if (dynamic_cast<const EventSqueeze *>(&event))         return "squeeze";
    if (dynamic_cast<const EventSlate *>(&event))           return "slate";
    if (dynamic_cast<const EventSpliceInsert *>(&event))    return "scte35_spliceinsert";
    if (dynamic_cast<const EventTimeSignal *>(&event))      return "scte35_timesignal";
    if (dynamic_cast<const EventReturnToNetwork *>(&event)) return "scte35_returntonetwork";

    return "event";
}

void transcoder::EventScheduler::ScheduledEvent::logFired(const Event &event, utpl::Timestamp at, GstClockTime runningTime, const std::string &detail) {
    logger.log("FIRED id={} type={} at={} running_time={}{}{}",
        event.getId(), typeName(event), _formatWallClock(at), _formatRunningTime(runningTime), detail.empty() ? "" : " ", detail
    );
}

void transcoder::EventScheduler::ScheduledEvent::logEnded(const Event &event, utpl::Timestamp at, GstClockTime runningTime, const std::string &detail) {
    logger.log("ENDED id={} type={} at={} running_time={}{}{}",
        event.getId(), typeName(event), _formatWallClock(at), _formatRunningTime(runningTime), detail.empty() ? "" : " ", detail
    );
}

void transcoder::EventScheduler::ScheduledEvent::logMissed(const Event &event, const std::string &reason) {
    logger.warning("MISSED id={} type={} at={} reason=\"{}\"",
        event.getId(), typeName(event), _formatWallClock(utpl::Time::getUTCTimestamp()), reason
    );
}

GstClockTime transcoder::EventScheduler::ScheduledEvent::getCurrentRunningTime() {
    auto compositor = this->getChannel()->getVideoCompositor();
    if (! compositor) {
        return GST_CLOCK_TIME_NONE;
    }

    return compositor->getVideoLayer()->getRunningTime();
}

transcoder::EventScheduler::ScheduledEvent::~ScheduledEvent() {
    std::lock_guard guard(this->mutex);

    if (this->startCallbackId) {
        this->getChannel()->cancelTimedCallback(this->startCallbackId);
    }

    if (this->stopCallbackId) {
        this->getChannel()->cancelTimedCallback(this->stopCallbackId);
    }
}

bool transcoder::EventScheduler::ScheduledEvent::_onEventStart() {
    Event *event = this->getEvent();

    logger.log("Event with ID {} is starting now (estimated start time {})", event->getId(), utpl::Time::toLocalString(event->getTimestamp()));

    {
        std::lock_guard guard(this->mutex);

        this->setState(State::STARTED);

        // dispatch stop timer
        if (event->getDuration() > 0) {
            this->stopCallbackId = this->getChannel()->addTimedCallback(
                event->getTimestamp() + event->getDuration(), 0, this->timedSource, [this, event](pipeline::TimedCallbackId id, GstClockTime time) {
                    {
                        std::lock_guard guard(this->mutex);

                        this->stopCallbackId = nullptr;
                    }

                    this->_onEventStop();
                    
                    return false;
                }
            );
        }
    }

    try {
        this->handleStart();
    } catch (const std::exception &ex) {
        this->error = ex.what();
    }

    if (event->getDuration() == 0 || ! this->error.empty()) {
        this->_onEventStop();
    }

    return false;
}

bool transcoder::EventScheduler::ScheduledEvent::_onEventStop() {
    Event *event = this->getEvent();

    if (this->asyncStop) {
        this->setState(State::STOPPING);
    }

    if (event->getDuration()) {
        logger.log("Event with ID {} is stopping now (estimated stop time {})", event->getId(), utpl::Time::toLocalString(event->getTimestamp() + event->getDuration()));

        try {
            this->handleStop();
        } catch (const std::exception &ex) {
            this->error = ex.what();
        }
    }

    this->provider->markProcessed(*event, this->error);

    if (! this->asyncStop) {
        this->setState(State::HANDLED);
    }

    return false;
}

void transcoder::EventScheduler::ScheduledEvent::prepare() {
    auto *event = this->getEvent();

    {
        std::lock_guard guard(this->mutex);

        this->setState(State::STARTING);

        this->startCallbackId = this->getChannel()->addTimedCallback(
            this->getEvent()->getTimestamp(), 0, this->timedSource, [this, event](pipeline::TimedCallbackId id, GstClockTime time) {
                {
                    std::lock_guard guard(this->mutex);

                    this->startCallbackId = nullptr;
                }

                this->_onEventStart();

                if (event->getDuration() == 0 || ! this->error.empty()) {
                    this->_onEventStop();
                }
                
                return false;
            }
        );
    }
}

bool transcoder::EventScheduler::ScheduledEvent::isHandled() const {
    std::lock_guard guard(this->mutex);

    return this->state == State::HANDLED;
}


bool transcoder::EventScheduler::ScheduledEvent::isInProgress() const {
    std::lock_guard guard(this->mutex);

    return this->state > State::INIT && this->state < State::HANDLED;
}


bool transcoder::EventScheduler::ScheduledEvent::isStopping() const {
    std::lock_guard guard(this->mutex);

    return this->state >= State::STOPPING && this->state < State::HANDLED;
}


transcoder::EventProvider *transcoder::EventScheduler::ScheduledEvent::getProvider() {
    return this->provider;
}


const transcoder::EventProvider *transcoder::EventScheduler::ScheduledEvent::getProvider() const {
    return this->provider;
}


transcoder::Channel *transcoder::EventScheduler::ScheduledEvent::getChannel() {
    return this->channel;
}


const transcoder::Channel *transcoder::EventScheduler::ScheduledEvent::getChannel() const {
    return this->channel;
}

void transcoder::EventScheduler::ScheduledEvent::setState(State state) {
    std::lock_guard guard(this->mutex);

    if (this->state != state) {
        this->state = state;
    }
}

void transcoder::EventScheduler::ScheduledEvent::update(Event *event) {

}

bool transcoder::EventScheduler::ScheduledEvent::cancel() {
    bool callStop = false;

    {
        std::lock_guard guard(this->mutex);

        if (this->startCallbackId) {
            this->getChannel()->cancelTimedCallback(this->startCallbackId);

            this->startCallbackId = nullptr;
        }

        if (this->stopCallbackId) {
            this->getChannel()->cancelTimedCallback(this->stopCallbackId);

            this->stopCallbackId = nullptr;
        }

        switch (this->state) {
            case State::INIT:
            case State::STARTING:
                this->setState(State::HANDLED);
                break;

            case State::STARTED:
                callStop = true;
                break;
        }
    }

    if (callStop) {
        this->_onEventStop();
    }

    return true;
}


bool transcoder::EventScheduler::_onCheckForNewEvent(EventScheduler *self) {
    // Remove handled and not existing events
    {
        auto it = self->_events.begin();

        while (it != self->_events.end()) {
            if ((*it)->isHandled()) {
                DBG(("Removing handled event {} from the schedule, {} remain", it->get()->getEvent()->getId(), self->_events.size() - 1));

                it = self->_events.erase(it);

            } else {
                ++it;
            }
        }
    }

    // Schedule new or update if required
    {
        utpl::Timestamp notBefore = utpl::Time::getUTCTimestamp();

        for (auto *provider : self->_providers) {
            auto events = provider->getNextEvents(notBefore - EVENT_CHECK_INTERVAL_MS * 2, notBefore + NEW_EVENT_CHECK_TIME_WINDOW_MS);

            for (auto &event : events) {
                // Check if event is already queued
                auto it = std::find_if(self->_events.begin(), self->_events.end(), [provider, &event](const std::unique_ptr<ScheduledEvent> &ev) {
                    const auto evPtr = ev->getEvent();

                    if (
                        ev->getProvider() == provider &&
                        evPtr->getId()    == event->getId()
                    ) {
                        return true;
                    }

                    return false;
                });

                if (it != self->_events.end()) {
                    auto *sameEvent = (*it)->getEvent();
                    
                    if (*sameEvent != *event) {
                        LOG(("Currently scheduled event of id {} has changed", sameEvent->getId()));

                        (*it)->update(event.get());
                    }

                } else {
                    std::unique_ptr<ScheduledEvent> scheduledEvent;

                    LOG(("{}", event->toString()));

                    if (dynamic_cast<EventSqueeze *>(event.get())) {
                        scheduledEvent = std::make_unique<ScheduledSqueezeEvent>(provider, self->_channel, reinterpret_cast<EventSqueeze &>(*event), &self->_squeezes);

                    } else if (dynamic_cast<EventOverlay *>(event.get())) {
                        scheduledEvent = std::make_unique<ScheduledOverlayEvent>(provider, self->_channel, reinterpret_cast<EventOverlay &>(*event));

                    } else if (dynamic_cast<EventSpliceInsert *>(event.get())) {
                        scheduledEvent = std::make_unique<ScheduledSpliceInsertEvent>(provider, self->_channel, reinterpret_cast<EventSpliceInsert &>(*event), self->_scte.getPrerollTime());

                    } else if (dynamic_cast<EventTimeSignal *>(event.get())) {
                        scheduledEvent = std::make_unique<ScheduledTimeSignalEvent>(provider, self->_channel, reinterpret_cast<EventTimeSignal &>(*event), self->_scte.getPrerollTime());

                    } else if (dynamic_cast<EventReturnToNetwork *>(event.get())) {
                        scheduledEvent = std::make_unique<ScheduledReturnToNetworkEvent>(provider, self->_channel, reinterpret_cast<EventReturnToNetwork &>(*event), self->_scte.getPrerollTime());

                    } else if (dynamic_cast<EventSlate *>(event.get())) {
                        scheduledEvent = std::make_unique<ScheduledSlateEvent>(provider, self->_channel, reinterpret_cast<EventSlate &>(*event));
                    }

                    if (scheduledEvent) {
                        self->_events.push_back(std::move(scheduledEvent));

                        self->_events.rbegin()->get()->prepare();
                    }
                }
            }
        }
    }

    // Stop all removed events that are in progress
    {
        auto it = self->_events.begin();

        while (it != self->_events.end()) {
            auto &event = *it;
            
            bool remove = false;

            if (event->isInProgress() && ! event->isStopping()) {
                if (! event->getProvider()->isValid(*event->getEvent())) {
                    auto eventId = event->getEvent()->getId();

                    logger.log("Cancelling event with ID {} as its provider indicates it is no longer valid.", eventId);

                    if (! event->cancel()) {
                        WARN(("Error occurred during cancelling event of ID {}", eventId));
                    }

                    // Immediate stop is not yet supported. Events should be stopped with requested fade delays
                    // remove = true;
                }
            }

            if (remove) {
                it = self->_events.erase(it);

            } else {
                ++it;
            }
        }
    }

    // Remove all invalid events
    {
        auto it = self->_events.begin();

        while (it != self->_events.end()) {
            if (! (*it)->isHandled() && ! (*it)->isInProgress()) {
                Event &event = *(*it)->getEvent();

                if (! (*it)->getProvider()->isValid(event)) {
                    LOG(("Removing not valid event {} from the schedule, {} remain", event.getId(), self->_events.size() - 1));

                    it = self->_events.erase(it);

                } else {
                    ++it;
                }

            } else {
                ++it;
            }
        }
    }

    return true;
}


transcoder::EventScheduler::EventScheduler() {
    this->_onCheckForNewEventSigId = 0;
}

transcoder::EventScheduler::~EventScheduler() {
    this->stop();
}

void transcoder::EventScheduler::addProvider(EventProvider *provider) {
    this->_providers.push_back(provider);
}

void transcoder::EventScheduler::removeProvider(EventProvider *provider) {
    this->_providers.erase(
        std::remove(this->_providers.begin(), this->_providers.end(), provider), 
        this->_providers.end()
    );
}

void transcoder::EventScheduler::start(const PredefinedSqueezes &squeezes, const ScteParameters &scteParams, Channel *channel) {
    this->_squeezes = squeezes;
    this->_scte     = scteParams;
    this->_channel  = channel;

    for (auto *provider : this->_providers) {
        provider->start();
    }

    this->_onCheckForNewEventSigId = g_timeout_add(EVENT_CHECK_INTERVAL_MS, G_SOURCE_FUNC(_onCheckForNewEvent), this);
}

void transcoder::EventScheduler::stop() {
    if (this->_onCheckForNewEventSigId) {
        g_source_remove(this->_onCheckForNewEventSigId);

        this->_onCheckForNewEventSigId = 0;
    }

    this->_events.clear();

    for (auto *provider : this->_providers) {
        provider->stop();
    }
}
