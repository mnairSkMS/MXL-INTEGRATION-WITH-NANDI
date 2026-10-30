#include <gst/gst.h>
#include <gst/audio/audio-channels.h>

#include "common/plugin.h"
#include "common/metadata.h"
#include "common/utils.h"

#include "audiomute.h"
#include "types.h"

GST_DEBUG_CATEGORY (gst_utplaudiomute_debug);
#define GST_CAT_DEFAULT gst_utplaudiomute_debug

#define gst_utplmute_parent_class parent_class
G_DEFINE_TYPE (GstUtplAudioMute, gst_utplaudiomute, GST_TYPE_AUDIO_FILTER);

GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (
    utplaudiomute, 
    "utplaudiomute",
    GST_RANK_PRIMARY, 
    GST_UTPLAUDIOMUTE_TYPE,
    GST_DEBUG_CATEGORY_INIT (
        gst_utplaudiomute_debug, "utplaudiomute", 0, "UTPL audio muting element"
    );
);

static GstStaticPadTemplate _sinkTemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        DLBPAT_MIMETYPE_ES "(" DLBPAT_CAPS_FEATURE_METADATA "), "
        "format = (string) " GST_AUDIO_NE (S32) ", "
        "layout = (string) interleaved, "
        "rate = (int) " G_STRINGIFY(DLBPAT_DEFAULT_SAMPLE_RATE) ", "
        "channels = (int) 16, "
        "channel-mask=(bitmask)0x0;"

        DLBPAT_MIMETYPE_ES ", "
        "format = (string) {"
            "S8" ","
            "U8" "," 
            GST_AUDIO_NE(S16) ","
            GST_AUDIO_OE(S16) ","
            GST_AUDIO_NE(S20) ","
            GST_AUDIO_OE(S20) ","
            GST_AUDIO_NE(S24) ","
            GST_AUDIO_OE(S24) ","
            GST_AUDIO_NE(S24_32) ","
            GST_AUDIO_OE(S24_32) ","
            GST_AUDIO_NE(S32) ","
            GST_AUDIO_OE(S32) ","
            
            GST_AUDIO_NE(U16) ","
            GST_AUDIO_OE(U16) ","
            GST_AUDIO_NE(U20) ","
            GST_AUDIO_OE(U20) ","
            GST_AUDIO_NE(U24) ","
            GST_AUDIO_OE(U24) ","
            GST_AUDIO_NE(U24_32) ","
            GST_AUDIO_OE(U24_32) ","
            GST_AUDIO_NE(U32) ","
            GST_AUDIO_OE(U32) ","
        "};"
    )
);


static GstStaticPadTemplate _srcTemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        DLBPAT_MIMETYPE_ES "(" DLBPAT_CAPS_FEATURE_METADATA "), "
        "format = (string) " GST_AUDIO_NE (S32) ","
        "layout = (string) interleaved, "
        "rate = (int) " G_STRINGIFY(DLBPAT_DEFAULT_SAMPLE_RATE) ", "
        "channels = (int) 16, "
        "channel-mask=(bitmask)0x0;"

        DLBPAT_MIMETYPE_ES ", "
        "format = (string) {"
            "S8" ","
            "U8" "," 
            GST_AUDIO_NE(S16) ","
            GST_AUDIO_OE(S16) ","
            GST_AUDIO_NE(S20) ","
            GST_AUDIO_OE(S20) ","
            GST_AUDIO_NE(S24) ","
            GST_AUDIO_OE(S24) ","
            GST_AUDIO_NE(S24_32) ","
            GST_AUDIO_OE(S24_32) ","
            GST_AUDIO_NE(S32) ","
            GST_AUDIO_OE(S32) ","
            
            GST_AUDIO_NE(U16) ","
            GST_AUDIO_OE(U16) ","
            GST_AUDIO_NE(U20) ","
            GST_AUDIO_OE(U20) ","
            GST_AUDIO_NE(U24) ","
            GST_AUDIO_OE(U24) ","
            GST_AUDIO_NE(U24_32) ","
            GST_AUDIO_OE(U24_32) ","
            GST_AUDIO_NE(U32) ","
            GST_AUDIO_OE(U32) ","
        "};"
    )
);

static void _utplaudiomute_set_property(GObject *object, guint propId, const GValue *value, GParamSpec *paramSpec) {
    GstUtplAudioMute *self = GST_UTPLAUDIOMUTE_PROC(object);

    switch (propId) {
        case PROPERTY_MUTE:
            self->pendingMute = g_value_get_boolean(value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propId, paramSpec);
            break;
    }
}

static void _utplaudiomute_get_property(GObject *object, guint propId, GValue *value, GParamSpec *paramSpec) {
    GstUtplAudioMute *self = GST_UTPLAUDIOMUTE_PROC(object);

    switch (propId) {
        case PROPERTY_MUTE:
            g_value_set_boolean(value, self->pendingMute);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propId, paramSpec);
            break;
    }
}

static void gst_utplaudiomute_init(GstUtplAudioMute *filter) {
    GST_DEBUG_OBJECT(filter, "CALL");

    filter->pendingMute = FALSE;
    filter->currentMute = filter->pendingMute;
    

    gst_base_transform_set_gap_aware(GST_BASE_TRANSFORM(filter), TRUE);
}

static gboolean _utplaudiomute_update(GstUtplAudioMute *self, const GstAudioInfo *info, gboolean mute) {
    GST_DEBUG("CALL");

    if (self->currentMute != mute) {
        self->currentMute = mute;
    }

    if (! self->currentMute) {
        gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(self), TRUE);

    } else {
        gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(self), FALSE);
    }

    return TRUE;
}

static gboolean _utplaudiomute_setup (GstAudioFilter *filter, const GstAudioInfo *info) {
    GstUtplAudioMute *self = GST_UTPLAUDIOMUTE_PROC(filter);

    GST_TRACE_OBJECT(self, "CALL");

    {
        gboolean mute;

        GST_OBJECT_LOCK(self);
        {
            mute = self->pendingMute;
        }
        GST_OBJECT_UNLOCK(self);

        return _utplaudiomute_update(self, info, mute);
    }
}

static void _utplaudiomute_before_transform(GstBaseTransform *base, GstBuffer *buffer) {
    GstUtplAudioMute *self = GST_UTPLAUDIOMUTE_PROC(base);

    GST_TRACE_OBJECT(base, "CALL");

    {
        GstClockTime timestamp;

        timestamp = GST_BUFFER_TIMESTAMP(buffer);
        timestamp = gst_segment_to_stream_time(
            &base->segment, GST_FORMAT_TIME, timestamp
        );

        if (GST_CLOCK_TIME_IS_VALID(timestamp)) {
            gst_object_sync_values(GST_OBJECT(self), timestamp);
        }
    }

    {
        gboolean mute;

        GST_OBJECT_LOCK(self);
        {
            mute = self->pendingMute;
        }
        GST_OBJECT_UNLOCK(self);

        if (mute != self->currentMute) {
            GST_DEBUG("Mute flag has changed! %d -> %d", self->currentMute, mute);

            _utplaudiomute_update(self, GST_AUDIO_FILTER_INFO(self), mute);
        }
    }
}

static GstFlowReturn _utplaudiomute_transform_ip(GstBaseTransform *base, GstBuffer *outbuf) {
    GST_TRACE_OBJECT(base, "CALL");

    // don't process data with GAP
    if (GST_BUFFER_FLAG_IS_SET (outbuf, GST_BUFFER_FLAG_GAP))
        return GST_FLOW_OK;

    {
        GstMapInfo map;

        gst_buffer_map(outbuf, &map, GST_MAP_READWRITE);
        {
            memset(map.data, 0, map.size);

            GST_BUFFER_FLAG_SET(outbuf, GST_BUFFER_FLAG_GAP);
        }
        gst_buffer_unmap(outbuf, &map);
    }

    return GST_FLOW_OK;
}


static void gst_utplaudiomute_class_init(GstUtplAudioMuteClass * klass) {
    GObjectClass          *gobjectClass = G_OBJECT_CLASS(klass);
    GstElementClass       *elementClass = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *baseClass    = GST_BASE_TRANSFORM_CLASS(klass);
    GstAudioFilterClass   *filterClass  = GST_AUDIO_FILTER_CLASS(klass);

    gst_element_class_add_static_pad_template(elementClass, &_srcTemplate);
    gst_element_class_add_static_pad_template(elementClass, &_sinkTemplate);

    {
        gobjectClass->get_property = GST_DEBUG_FUNCPTR(_utplaudiomute_get_property);
        gobjectClass->set_property = GST_DEBUG_FUNCPTR(_utplaudiomute_set_property);

        baseClass->before_transform = GST_DEBUG_FUNCPTR(_utplaudiomute_before_transform);
        baseClass->transform_ip     = GST_DEBUG_FUNCPTR(_utplaudiomute_transform_ip);

        baseClass->transform_ip_on_passthrough = FALSE;

        filterClass->setup = GST_DEBUG_FUNCPTR(_utplaudiomute_setup);
    }

    gst_element_class_set_static_metadata(
        elementClass,
        "UTPL audio muting element", 
        "Filter/Audio",
        "Allows to mute audio streams",
        GST_AUTHORS
    );

    // Properties
    {
        GParamSpec *properties[PROPERTY_LAST] = { NULL, };

        properties[PROPERTY_MUTE] = g_param_spec_boolean(
            "mute", 
            "Mute", 
            "Mute audio stream", 
            FALSE, 
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        g_object_class_install_properties(
            gobjectClass, PROPERTY_LAST, properties
        );
    }
}