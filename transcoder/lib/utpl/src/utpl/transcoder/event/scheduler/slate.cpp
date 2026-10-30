#include "utpl/common/debug.h"

#include "utpl/transcoder/input/overlay.h"

#include "slate.h"

using namespace utpl;


#define PRELOAD_TIMEOUT_MS       3000
#define OVERLAY_SHOW_TIME_MIN_MS  500


void transcoder::EventScheduler::ScheduledSlateEvent::_start() {
    std::lock_guard guard(this->startGuard);

    {
        Timestamp startTimestamp = Time::getUTCTimestamp();

        if (this->getEvent()->getTimestamp() > startTimestamp) {
            DBG(("It is too early to start the event {}. Skipping start request", this->getEvent()->getId()));

        } else {

            if (this->inputProbed && ! this->inputAdded) {
                auto channel = this->getChannel();

                {
                    bool hasAudioStream = false;
                    bool audioWillPlay  = false;
                    bool videoWillPlay  = false;

                    auto &streams = this->input->getStreams();

                    if (streams.empty()) {
                        ERR(("Slate media has neither audio nor video streams - skipping playback"));

                    } else {
                        for (const auto &stream : streams) {
                            const auto &params = stream->getStreamParameters();

                            if (params.hasAudioParameters()) {
                                hasAudioStream = true;

                                auto processors = this->getChannel()->getAudioProcessors();
                                if (processors.empty()) {
                                    WARN(("Slate media has an audio track, but the channel has no audio track - skipping audio part"));

                                } else {
                                    bool anyTranscoding = false;

                                    for (auto *processor : processors) {
                                        if (processor->getInputParameters().getCodec() != Codec::PASSTHROUGH) {
                                            anyTranscoding = true;
                                        }
                                    }

                                    if (! anyTranscoding) {
                                        WARN(("Slate media has an audio track, but the channel has no transcoded audio track - skipping audio part"));

                                    } else {
                                        audioWillPlay = true;
                                    }
                                }

                            } else if (params.hasVideoParameters()) {
                                auto *processor = this->getChannel()->getVideoProcessor();
                                if (! processor) {
                                    WARN(("Slate media has a video track, but the channel has no video track - skipping video part"));

                                } else {
                                    if (processor->getInputParameters().getCodec() != Codec::PASSTHROUGH) {
                                        videoWillPlay = true;

                                    } else {
                                        WARN(("Slate media has a video track, but the channel has no transcoded video track - skipping video part"));
                                    }
                                }

                            } else {
                                WARN(("Unsupported stream type: neither video nor audio"));
                            }
                        }
                    }

                    if (audioWillPlay || videoWillPlay) {
                        this->originalInput = channel->getCurrentInput();

                        // Slate media with no audio track of its own would otherwise leave the
                        // programme audio playing under a silent card. Claim the mute for as
                        // long as this event lives; the hold releases itself.
                        if (! hasAudioStream) {
                            this->audioBlank = Channel::AudioBlankHold(channel);

                            LOG(("Slate media has no audio stream - channel audio has been muted for event {}", this->getEvent()->getId()));
                        }

                        channel->addInput(this->input);

                        // Set before the switch, not after: if setCurrentInput throws or
                        // re-enters, cleanup still has to know the input was added.
                        this->inputAdded = true;

                        channel->setCurrentInput(this->input);

                        logFired(this->event, Time::getUTCTimestamp(), this->getCurrentRunningTime(), fmt::format(
                            "uri=\"{}\" video={} audio={} duration_ms={}",
                            this->event.getUri(), videoWillPlay ? "slate" : "programme",
                            audioWillPlay ? "slate" : (hasAudioStream ? "programme" : "muted"), this->event.getDuration()
                        ));

                    } else if (! this->missedLogged) {
                        logMissed(this->event, "the slate media has no stream this channel can play");

                        this->missedLogged = true;
                    }
                }
            }
        }
    }
}


transcoder::EventScheduler::ScheduledSlateEvent::ScheduledSlateEvent(
    EventProvider *provider, 
    Channel *channel, 
    const EventSlate &event
) : ScheduledEvent(
    provider, channel, channel->getVideoProcessor() ? Channel::TimedCallbackSource::COMPOSITOR : Channel::TimedCallbackSource::PROCESSOR, false, false
), event(event) {
    utpl::Timestamp preloadTimeout = PRELOAD_TIMEOUT_MS;
    utpl::Timestamp eventDuration  = event.getDuration();

    if (preloadTimeout > eventDuration) {
        preloadTimeout = eventDuration - OVERLAY_SHOW_TIME_MIN_MS;
    }

    {
        auto *channel = this->getChannel();

        // TODO: Parameters!
        this->input = std::make_unique<utpl::transcoder::OverlayInput>("/tmp/", preloadTimeout);
        if (this->input) {
            {
                transcoder::InputParameters params;
    
                params.addUri(event.getUri());
    
                this->input->prepare(params);
            }
    
            this->inputProbed = false;
            this->inputAdded  = false;
            this->audioBlank.release();

            this->input->probe([this](bool success) {
                std::lock_guard guard(this->startGuard);

                this->inputProbed = success;
                if (this->inputProbed) {
                    this->_start();
                }

                return false;
            });
        }
    }
}

transcoder::EventScheduler::ScheduledSlateEvent::~ScheduledSlateEvent() {
    // Destruction while the slate is on air - a cancellation, a reconfiguration - used to
    // just drop the input, leaving it in the channel with the original never restored. The
    // audio hold would release itself either way, but the input needs this.
    if (this->inputAdded || this->audioBlank.held()) {
        this->handleStop();

    } else {
        this->input = nullptr;
    }
}

transcoder::Event *transcoder::EventScheduler::ScheduledSlateEvent::getEvent() {
    return &this->event;
}

void transcoder::EventScheduler::ScheduledSlateEvent::handleStart() {
    DBG(("Timer requested start of event {}", this->getEvent()->getId()));

    this->_start();
}

void transcoder::EventScheduler::ScheduledSlateEvent::handleStop() {
    std::lock_guard guard(this->startGuard);

    // Restoring the input and releasing the mute are independent: a slate can hold the mute
    // without having been added yet, and vice versa. The previous version nested the same
    // null check inside its own else branch, so that whole path - and the setState it carried
    // - was unreachable. State::HANDLED is set generically by the scheduler in either case.
    if (this->input && this->inputAdded) {
        DBG(("Stopping slate input"));

        auto channel = this->getChannel();

        channel->setCurrentInput(this->originalInput);
        channel->removeInput(this->input);

        this->inputAdded = false;

        logEnded(this->event, Time::getUTCTimestamp(), this->getCurrentRunningTime());

    } else {
        DBG(("Slate input was not added - skipping input restoration"));

        if (! this->missedLogged) {
            logMissed(this->event, "the slate never went on air - media not ready in time or not playable");
        }
    }

    this->audioBlank.release();

    this->inputProbed = false;
    this->input       = nullptr;
}

void transcoder::EventScheduler::ScheduledSlateEvent::update(Event *event) {
    DBG(("Updating a scheduled slate event is not supported - ignored"));
}

bool transcoder::EventScheduler::ScheduledSlateEvent::cancel() {
    return ScheduledEvent::cancel();
}
