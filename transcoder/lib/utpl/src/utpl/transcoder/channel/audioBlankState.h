#ifndef __UTPL_TRANSCODER_CHANNEL_AUDIO_BLANK_STATE_H__
#define __UTPL_TRANSCODER_CHANNEL_AUDIO_BLANK_STATE_H__

#include <cstdint>
#include <unordered_set>

namespace utpl {
    namespace transcoder {
        /**
         * Why the channel's audio is muted, rather than whether it is.
         *
         * Audio blanking used to be a single boolean behind Channel::blank(). That works
         * until two things want it at once: a slate whose media has no audio track mutes on
         * start and unmutes on stop, and if an SCTE blackout is running at the time, the
         * slate's unmute lets programme audio out of the blackout. Last writer wins on a
         * shared flag.
         *
         * So the reasons are stored instead of the answer. The channel-level blackout is a
         * level - it is on or off - while every transient muter holds a token. shouldBlank()
         * is the OR of the two, and neither party can clear the other's reason.
         *
         * Tokens rather than a counter: a plain refcount lets any holder decrement it, so one
         * double-release would silently unmute someone else's claim. erase() reports whether
         * the token was really held, which turns that bug into a warning instead of silence.
         */
        class AudioBlankState {
            public:
                using Token = uint64_t;

                /** The channel-wide blackout. Only Channel::blank() sets this. */
                void setChannelBlanked(bool blanked) {
                    this->_channelBlanked = blanked;
                }

                Token acquire() {
                    Token token;

                    // 0 is the "holds nothing" sentinel, so it must never be issued.
                    do {
                        token = this->_nextToken++;
                    } while (token == 0 || this->_tokens.find(token) != this->_tokens.end());

                    this->_tokens.insert(token);
                    return token;
                }

                enum class ReleaseResult {
                    RELEASED,   ///< The token was held and is now gone.
                    STALE,      ///< From a previous generation; reset() already dropped it.
                    UNKNOWN,    ///< Never issued, or released twice. A real bug.
                };

                /**
                 * `generation` is the value read when the token was acquired.
                 *
                 * The distinction matters because stop() calls reset(), which drops every
                 * token while their holders still exist. Those holders then release tokens
                 * that are legitimately gone. Without the generation that is indistinguishable
                 * from a double-release, so a routine channel restart would log warnings and
                 * train everyone to ignore the one message that means something.
                 */
                ReleaseResult release(Token token, uint64_t generation) {
                    if (this->_tokens.erase(token) == 1) {
                        return ReleaseResult::RELEASED;
                    }

                    return generation != this->_generation
                        ? ReleaseResult::STALE
                        : ReleaseResult::UNKNOWN;
                }

                uint64_t generation() const {
                    return this->_generation;
                }

                bool shouldBlank() const {
                    return this->_channelBlanked || ! this->_tokens.empty();
                }

                /** Channel teardown. Bumps the generation so surviving holders release quietly. */
                void reset() {
                    this->_channelBlanked = false;
                    this->_tokens.clear();
                    this->_generation++;
                }

            private:
                bool _channelBlanked = false;
                Token _nextToken = 1;
                uint64_t _generation = 1;
                std::unordered_set<Token> _tokens;
        };
    }
}

#endif /* __UTPL_TRANSCODER_CHANNEL_AUDIO_BLANK_STATE_H__ */
