#include "common/queue.h"

#include "common/metadata.h"
#include "common/utils.h"

GST_DEBUG_CATEGORY_EXTERN(gst_dlbpatcommon_debug);
#define GST_CAT_DEFAULT gst_dlbpatcommon_debug


gboolean dlbpat_queue_output(DlbPatTimeline *timeline, GQueue *queue, GstBuffer *buffer, dlb_pamif_md_segment *md, guint samples) {
    GstClockTime pts, duration;
    gboolean discont;
    guint tag;

    if (! samples) {
        gst_buffer_unref(buffer);

        return FALSE;
    }

    if (dlbpat_timeline_take(timeline, samples, &pts, &duration, &tag, &discont)) {
        gsize size = DLBPAT_SAMPLES_SIZE(samples);

        gst_buffer_set_size(buffer, size);

        dlbpat_metadata_add(buffer, md, size, tag);

        GST_BUFFER_PTS(buffer)      = pts;
        GST_BUFFER_DTS(buffer)      = GST_CLOCK_TIME_NONE;
        GST_BUFFER_DURATION(buffer) = duration;

        if (discont) {
            GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DISCONT);
        }

        g_queue_push_tail(queue, buffer);

    } else {
        gst_buffer_unref(buffer);
    }

    return TRUE;
}


gboolean dlbpat_queue_hold_event(DlbPatTimeline *timeline, GQueue *queue, GstEvent *event) {
    if (dlbpat_timeline_hold_event(timeline, event)) {
        return TRUE;
    }

    // Nothing pending in the library, but buffers ahead of the event have not left yet
    if (! g_queue_is_empty(queue)) {
        g_queue_push_tail(queue, event);

        return TRUE;
    }

    return FALSE;
}


GstBuffer *dlbpat_queue_next(GstBaseTransform *trans, GQueue *queue, DlbPatEventFunc eventFunc) {
    GstMiniObject *item;

    while ((item = g_queue_pop_head(queue))) {
        if (GST_IS_BUFFER(item)) {
            return GST_BUFFER_CAST(item);
        }

        eventFunc(trans, GST_EVENT_CAST(item));
    }

    return NULL;
}


GstFlowReturn dlbpat_queue_push_all(GstBaseTransform *trans, GQueue *queue, DlbPatEventFunc eventFunc) {
    GstFlowReturn ret = GST_FLOW_OK;
    GstMiniObject *item;

    while ((item = g_queue_pop_head(queue))) {
        if (GST_IS_BUFFER(item)) {
            if (ret == GST_FLOW_OK) {
                ret = gst_pad_push(GST_BASE_TRANSFORM_SRC_PAD(trans), GST_BUFFER_CAST(item));

            } else {
                gst_buffer_unref(GST_BUFFER_CAST(item));
            }

        } else {
            eventFunc(trans, GST_EVENT_CAST(item));
        }
    }

    return ret;
}


void dlbpat_queue_clear(GQueue *queue) {
    g_queue_clear_full(queue, (GDestroyNotify) gst_mini_object_unref);
}
