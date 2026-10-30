#include "utpl/common/types.h"

#include <gtest/gtest.h>

using namespace utpl;


static const Rectangle HD(0, 0, 1920, 1080);
static const Rectangle UHD(0, 0, 3840, 2160);


static void expectRect(const Rectangle &actual, int x, int y, int w, int h) {
    EXPECT_EQ(actual.x(),      x);
    EXPECT_EQ(actual.y(),      y);
    EXPECT_EQ(actual.width(),  w);
    EXPECT_EQ(actual.height(), h);
}

TEST(common_norm_rect, full_frame_resolves_to_the_whole_output) {
    NormRect full(0.0, 0.0, 1.0, 1.0);

    expectRect(full.resolve(HD),  0, 0, 1920, 1080);
    expectRect(full.resolve(UHD), 0, 0, 3840, 2160);
}

// The crop-vs-resize branch in Channel::squeeze compares the resolved source window
// against the compositor frame, so a full-frame fraction must land on it exactly.
TEST(common_norm_rect, resolved_full_frame_equals_the_compositor_frame) {
    NormRect full(0.0, 0.0, 1.0, 1.0);

    EXPECT_EQ(full.resolve(HD),  HD);
    EXPECT_EQ(full.resolve(UHD), UHD);
}

TEST(common_norm_rect, shipped_squeeze_windows_round_trip) {
    // lband-voltedge: 1500x825 of a 1920x1080 canvas.
    NormRect voltedge(0.0, 0.0, 0.78125, 0.763889);

    expectRect(voltedge.resolve(HD),  0, 0, 1500, 825);
    expectRect(voltedge.resolve(UHD), 0, 0, 3000, 1650);

    // lband-playboxtv: offset origin, programme to the right of the L.
    NormRect playbox(0.161979, 0.0, 0.838021, 0.822222);

    expectRect(playbox.resolve(HD),  311, 0, 1609, 888);
    expectRect(playbox.resolve(UHD), 622, 0, 3218, 1776);
}

// Odd dimensions are load-bearing: aston-static is 1920x917 and its artwork hole
// matches that exactly, so rounding must not snap results to even numbers.
TEST(common_norm_rect, odd_dimensions_are_preserved) {
    NormRect aston(0.0, 0.0, 1.0, 0.849074);

    expectRect(aston.resolve(HD), 0, 0, 1920, 917);

    NormRect voltedge(0.0, 0.0, 0.78125, 0.763889);

    EXPECT_EQ(voltedge.resolve(HD).height() % 2, 1);
}

// Negative components are the "auto" sentinel that calcWindow resolves later against
// the source frame; scaling them would destroy that meaning.
TEST(common_norm_rect, the_auto_sentinel_passes_through_unscaled) {
    expectRect(NormRect().resolve(UHD), -1, -1, -1, -1);

    NormRect partial(0.5, NormRect::AUTO, 0.25, NormRect::AUTO);

    expectRect(partial.resolve(UHD), 1920, -1, 960, -1);
}

TEST(common_norm_rect, equality_covers_every_component) {
    NormRect a(0.0, 0.0, 0.78125, 0.763889);

    EXPECT_EQ(a, NormRect(0.0, 0.0, 0.78125, 0.763889));
    EXPECT_NE(a, NormRect(0.0, 0.0, 0.78125, 0.5));
    EXPECT_NE(a, NormRect(0.1, 0.0, 0.78125, 0.763889));
    EXPECT_EQ(NormRect(), NormRect());
}
