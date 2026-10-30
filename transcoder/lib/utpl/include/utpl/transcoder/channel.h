#ifndef __UTPL_TRANSCODER_CHANNEL_H__
#define __UTPL_TRANSCODER_CHANNEL_H__

#include <cstdint>

#include "utpl/common/utils/class.h"

#include "utpl/pipeline/element.h"
#include "utpl/pipeline/pipe.h"

#include "utpl/transcoder/input.h"
#include "utpl/transcoder/output.h"
#include "utpl/transcoder/channel/processor.h"
#include "utpl/transcoder/stream/input/image.h"
#include "utpl/esam/client.h"

namespace utpl {
    namespace transcoder {
        class Channel {
            UTPL_CLASS_NOCOPY(Channel);

            public:
                using StateChangeCallback = std::function<void()>;

                /**
                 * One reason for the channel's audio to be muted, held by whoever wants it.
                 *
                 * Deliberately not an alias of AudioBlankState::Token: that header is private
                 * to the implementation, and duplicating the underlying type in two places is
                 * how the two drift apart. channel.cpp static_asserts that they agree.
                 */
                using AudioBlankToken = uint64_t;

                enum class TimedCallbackSource {
                    COMPOSITOR,
                    PROCESSOR,
                    OUTPUT
                };

                using InputList = std::vector<std::shared_ptr<Input>>;

            public:
                Channel();
                virtual ~Channel();

                void configure(const transcoder::ScteParameters &scteParams);

                // maps inputs to outputs
                // Creates internal pipeline elements
                // links corresponding pads
                void addInput(std::shared_ptr<Input> &input);
                void removeInput(std::shared_ptr<Input> &input);
                void setCurrentInput(std::shared_ptr<Input> &input);
                std::shared_ptr<Input> getCurrentInput();

                void addOutput(std::shared_ptr<Output> &output, const OutputParameters &params);

                void prepare(pipeline::PipePtr pipe);
                void makeReady(std::function<void()> callback = {});
                void cleanup();

                pipeline::BinPtr getNode();

                Processor *getVideoProcessor();
                std::vector<Processor *>getAudioProcessors();

                std::shared_ptr<Compositor> getVideoCompositor();

                void squeeze(const NormRect &srcNorm, const NormRect &dstNorm, int transitionMs);

                std::unique_ptr<utpl::transcoder::ImageInputStream> createImageStream(
                    const std::vector<std::string> &uris, 
                    const utpl::transcoder::VideoParameters &parameters
                );

                std::unique_ptr<utpl::transcoder::Input> createOverlayInput();

                pipeline::TimedCallbackId addTimedCallback(Timestamp callTime, Timestamp interval, TimedCallbackSource src, std::function<bool(pipeline::TimedCallbackId, GstClockTime time)> callback);
                void cancelTimedCallback(pipeline::TimedCallbackId callbackId);

                void injectScte35Event(const Scte35Data &data);

                void onScteData(ScteDataCallback callback);

                // Lets a local consumer that replaces content - ad insertion - hide the source
                // markers for as long as the replacement is on air. Unset means passthrough,
                // which is the default and matches the behavior without ad insertion.
                void setScteSuppressionPredicate(std::function<bool()> predicate);

                /**
                 * The channel-wide blackout: video, captions and audio together.
                 *
                 * blank(false) clears only this reason. If a slate still holds an audio blank,
                 * audio stays muted - which is the point. See AudioBlankHold.
                 */
                void blank(bool blank);

                /**
                 * A scoped claim on muting the channel's audio, independent of blank().
                 *
                 * Move-only, and releases in its destructor, so a caller cannot leak a claim
                 * by taking an early return or by being destroyed mid-flight. A leaked claim
                 * is permanent silence with nothing left holding the token to release it, so
                 * the ownership is structural rather than a call someone has to remember.
                 *
                 * The channel is borrowed, so a hold must not outlive it. Engine satisfies
                 * that by construction: _channel is declared before _scheduler, so every
                 * ScheduledSlateEvent - and every hold it owns - is destroyed first, and
                 * Engine::stop() clears the events explicitly while the channel is still up.
                 */
                class AudioBlankHold {
                    public:
                        AudioBlankHold() = default;
                        explicit AudioBlankHold(Channel *channel);

                        ~AudioBlankHold();

                        AudioBlankHold(AudioBlankHold &&other) noexcept;
                        AudioBlankHold &operator=(AudioBlankHold &&other) noexcept;

                        AudioBlankHold(const AudioBlankHold &) = delete;
                        AudioBlankHold &operator=(const AudioBlankHold &) = delete;

                        /** Idempotent; safe to call before destruction. */
                        void release();

                        bool held() const {
                            return this->_channel != nullptr;
                        }

                    private:
                        Channel        *_channel = nullptr;
                        AudioBlankToken _token = 0;
                        uint64_t        _generation = 0;
                };

                /**
                 * Prefer AudioBlankHold. These are the primitives it is built from, exposed
                 * because the hold has to reach them.
                 */
                AudioBlankToken acquireAudioBlank(uint64_t *generation = nullptr);
                void releaseAudioBlank(AudioBlankToken token, uint64_t generation);

                InputList &getInputs();

                std::shared_ptr<Input> getInput(const std::string &id);


            private:
                class Pimpl;
                std::unique_ptr<Pimpl> self;
        };
    }
}

#endif /* __UTPL_TRANSCODER_CHANNEL_H__ */
