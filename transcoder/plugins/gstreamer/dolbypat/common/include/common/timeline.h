#ifndef __DLBPAT_COMMON_TIMELINE_H__
#define __DLBPAT_COMMON_TIMELINE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/*
 * Sample accounting for the Dolby PCM processors (PALOUD, PADMX).
 *
 * The libraries delay their output by a fixed number of samples: a freshly opened instance first emits
 * `latency` samples of priming silence, and a flush returns the samples still held inside. Dropping the
 * priming makes output sample N the processed version of input sample N, so each output block can carry
 * the timestamp, source layout and serialized events of the input samples it came from.
 *
 * Input runs are recorded as marks (a new timestamp anchor or source layout); serialized events are held
 * as marks at the input position where they arrived. A library read never spans a mark, so a timestamp
 * jump, a layout change or a segment change lands exactly on a block boundary of the output.
 */

typedef struct _DlbPatTimeline {
    GQueue marks;

    gint rate;

    guint64 inCount;
    guint64 outCount;
    guint   priming;

    gboolean     haveInput;
    GstClockTime nextInPts;
    GstClockTime inAnchorPts;
    guint64      inAnchor;
    guint        inTag;

    GstClockTime outPts;
    guint64      outAnchor;
    guint        outTag;
    gboolean     outDiscont;
} DlbPatTimeline;

void dlbpat_timeline_init(DlbPatTimeline *timeline, gint rate);

// Forgets all samples and drops the held events (flush, stop).
void dlbpat_timeline_clear(DlbPatTimeline *timeline);

// The library was (re)opened and will emit `latency` samples of priming first.
void dlbpat_timeline_start(DlbPatTimeline *timeline, guint latency);

// Records `samples` input samples starting at `pts` that came from a `tag`-channel source.
void dlbpat_timeline_input(DlbPatTimeline *timeline, GstClockTime pts, guint64 samples, guint tag, gboolean discont);

// Holds a serialized event until the output reaches the current input position. Returns FALSE (and does not
// take the event) when no input samples are pending, so the event can be forwarded right away.
gboolean dlbpat_timeline_hold_event(DlbPatTimeline *timeline, GstEvent *event);

// Moves the events due before the next output sample to `events` (oldest first).
void dlbpat_timeline_due(DlbPatTimeline *timeline, GQueue *events);

// How many of the `available` library samples the next read may take, so that a read never spans a mark.
guint dlbpat_timeline_chunk(const DlbPatTimeline *timeline, guint available);

// Accounts `samples` library samples just read. Returns FALSE when they are priming or have no input
// counterpart and must be dropped; otherwise fills their timestamp, duration, source tag and discont flag.
gboolean dlbpat_timeline_take(DlbPatTimeline *timeline, guint samples, GstClockTime *pts, GstClockTime *duration, guint *tag, gboolean *discont);

// After a drain: moves every remaining event to `events` and aligns the output with the input.
void dlbpat_timeline_finish(DlbPatTimeline *timeline, GQueue *events);

// Input samples not yet emitted.
guint64 dlbpat_timeline_pending(const DlbPatTimeline *timeline);

G_END_DECLS

#endif /* __DLBPAT_COMMON_TIMELINE_H__ */
