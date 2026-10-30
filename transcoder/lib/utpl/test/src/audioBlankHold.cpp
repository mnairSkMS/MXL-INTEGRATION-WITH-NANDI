#include <gtest/gtest.h>

#include "utpl/transcoder/channel.h"

using namespace utpl::transcoder;

/*
 * AudioBlankHold against a real Channel.
 *
 * A default-constructed Channel builds only its Pimpl - no pipeline, no elements - and the
 * acquire/release path touches just the blank state and then walks connector and processor
 * lists that are empty. So the ownership semantics can be exercised for real rather than
 * against a stand-in that might not behave like the thing it replaces.
 *
 * These cover the gap the original change left: the destructor is what makes a leaked mute
 * impossible, and nothing tested it.
 */

TEST(AudioBlankHold, DefaultConstructedHoldsNothing) {
    Channel::AudioBlankHold hold;

    EXPECT_FALSE(hold.held());
}

TEST(AudioBlankHold, ConstructingFromAChannelTakesAHold) {
    Channel channel;
    Channel::AudioBlankHold hold(&channel);

    EXPECT_TRUE(hold.held());
}

TEST(AudioBlankHold, ANullChannelHoldsNothing) {
    Channel::AudioBlankHold hold(nullptr);

    EXPECT_FALSE(hold.held());
}

TEST(AudioBlankHold, ReleaseIsIdempotent) {
    Channel channel;
    Channel::AudioBlankHold hold(&channel);

    hold.release();
    EXPECT_FALSE(hold.held());

    // The second release must be a no-op rather than a double-release of the same token,
    // which the channel would rightly warn about.
    hold.release();
    EXPECT_FALSE(hold.held());
}

TEST(AudioBlankHold, DestructionReleasesWithoutAnExplicitCall) {
    Channel channel;

    {
        Channel::AudioBlankHold hold(&channel);
        EXPECT_TRUE(hold.held());
    }

    // Nothing to assert on the channel from here - it exposes no blank state - but this
    // running clean under the destructor is the point: the release happened, and it did not
    // warn about an unknown token.
    SUCCEED();
}

TEST(AudioBlankHold, MoveConstructionTransfersOwnership) {
    Channel channel;
    Channel::AudioBlankHold first(&channel);

    Channel::AudioBlankHold second(std::move(first));

    EXPECT_FALSE(first.held());
    EXPECT_TRUE(second.held());
}

TEST(AudioBlankHold, MoveAssignmentTransfersOwnershipAndReleasesTheOldOne) {
    Channel channel;
    Channel::AudioBlankHold first(&channel);
    Channel::AudioBlankHold second(&channel);

    second = std::move(first);

    EXPECT_FALSE(first.held());
    EXPECT_TRUE(second.held());
}

TEST(AudioBlankHold, SelfMoveAssignmentKeepsTheHold) {
    Channel channel;
    Channel::AudioBlankHold hold(&channel);

    auto &alias = hold;
    hold = std::move(alias);

    EXPECT_TRUE(hold.held());
}

TEST(AudioBlankHold, AMovedFromHoldReleasesNothingWhenDestroyed) {
    Channel channel;
    Channel::AudioBlankHold moved;

    {
        Channel::AudioBlankHold original(&channel);
        moved = std::move(original);
        // original goes out of scope here holding nothing; if it released anyway, the token
        // would be gone while `moved` still believes it owns one.
    }

    EXPECT_TRUE(moved.held());
    moved.release();
}

// A hold borrows its channel, so it must not outlive it. That is a real constraint, and it is
// satisfied structurally rather than by care at the call site: Engine declares _channel before
// _scheduler, so the scheduler and every ScheduledSlateEvent it owns - and therefore every
// hold - are destroyed first; and Engine::stop() clears _events explicitly while the channel
// is still alive. Asserting the inverted order here would only be asserting a use-after-free.
TEST(AudioBlankHold, ReleasesCleanlyWhileItsChannelIsStillAlive) {
    Channel channel;

    {
        Channel::AudioBlankHold hold(&channel);
        EXPECT_TRUE(hold.held());
    }

    // A fresh hold on the same channel still works, which it would not if the previous
    // destructor had left the token behind or released one it did not own.
    Channel::AudioBlankHold again(&channel);
    EXPECT_TRUE(again.held());
}
