#include "common/timeline.h"

GST_DEBUG_CATEGORY_EXTERN(gst_dlbpatcommon_debug);
#define GST_CAT_DEFAULT gst_dlbpatcommon_debug

// Timestamp jitter tolerated before a new anchor is taken (rounding of container timestamps).
#define DLBPAT_TIMELINE_TOLERANCE (GST_MSECOND)


typedef struct _DlbPatMark {
    guint64 index;

    // Held event, or NULL for a timestamp/layout anchor
    GstEvent *event;

    GstClockTime pts;
    guint        tag;
    gboolean     discont;
} DlbPatMark;


static void _mark_free(DlbPatMark *mark) {
    if (mark->event) {
        gst_event_unref(mark->event);
    }

    g_free(mark);
}


static GstClockTime _samples_to_time(const DlbPatTimeline *timeline, guint64 samples) {
    return gst_util_uint64_scale_int(samples, GST_SECOND, timeline->rate);
}


void dlbpat_timeline_init(DlbPatTimeline *timeline, gint rate) {
    g_queue_init(&timeline->marks);

    timeline->rate = rate;

    dlbpat_timeline_clear(timeline);
}


void dlbpat_timeline_clear(DlbPatTimeline *timeline) {
    g_queue_clear_full(&timeline->marks, (GDestroyNotify) _mark_free);

    timeline->inCount  = 0;
    timeline->outCount = 0;
    timeline->priming  = 0;

    timeline->haveInput = FALSE;
    timeline->nextInPts   = GST_CLOCK_TIME_NONE;
    timeline->inAnchorPts = GST_CLOCK_TIME_NONE;
    timeline->inAnchor    = 0;
    timeline->inTag       = 0;

    timeline->outPts     = GST_CLOCK_TIME_NONE;
    timeline->outAnchor  = 0;
    timeline->outTag     = 0;
    timeline->outDiscont = FALSE;
}


void dlbpat_timeline_start(DlbPatTimeline *timeline, guint latency) {
    if (timeline->outCount != timeline->inCount) {
        GST_WARNING("Library reopened with %" G_GUINT64_FORMAT " samples unaccounted for", timeline->inCount - timeline->outCount);

        timeline->outCount = timeline->inCount;
    }

    timeline->priming = latency;
}


void dlbpat_timeline_input(DlbPatTimeline *timeline, GstClockTime pts, guint64 samples, guint tag, gboolean discont) {
    gboolean anchor = ! timeline->haveInput || discont || tag != timeline->inTag;

    if (! anchor && GST_CLOCK_TIME_IS_VALID(pts)) {
        if (GST_CLOCK_TIME_IS_VALID(timeline->nextInPts)) {
            GstClockTimeDiff drift = GST_CLOCK_DIFF(timeline->nextInPts, pts);

            anchor = ABS(drift) > (GstClockTimeDiff) DLBPAT_TIMELINE_TOLERANCE;

        } else {
            anchor = TRUE;
        }
    }

    if (anchor) {
        DlbPatMark *mark = g_new0(DlbPatMark, 1);

        mark->index   = timeline->inCount;
        mark->pts     = GST_CLOCK_TIME_IS_VALID(pts) ? pts : timeline->nextInPts;
        mark->tag     = tag;
        mark->discont = discont;

        g_queue_push_tail(&timeline->marks, mark);

        timeline->inAnchorPts = mark->pts;
        timeline->inAnchor    = mark->index;
        timeline->inTag       = tag;
        timeline->haveInput   = TRUE;

        GST_LOG("Anchor at sample %" G_GUINT64_FORMAT ": pts %" GST_TIME_FORMAT ", source channels %u",
            mark->index, GST_TIME_ARGS(mark->pts), tag
        );
    }

    // Derived from the anchor, so the expected position does not accumulate rounding
    if (GST_CLOCK_TIME_IS_VALID(timeline->inAnchorPts)) {
        timeline->nextInPts = timeline->inAnchorPts + _samples_to_time(timeline, timeline->inCount + samples - timeline->inAnchor);
    }

    timeline->inCount += samples;
}


gboolean dlbpat_timeline_hold_event(DlbPatTimeline *timeline, GstEvent *event) {
    if (timeline->inCount == timeline->outCount) {
        return FALSE;
    }

    {
        DlbPatMark *mark = g_new0(DlbPatMark, 1);

        mark->index = timeline->inCount;
        mark->event = event;

        g_queue_push_tail(&timeline->marks, mark);
    }

    GST_LOG("Holding %s event until sample %" G_GUINT64_FORMAT, GST_EVENT_TYPE_NAME(event), timeline->inCount);

    return TRUE;
}


void dlbpat_timeline_due(DlbPatTimeline *timeline, GQueue *events) {
    if (timeline->priming) {
        return;
    }

    for (DlbPatMark *mark = g_queue_peek_head(&timeline->marks); mark && mark->index <= timeline->outCount; mark = g_queue_peek_head(&timeline->marks)) {
        g_queue_pop_head(&timeline->marks);

        if (mark->event) {
            g_queue_push_tail(events, mark->event);

            mark->event = NULL;

        } else {
            timeline->outPts     = mark->pts;
            timeline->outAnchor  = mark->index;
            timeline->outTag     = mark->tag;
            timeline->outDiscont = timeline->outDiscont || mark->discont;
        }

        _mark_free(mark);
    }
}


guint dlbpat_timeline_chunk(const DlbPatTimeline *timeline, guint available) {
    guint64 limit;

    if (timeline->priming) {
        return MIN(available, timeline->priming);
    }

    if (timeline->outCount >= timeline->inCount) {
        // Surplus samples (e.g. a flush tail longer than the latency) are read and dropped
        return available;
    }

    limit = timeline->inCount - timeline->outCount;

    {
        DlbPatMark *next = timeline->marks.head ? timeline->marks.head->data : NULL;

        if (next && next->index > timeline->outCount) {
            limit = MIN(limit, next->index - timeline->outCount);
        }
    }

    return (guint) MIN((guint64) available, limit);
}


gboolean dlbpat_timeline_take(DlbPatTimeline *timeline, guint samples, GstClockTime *pts, GstClockTime *duration, guint *tag, gboolean *discont) {
    if (timeline->priming) {
        timeline->priming -= MIN(samples, timeline->priming);

        return FALSE;
    }

    if (timeline->outCount >= timeline->inCount) {
        GST_DEBUG("Dropping %u samples without input counterpart", samples);

        return FALSE;
    }

    samples = (guint) MIN((guint64) samples, timeline->inCount - timeline->outCount);

    if (GST_CLOCK_TIME_IS_VALID(timeline->outPts)) {
        GstClockTime start = timeline->outPts + _samples_to_time(timeline, timeline->outCount - timeline->outAnchor);
        GstClockTime end   = timeline->outPts + _samples_to_time(timeline, timeline->outCount + samples - timeline->outAnchor);

        *pts      = start;
        *duration = end - start;

    } else {
        *pts      = GST_CLOCK_TIME_NONE;
        *duration = _samples_to_time(timeline, samples);
    }

    *tag     = timeline->outTag;
    *discont = timeline->outDiscont;

    timeline->outDiscont = FALSE;
    timeline->outCount  += samples;

    return TRUE;
}


void dlbpat_timeline_finish(DlbPatTimeline *timeline, GQueue *events) {
    if (timeline->outCount != timeline->inCount) {
        GST_WARNING("Drain left %" G_GINT64_FORMAT " samples unaccounted for", (gint64) (timeline->inCount - timeline->outCount));
    }

    timeline->priming  = 0;
    timeline->outCount = MAX(timeline->outCount, timeline->inCount);

    dlbpat_timeline_due(timeline, events);

    // Anything left was queued past the input position; keep its order
    for (DlbPatMark *mark = g_queue_pop_head(&timeline->marks); mark; mark = g_queue_pop_head(&timeline->marks)) {
        if (mark->event) {
            g_queue_push_tail(events, mark->event);

            mark->event = NULL;
        }

        _mark_free(mark);
    }

    timeline->outCount = timeline->inCount;
}


guint64 dlbpat_timeline_pending(const DlbPatTimeline *timeline) {
    return timeline->inCount > timeline->outCount ? timeline->inCount - timeline->outCount : 0;
}
