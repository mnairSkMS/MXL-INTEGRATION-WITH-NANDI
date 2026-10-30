#ifndef __UTPL_TRANSCODER_AD_BREAK_H__
#define __UTPL_TRANSCODER_AD_BREAK_H__

#include <atomic>
#include <memory>

#include "utpl/transcoder/types.h"
#include "utpl/transcoder/input.h"
#include "utpl/transcoder/channel.h"
#include "utpl/transcoder/scte/eventTracker.h"

namespace utpl {
    namespace transcoder {
        // Replaces the programme with an alternate source for the duration of a signalled avail.
        //
        // A cue-out puts the ad source on air; a cue-in takes it off again and freezes it, so a
        // spot that was interrupted continues from the same point in the next break instead of
        // starting over. Freezing is also what makes the switch fast: a paused branch is a
        // prerolled branch, so going back on air is a state change rather than a fresh preroll.
        //
        // The programme input keeps running throughout. Its buffers are dropped by the connector
        // while it is off air, which is what makes the return land at the live edge.
        //
        // Break length comes from the cues alone. A signalled duration is descriptive only; the
        // bound configured as maxBreakDuration is the sole protection against a lost cue-in, and
        // it is scheduled through the tracker so that a late cue-in still wins.
        class AdBreakSwitcher : public ScteEventTracker {
            public:
                AdBreakSwitcher(Channel *channel);

                void configure(const AdInsertionParameters &params);

                // Supplied once the engine has built its inputs. Without both of them the
                // switcher stays inert, whatever the configuration says.
                void bindInputs(
                    std::shared_ptr<Input> programmeInput,
                    std::shared_ptr<Input> adInput
                );

                // True while a locally inserted break is on air. The channel consults it to decide
                // whether source markers may still reach the outputs.
                bool isBreakOnAir() const {
                    return this->_onAir;
                }

            protected:
                void onStateResolved(
                    bool active,
                    uint64_t eventId,
                    BlackoutSegmentationPriority priority
                ) override;

            private:
                bool _canSwitch();
                void _enterBreak(uint64_t eventId);
                void _leaveBreak();

            private:
                AdInsertionParameters _params;

                std::shared_ptr<Input> _programmeInput;
                std::shared_ptr<Input> _adInput;

                // The source to restore. Captured when the break starts, so whatever was on air at
                // that moment - the programme, or a slate that had taken over - is what comes back.
                std::shared_ptr<Input> _returnTarget;

                // Read from the channel's SCTE output path, written from the event path.
                std::atomic_bool _onAir{false};

                // Logged once, so a misconfiguration does not repeat on every cue.
                bool _unsupportedReported = false;
        };
    }
}

#endif /* __UTPL_TRANSCODER_AD_BREAK_H__ */
