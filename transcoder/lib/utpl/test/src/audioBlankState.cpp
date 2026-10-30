#include <gtest/gtest.h>

#include "utpl/transcoder/channel/audioBlankState.h"

using namespace utpl::transcoder;

namespace {
    // Every release in these tests is from the current generation unless a test says otherwise.
    AudioBlankState::ReleaseResult release(AudioBlankState &state, AudioBlankState::Token token) {
        return state.release(token, state.generation());
    }

    constexpr auto RELEASED = AudioBlankState::ReleaseResult::RELEASED;
    constexpr auto STALE    = AudioBlankState::ReleaseResult::STALE;
    constexpr auto UNKNOWN  = AudioBlankState::ReleaseResult::UNKNOWN;
}

TEST(AudioBlankState, StartsUnblanked) {
    AudioBlankState state;

    EXPECT_FALSE(state.shouldBlank());
}

TEST(AudioBlankState, AudioRequestCanBeReleased) {
    AudioBlankState state;

    auto token = state.acquire();
    EXPECT_TRUE(state.shouldBlank());

    EXPECT_EQ(release(state, token), RELEASED);
    EXPECT_FALSE(state.shouldBlank());
}

TEST(AudioBlankState, OneRequesterCannotReleaseAnotherRequester) {
    AudioBlankState state;

    auto first = state.acquire();
    auto second = state.acquire();

    EXPECT_EQ(release(state, first), RELEASED);
    EXPECT_TRUE(state.shouldBlank());

    EXPECT_EQ(release(state, second), RELEASED);
    EXPECT_FALSE(state.shouldBlank());
}

TEST(AudioBlankState, ChannelBlankAndAudioRequestAreIndependent) {
    AudioBlankState state;

    auto slate = state.acquire();
    state.setChannelBlanked(true);

    EXPECT_EQ(release(state, slate), RELEASED);
    EXPECT_TRUE(state.shouldBlank());

    state.setChannelBlanked(false);
    EXPECT_FALSE(state.shouldBlank());
}

TEST(AudioBlankState, ChannelUnblankDoesNotOverrideAudioRequest) {
    AudioBlankState state;

    state.setChannelBlanked(true);
    auto slate = state.acquire();
    state.setChannelBlanked(false);

    EXPECT_TRUE(state.shouldBlank());

    EXPECT_EQ(release(state, slate), RELEASED);
    EXPECT_FALSE(state.shouldBlank());
}

TEST(AudioBlankState, ReleasingUnknownTokenDoesNotChangeState) {
    AudioBlankState state;

    auto token = state.acquire();

    EXPECT_EQ(release(state, token + 1), UNKNOWN);
    EXPECT_TRUE(state.shouldBlank());
}

TEST(AudioBlankState, DoubleReleaseIsReportedAsUnknown) {
    AudioBlankState state;

    auto token = state.acquire();

    EXPECT_EQ(release(state, token), RELEASED);
    EXPECT_EQ(release(state, token), UNKNOWN);
}

TEST(AudioBlankState, ResetClearsAllBlankingReasons) {
    AudioBlankState state;

    state.setChannelBlanked(true);
    state.acquire();
    state.reset();

    EXPECT_FALSE(state.shouldBlank());
}

// A channel teardown drops every token while the holders are still alive. Their eventual
// release is expected, not a fault, and must not look like the double-release above - which
// is a real bug worth a warning.
TEST(AudioBlankState, ReleaseAfterResetIsStaleRatherThanUnknown) {
    AudioBlankState state;

    auto generation = state.generation();
    auto token = state.acquire();

    state.reset();

    EXPECT_EQ(state.release(token, generation), STALE);
    EXPECT_FALSE(state.shouldBlank());
}

TEST(AudioBlankState, AnUnknownTokenInTheCurrentGenerationIsStillUnknown) {
    AudioBlankState state;

    state.acquire();
    state.reset();

    // Acquired after the reset, so this holder's generation is current: a bad token here is a
    // genuine bug and must not be excused as stale.
    auto generation = state.generation();
    auto token = state.acquire();

    EXPECT_EQ(state.release(token + 99, generation), UNKNOWN);
}

TEST(AudioBlankState, ResetBumpsTheGeneration) {
    AudioBlankState state;

    auto before = state.generation();
    state.reset();

    EXPECT_NE(state.generation(), before);
}
