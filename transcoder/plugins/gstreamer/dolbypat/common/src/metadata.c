#include "common/metadata.h"

#include "common/utils.h"

GST_DEBUG_CATEGORY_EXTERN(gst_dlbpatcommon_debug);
#define GST_CAT_DEFAULT gst_dlbpatcommon_debug


static gboolean _metadata_init(DlbPatMetadata *meta, gpointer G_GNUC_UNUSED params, GstBuffer G_GNUC_UNUSED *buffer) {
    meta->md        = NULL;
    meta->blockSize = 0;

    meta->sourceChannels = 0;

    return TRUE;
}


static void _metadata_free(DlbPatMetadata *meta, GstBuffer G_GNUC_UNUSED * buffer) {
    if (NOT_NULL(meta->md)) {
        dlb_pamif_md_segment_destroy(&meta->md);
    }
}

static gboolean _metadata_transform(GstBuffer *dest, DlbPatMetadata *meta, GstBuffer G_GNUC_UNUSED *buffer, GQuark type, GstMetaTransformCopy *data) {
    if (! data->region) {
        dlbpat_metadata_add(dest, meta->md, meta->blockSize, meta->sourceChannels);

    } else {
        // A clipped block (segment boundary at an item change) no longer matches its metadata segment. Not copying
        // it is the intended result, so report success: FALSE makes GStreamer log a failed copy.
        GST_DEBUG("Dropping Dolby PAT metadata of a partially copied buffer");
    }

    return TRUE;
}


GType dlbpat_metadata_api_get_type() {
    static GType type;

    {
        static const gchar *tags[] = { "dlbpatmd", NULL };

        if (g_once_init_enter (&type)) {
            GType _type = gst_meta_api_type_register("DlbPatMetadataType", tags);
        
            g_once_init_leave (&type, _type);
        }
    }

    return type;
}


const GstMetaInfo *dlbpat_metadata_get_info(void) {
    static const GstMetaInfo *object_audio_meta_info = NULL;

    if (g_once_init_enter ((GstMetaInfo **) & object_audio_meta_info)) {
        const GstMetaInfo *meta = gst_meta_register(
            DLBPAT_METADATA_API_TYPE,
            "DlbPatMetadata", 
            sizeof(DlbPatMetadata),
            (GstMetaInitFunction)      _metadata_init,
            (GstMetaFreeFunction)      _metadata_free,
            (GstMetaTransformFunction) _metadata_transform
        );

        g_once_init_leave ((GstMetaInfo **) & object_audio_meta_info, (GstMetaInfo *) meta);
    }

    return object_audio_meta_info;
}


DlbPatMetadata *dlbpat_metadata_add(GstBuffer *buffer, dlb_pamif_md_segment *md, gsize blockSize, guint sourceChannels) {
    DlbPatMetadata *meta = (DlbPatMetadata *) gst_buffer_add_meta(buffer, DLBPAT_METADATA_INFO, NULL);

    dlb_pamif_md_segment_create_copy(&meta->md, md);
    
    meta->blockSize = blockSize;

    meta->sourceChannels = sourceChannels;
    
    return meta;
}


DlbPatMetadata *dlbpat_metadata_get(GstBuffer *buffer) {
    return (DlbPatMetadata *) gst_buffer_get_meta (buffer, DLBPAT_METADATA_API_TYPE);
}


gsize dlbpat_metadata_count(GstBuffer *buffer) {
    return gst_buffer_get_n_meta(buffer, DLBPAT_METADATA_API_TYPE);
}


DlbPatMetadata *dlbpat_metadata_at(GstBuffer *buffer, gint index) {
    gpointer state = NULL;
    GstMeta *meta;
    guint n = 0;

    while ((meta = gst_buffer_iterate_meta_filtered(buffer, &state, DLBPAT_METADATA_API_TYPE))) {
        if (index-- == 0) {
            return (DlbPatMetadata *) meta;
        }
    }

    return NULL;
}
