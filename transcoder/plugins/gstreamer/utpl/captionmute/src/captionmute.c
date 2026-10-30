#include <gst/gst.h>
#include <gst/audio/audio-channels.h>

#include "common/plugin.h"
#include "common/metadata.h"
#include "common/utils.h"

#include "captionmute.h"
#include "types.h"

GST_DEBUG_CATEGORY (gst_utplcaptionmute_debug);
#define GST_CAT_DEFAULT gst_utplcaptionmute_debug

#define gst_utplcaptionmute_parent_class parent_class
G_DEFINE_TYPE (GstUtplCaptionMute, gst_utplcaptionmute, GST_TYPE_BASE_TRANSFORM);

GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (
    utplcaptionmute, 
    "utplcaptionmute",
    GST_RANK_PRIMARY, 
    GST_UTPLCAPTIONMUTE_TYPE,
    GST_DEBUG_CATEGORY_INIT (
        gst_utplcaptionmute_debug, "utplcaptionmute", 0, "UTPL caption muting element"
    );
);

static GstStaticPadTemplate _sinkTemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        "text/x-raw;"
        "application/x-ssa;"
        "application/x-ass;"

        "application/x-subtitle;"
        "application/x-subtitle-mpl2;"
        "application/x-subtitle-dks;"
        "application/x-subtitle-qttext;"
        "application/x-subtitle-sami;"
        "application/x-subtitle-tmplayer;"

        "application/x-teletext;"
        "application/x-kate;"
        "closedcaption/x-cea-608;"
        "closedcaption/x-cea-708;"
        "subtitle/x-kate;"
        "application/x-subtitle-vtt;"
        "subpicture/x-dvb;"
        "subpicture/x-pgs;"
        "subpicture/x-xsub;"
    )
);


static GstStaticPadTemplate _srcTemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        "text/x-raw;"
        "application/x-ssa;"
        "application/x-ass;"

        "application/x-subtitle;"
        "application/x-subtitle-mpl2;"
        "application/x-subtitle-dks;"
        "application/x-subtitle-qttext;"
        "application/x-subtitle-sami;"
        "application/x-subtitle-tmplayer;"

        "application/x-teletext;"
        "application/x-kate;"
        "closedcaption/x-cea-608;"
        "closedcaption/x-cea-708;"
        "subtitle/x-kate;"
        "application/x-subtitle-vtt;"
        "subpicture/x-dvb;"
        "subpicture/x-pgs;"
        "subpicture/x-xsub;"
    )
);

static void _utplcaptionmute_set_property(GObject *object, guint propId, const GValue *value, GParamSpec *paramSpec) {
    GstUtplCaptionMute *self = GST_UTPLCAPTIONMUTE_PROC(object);

    switch (propId) {
        case PROPERTY_MUTE:
            self->pendingMute = g_value_get_boolean(value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propId, paramSpec);
            break;
    }
}

static void _utplcaptionmute_get_property(GObject *object, guint propId, GValue *value, GParamSpec *paramSpec) {
    GstUtplCaptionMute *self = GST_UTPLCAPTIONMUTE_PROC(object);

    switch (propId) {
        case PROPERTY_MUTE:
            g_value_set_boolean(value, self->pendingMute);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propId, paramSpec);
            break;
    }
}

static gboolean _utplcaptionmute_update(GstUtplCaptionMute *self, gboolean mute) {
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

static void gst_utplcaptionmute_init(GstUtplCaptionMute *filter) {
    GST_DEBUG_OBJECT(filter, "CALL");

    filter->pendingMute = FALSE;
    filter->currentMute = filter->pendingMute;

    gst_base_transform_set_gap_aware(GST_BASE_TRANSFORM(filter), TRUE);

    _utplcaptionmute_update(filter, filter->pendingMute);
}

static void _utplcaptionmute_before_transform(GstBaseTransform *base, GstBuffer *buffer) {
    GstUtplCaptionMute *self = GST_UTPLCAPTIONMUTE_PROC(base);

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

            _utplcaptionmute_update(self, mute);
        }
    }
}

static GstFlowReturn _utplcaptionmute_transform_ip(GstBaseTransform *base, GstBuffer *outbuf) {
    GstClockTime ts;
    
    GST_TRACE_OBJECT(base, "CALL");

    ts = GST_BUFFER_TIMESTAMP(outbuf);
    if (GST_CLOCK_TIME_IS_VALID (ts)) {
        GstClockTime duration = GST_BUFFER_DURATION(outbuf);

        gst_pad_push_event(GST_BASE_TRANSFORM_SRC_PAD(base), gst_event_new_gap(ts, duration));
    }

    return GST_BASE_TRANSFORM_FLOW_DROPPED;
}


static void gst_utplcaptionmute_class_init(GstUtplCaptionMuteClass * klass) {
    GObjectClass          *gobjectClass = G_OBJECT_CLASS(klass);
    GstElementClass       *elementClass = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *baseClass    = GST_BASE_TRANSFORM_CLASS(klass);

    gst_element_class_add_static_pad_template(elementClass, &_srcTemplate);
    gst_element_class_add_static_pad_template(elementClass, &_sinkTemplate);

    {
        gobjectClass->get_property = GST_DEBUG_FUNCPTR(_utplcaptionmute_get_property);
        gobjectClass->set_property = GST_DEBUG_FUNCPTR(_utplcaptionmute_set_property);

        baseClass->before_transform = GST_DEBUG_FUNCPTR(_utplcaptionmute_before_transform);
        baseClass->transform_ip     = GST_DEBUG_FUNCPTR(_utplcaptionmute_transform_ip);

        baseClass->transform_ip_on_passthrough = FALSE;
    }

    gst_element_class_set_static_metadata(
        elementClass,
        "UTPL caption muting element", 
        "Filter/Subtitle",
        "Allows to mute caption streams",
        GST_AUTHORS
    );

    // Properties
    {
        GParamSpec *properties[PROPERTY_LAST] = { NULL, };

        properties[PROPERTY_MUTE] = g_param_spec_boolean(
            "mute", 
            "Mute", 
            "Mute caption stream", 
            FALSE, 
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        g_object_class_install_properties(
            gobjectClass, PROPERTY_LAST, properties
        );
    }
}