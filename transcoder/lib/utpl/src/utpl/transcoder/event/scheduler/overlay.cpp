#include "utpl/common/debug.h"

#include "overlay.h"

using namespace utpl;


#define PRELOAD_TIMEOUT_MS       3000
#define OVERLAY_SHOW_TIME_MIN_MS  500


void transcoder::EventScheduler::ScheduledOverlayEvent::_start() {
    std::lock_guard guard(this->startGuard);

    {
        Timestamp startTimestamp = Time::getUTCTimestamp();
        
        if (this->getEvent()->getTimestamp() > startTimestamp) {
            DBG(("It is too early to start the event {}. Skipping start request", this->getEvent()->getId()));

        } else {
            if (this->overlay) {
                DBG(("Event {} is already started - skipping start request", this->getEvent()->getId()));

            } else if (! this->stream->isPreloaded()) {
                DBG(("Event {} should start right now, but the image stream is not yet preloaded – skipping its startup. It will start asynchronously when buffered.", this->getEvent()->getId()));

            } else {
                auto compositor = this->getChannel()->getVideoCompositor();
                if (! compositor) {
                    WARN(("The video processor has no compositor attached - please verify if you are transcoding the video. This may happen when using passthrough mode"));

                } else {
                    DBG(("Starting event of ID {}. Creating video overlay, compositor {}", this->getEvent()->getId(), (void *) compositor.get()));

                    this->overlay = compositor->createLayer();

                    if (event.hasZOrder()) {
                        this->overlay->setZOrder(event.getZOrder());
                    }

                    this->overlay->setOpacity(0);

                    {
                        auto geometry = event.getGeometry().resolve(compositor->getWindowSize());
                        if (geometry.width() <= 0 || geometry.height() <= 0) {
                            const auto &streamParams = this->stream->getStreamParameters().getVideoParameters();

                            geometry.width(streamParams.getWidth());
                            geometry.height(streamParams.getHeight());
                        }

                        this->overlay->setWindows(ORIGINAL_WINDOW, geometry);
                    }

                    {
                        auto overlaySinkPad = this->overlay->getSinkPad();

                        // A newly requested overlay pad has no segment yet, so querying
                        // its position commonly returns GST_CLOCK_TIME_NONE.  Starting
                        // the image appsrc at zero then makes a long-running compositor
                        // discard every overlay frame as late.  Align it to the active
                        // programme pad, which already owns the channel timeline.
                        auto runningTime = compositor->getVideoLayer()->getRunningTime();
                        if (! GST_CLOCK_TIME_IS_VALID(runningTime)) {
                            runningTime = overlaySinkPad->getTimePosition();
                        }
                        LOG(("Aligning overlay event {} to running time {} ns at z-order {}",
                            event.getId(), runningTime, this->overlay->getZOrder()
                        ));
                        this->stream->updatePosition(runningTime);

                        // The first buffer through the compositor pad is the moment the graphic
                        // is on air. The probe can outlive this object, so it keeps a copy of the
                        // event rather than `this`.
                        overlaySinkPad->probeCurrentPts([event = this->event, zOrder = this->overlay->getZOrder()](GstClockTime pts, GstClockTime sinkRunningTime) {
                            logFired(event, Time::getUTCTimestamp(), sinkRunningTime, fmt::format(
                                "pts={} z_order={} opacity={} fade_in_ms={} duration_ms={}",
                                pts, zOrder, event.getOpacity(), event.getFadeInDuration(), event.getDuration()
                            ));
                        });
                        
                        this->stream->requestPad(false)->link(overlaySinkPad);
                        this->stream->link();
                    }
                    
                    DBG(("Starting stream feeding, opacity: {}, fade in {}ms", event.getOpacity(), event.getFadeInDuration()));
                    
                    this->stream->unblock();
                    this->overlay->setOpacity(event.getOpacity(), event.getFadeInDuration());
                }
            }
        }
    }
}


transcoder::EventScheduler::ScheduledOverlayEvent::ScheduledOverlayEvent(
    EventProvider *provider, 
    Channel *channel, 
    const EventOverlay &event
) : ScheduledEvent(
    provider, channel, Channel::TimedCallbackSource::COMPOSITOR, false, event.getFadeOutDuration() > 0
), event(event) {
    this->fadeOutTimerId = nullptr;

    {
        utpl::transcoder::VideoParameters video;

        video.setFramerate(event.getRate());

        {
            // Sizes the decoded image raster. Without a compositor there is no output frame
            // to resolve against, which leaves the geometry at zero and defers sizing to the
            // stream's own parameters in _start().
            auto compositor = channel->getVideoCompositor();

            const auto geometry = event.getGeometry().resolve(
                compositor ? compositor->getWindowSize() : utpl::Rectangle()
            );

            video.setWidth(geometry.width());
            video.setHeight(geometry.height());
        }

        this->stream = channel->createImageStream(event.getUris(), video);
    }

    /*
     *               Duration 
     * |---------------------------------|
     * | fade in |                       | fade out |
     * |--------------------------------------------|
     * | fade in |        overlay        | fade out |
     * |---------------------------------------------
     */
    {
        utpl::Timestamp preloadTimeout = PRELOAD_TIMEOUT_MS;
        utpl::Timestamp eventDuration  = event.getDuration();

        if (eventDuration > 0 && preloadTimeout > eventDuration) {
            preloadTimeout = eventDuration - OVERLAY_SHOW_TIME_MIN_MS;
        }

        if (preloadTimeout > 0) {
            this->stream->preload(preloadTimeout, 
                [this, event]() {
                    LOG(("Image has been sucessfully preloaded, starting event {}", this->event.getId()));

                    this->_start();
                },
                [](Error errorCode) {
                    WARN(("Preloading has failed!"));
                }
            );
        }
    }
}

transcoder::EventScheduler::ScheduledOverlayEvent::~ScheduledOverlayEvent() {
    if (this->fadeOutTimerId) {
        this->getChannel()->cancelTimedCallback(this->fadeOutTimerId);
    }

    if (this->stream) {
        this->stream = nullptr;
    }

    if (this->overlay) {
        auto compositor = this->getChannel()->getVideoCompositor();
        if (compositor) {
            DBG(("Destroying video layer {} on processor {}", (void *) this->overlay.get(), (void *) compositor.get()));

            compositor->destroyLayer(this->overlay);

            this->overlay = nullptr;
        }
    }
}

transcoder::Event *transcoder::EventScheduler::ScheduledOverlayEvent::getEvent() {
    return &this->event;
}

void transcoder::EventScheduler::ScheduledOverlayEvent::handleStart() {
    DBG(("Timer requested start of event {}", this->getEvent()->getId()));

    this->_start();
}

void transcoder::EventScheduler::ScheduledOverlayEvent::handleStop() {
    if (! this->stream) {
        DBG(("Stream was not created - skipping the request"));

        logMissed(this->event, "the image stream was never created");

    } else {
        if (! this->overlay) {
            DBG(("video overlay is null - skipping the request"));

            logMissed(this->event, "the graphic never went on air - image not loaded in time, or no video compositor (video passthrough?)");

            this->setState(State::HANDLED);

        } else {
            DBG(("Stopping video overlay {}", (void *) this->overlay.get()));

            logEnded(this->event, Time::getUTCTimestamp(), this->getCurrentRunningTime(),
                fmt::format("fade_out_ms={}", event.getFadeOutDuration())
            );

            this->overlay->setOpacity(0, event.getFadeOutDuration());

            if (event.getFadeOutDuration()) {
                this->fadeOutTimerId = this->getChannel()->addTimedCallback(
                    // TODO: Investigate why the image is delayed compared to the compositor output (latency in the pipeline?).
                    utpl::Time::getUTCTimestamp() + event.getFadeOutDuration() + 1500, 0, Channel::TimedCallbackSource::COMPOSITOR, [this](pipeline::TimedCallbackId id, GstClockTime time) {
                        this->fadeOutTimerId = nullptr;

                        DBG(("Marking overlay event {} as handled after fading out", this->event.getId()));

                        this->stream->block([this]() {
                            auto peerPad = this->overlay->getSinkPad()->getPeer();
                            if (peerPad) {
                                auto peerPadPtr = std::make_shared<pipeline::Pad>(peerPad);

                                peerPadPtr->unlink();

                                this->stream->unlink();
                                this->stream->releasePad(peerPadPtr);
                            }

                            this->setState(State::HANDLED);

                            return true;
                        });

                        return false;
                    }
                );

                if (! this->fadeOutTimerId) {
                    this->stream->block([this]() {
                        auto peerPad = this->overlay->getSinkPad()->getPeer();
                        if (peerPad) {
                            auto peerPadPtr = std::make_shared<pipeline::Pad>(peerPad);

                            peerPadPtr->unlink();

                            this->stream->unlink();
                            this->stream->releasePad(peerPadPtr);
                        }

                        this->setState(State::HANDLED);

                        return true;
                    });
                }

            } else {
                this->stream->block([this]() {
                    auto peerPad = this->overlay->getSinkPad()->getPeer();
                    if (peerPad) {
                        auto peerPadPtr = std::make_shared<pipeline::Pad>(peerPad);

                        peerPadPtr->unlink();

                        this->stream->unlink();
                        this->stream->releasePad(peerPadPtr);
                    }

                    this->setState(State::HANDLED);

                    return true;
                });
            }
        }
    }
}

void transcoder::EventScheduler::ScheduledOverlayEvent::update(Event *event) {
    DBG(("Updating a scheduled overlay event is not supported - ignored"));
}

bool transcoder::EventScheduler::ScheduledOverlayEvent::cancel() {
    return ScheduledEvent::cancel();
}
