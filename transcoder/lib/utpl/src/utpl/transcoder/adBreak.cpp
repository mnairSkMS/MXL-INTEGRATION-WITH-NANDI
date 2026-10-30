#include "utpl/common/debug.h"

#include "utpl/transcoder/adBreak.h"
#include "utpl/transcoder/channel/processor.h"

#include "utpl/logger.h"

using namespace utpl;

static auto &logger = Logger::getLogger(UTPL_LOGGER_NAME_SCHEDULE);


transcoder::AdBreakSwitcher::AdBreakSwitcher(Channel *channel) : ScteEventTracker(channel) {
    if (channel) {
        channel->onScteData([this](const Scte35Data &scteData) {
            if (this->_params.isEnabled()) {
                this->handleEvent(this->_params.getTrigger(), scteData);
            }
        });
    }
}

void transcoder::AdBreakSwitcher::configure(const AdInsertionParameters &params) {
    this->_params = params;

    // A break ends on a cue-in, never on the duration carried by the cue-out.
    this->setIgnoreSignalledDuration(true);

    // ...which leaves a lost cue-in as the one way to strand the channel on ads, so bound it.
    this->setMaxEventDuration(params.getMaxBreakDuration());
}

void transcoder::AdBreakSwitcher::bindInputs(
    std::shared_ptr<Input> programmeInput,
    std::shared_ptr<Input> adInput
) {
    this->_programmeInput = programmeInput;
    this->_adInput        = adInput;
}

// Source switching happens inside the transcoding connectors, so a passthrough track has no
// selector to switch. Slate refuses the same way rather than half-working.
bool transcoder::AdBreakSwitcher::_canSwitch() {
    auto *channel = this->getChannel();

    if (! channel) {
        return false;
    }

    if (! this->_adInput || ! this->_programmeInput) {
        if (! this->_unsupportedReported) {
            this->_unsupportedReported = true;

            logger.warning("Ad insertion is enabled but no ad break input is bound - ignoring cues");
        }

        return false;
    }

    {
        auto *videoProcessor = channel->getVideoProcessor();

        if (videoProcessor && videoProcessor->getInputParameters().getCodec() == Codec::PASSTHROUGH) {
            if (! this->_unsupportedReported) {
                this->_unsupportedReported = true;

                logger.error("Ad insertion requires a transcoded video track, but the channel passes video through - ignoring cues");
            }

            return false;
        }
    }

    return true;
}

void transcoder::AdBreakSwitcher::onStateResolved(
    bool active,
    uint64_t eventId,
    BlackoutSegmentationPriority priority
) {
    // Called on every evaluation, not only on transitions, so act on the edges alone.
    if (active == this->_onAir) {
        return;
    }

    if (active) {
        this->_enterBreak(eventId);

    } else {
        this->_leaveBreak();
    }
}

void transcoder::AdBreakSwitcher::_enterBreak(uint64_t eventId) {
    if (! this->_canSwitch()) {
        return;
    }

    auto *channel = this->getChannel();

    logger.log("Ad break {} is starting - switching the programme to the ad source", eventId);

    // Resume before switching: a frozen branch is already prerolled, so this is a state change
    // and the first ad frame is available immediately rather than after a fresh preroll.
    {
        auto error = this->_adInput->setPlaybackActive(true);

        if (error != Error::NO_ERROR && error != Error::NOT_SUPPORTED) {
            logger.error("Unable to resume the ad source - staying on the programme");

            return;
        }
    }

    this->_returnTarget = channel->getCurrentInput();

    channel->setCurrentInput(this->_adInput);

    this->_onAir = true;
}

void transcoder::AdBreakSwitcher::_leaveBreak() {
    auto *channel = this->getChannel();

    if (! channel || ! this->_adInput) {
        this->_onAir = false;

        return;
    }

    logger.log("Ad break is ending - returning the programme to air");

    // Freeze before switching, not after. The ad branch is the selector's active pad and is
    // pushing into it; flipping the active pad out from under a running branch stalls the switch
    // inside input-selector, and because the switch runs on the channel's timed-callback thread
    // that stall takes every later scheduled event with it. Pausing first stops the branch, and
    // it is where the resume position is captured anyway.
    this->_adInput->setPlaybackActive(false);

    {
        auto target = this->_returnTarget ? this->_returnTarget : this->_programmeInput;

        if (target) {
            channel->setCurrentInput(target);
        }
    }

    logger.log("Programme is back on air");

    this->_returnTarget = nullptr;
    this->_onAir        = false;
}
