#ifndef __DLBPAT_COMMON_METADATA_H__
#define __DLBPAT_COMMON_METADATA_H__

#include <gst/gst.h>

#include "common/dolby.h"

G_BEGIN_DECLS

#define DLBPAT_METADATA_NAME         "DlbPatMetadata"
#define DLBPAT_CAPS_FEATURE_METADATA "meta:" DLBPAT_METADATA_NAME
#define DLBPAT_METADATA_API_TYPE     (dlbpat_metadata_api_get_type())
#define DLBPAT_METADATA_INFO         (dlbpat_metadata_get_info())

typedef struct _DlbPatMetadata {
    GstMeta parent;

    dlb_pamif_md_segment *md;
    gsize blockSize;

    // Channel count of the stream this block was decoded or converted from (0 = unknown). PAMIF metadata
    // cannot be queried, so this is what tells dlbpatmx whether a block started as stereo or surround.
    guint sourceChannels;
} DlbPatMetadata;

GType dlbpat_metadata_api_get_type();

const GstMetaInfo *dlbpat_metadata_get_info(void);

DlbPatMetadata *dlbpat_metadata_add(GstBuffer *buffer, dlb_pamif_md_segment *md, gsize blockSize, guint sourceChannels);

DlbPatMetadata *dlbpat_metadata_get(GstBuffer *buffer);

gsize dlbpat_metadata_count(GstBuffer *buffer);

DlbPatMetadata *dlbpat_metadata_at(GstBuffer *buffer, gint index);

G_END_DECLS

#endif /* __DLBPAT_COMMON_METADATA_H__ */
