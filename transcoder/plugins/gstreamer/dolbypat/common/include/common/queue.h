#ifndef __DLBPAT_COMMON_QUEUE_H__
#define __DLBPAT_COMMON_QUEUE_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include "common/dolby.h"
#include "common/timeline.h"

G_BEGIN_DECLS

/*
 * Output queue of the Dolby PCM processors: processed buffers and the serialized events held between them
 * (see common/timeline.h), in the order they must leave the element.
 */

typedef gboolean (*DlbPatEventFunc)(GstBaseTransform *trans, GstEvent *event);

// Queues `buffer` holding `samples` samples just read from the library, stamped by the timeline and tagged with
// the source channel count. Returns FALSE (and drops the buffer) when no samples were read.
gboolean dlbpat_queue_output(DlbPatTimeline *timeline, GQueue *queue, GstBuffer *buffer, dlb_pamif_md_segment *md, guint samples);

// Holds a serialized event behind the pending samples. Returns FALSE when it can be forwarded right away.
gboolean dlbpat_queue_hold_event(DlbPatTimeline *timeline, GQueue *queue, GstEvent *event);

// Dispatches the queued events (through `eventFunc`) up to the next buffer and returns that buffer, or NULL.
GstBuffer *dlbpat_queue_next(GstBaseTransform *trans, GQueue *queue, DlbPatEventFunc eventFunc);

// Pushes every queued buffer and dispatches every queued event.
GstFlowReturn dlbpat_queue_push_all(GstBaseTransform *trans, GQueue *queue, DlbPatEventFunc eventFunc);

void dlbpat_queue_clear(GQueue *queue);

G_END_DECLS

#endif /* __DLBPAT_COMMON_QUEUE_H__ */
