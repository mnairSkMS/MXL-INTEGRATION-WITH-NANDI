#ifndef __UTPL_TRANSCODER_BLANKER_H__
#define __UTPL_TRANSCODER_BLANKER_H__

#include "utpl/transcoder/types.h"
#include "utpl/transcoder/channel.h"
#include "utpl/transcoder/scte/eventTracker.h"

namespace utpl {
    namespace transcoder {
        // Blacks out and mutes the channel for the duration of a signalled section.
        //
        // Blanking and blackout are two rule sets over the same event tracker: an event scheduled
        // by either competes with the other on segmentation priority, and the winner decides
        // whether the channel is blanked.
        class Blanker : public ScteEventTracker {
            public:
                Blanker(Channel *channel);

                void configure(
                    const transcoder::BlackoutParameters &blackoutParams,
                    const transcoder::BlackoutParameters &blankingParams
                );

            protected:
                void onStateResolved(
                    bool active,
                    uint64_t eventId,
                    BlackoutSegmentationPriority priority
                ) override;

            private:
                BlackoutParameters _blackoutParams;
                BlackoutParameters _blankingParams;
        };
    }
}

#endif /* __UTPL_TRANSCODER_BLANKER_H__ */
