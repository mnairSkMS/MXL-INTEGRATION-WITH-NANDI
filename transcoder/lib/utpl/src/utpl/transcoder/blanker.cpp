#include "utpl/common/debug.h"

#include "utpl/transcoder/blanker.h"

using namespace utpl;


transcoder::Blanker::Blanker(Channel *channel) : ScteEventTracker(channel) {
    if (channel) {
        channel->onScteData([this](const Scte35Data &scteData) {
            if (this->_blankingParams.isEnabled()) {
                this->handleEvent(this->_blankingParams, scteData);
            }

            if (this->_blackoutParams.isEnabled()) {
                this->handleEvent(this->_blackoutParams, scteData);
            }
        });
    }
}

void transcoder::Blanker::onStateResolved(
    bool active,
    uint64_t eventId,
    BlackoutSegmentationPriority priority
) {
    if (active) {
        DBG(("Enable blanking due to event ID: {}, priority {}", eventId, priority));
    }

    this->getChannel()->blank(active);
}

void transcoder::Blanker::configure(
    const transcoder::BlackoutParameters &blackoutParams,
    const transcoder::BlackoutParameters &blankingParams
) {
    this->_blackoutParams = blackoutParams;
    this->_blankingParams = blankingParams;
}
