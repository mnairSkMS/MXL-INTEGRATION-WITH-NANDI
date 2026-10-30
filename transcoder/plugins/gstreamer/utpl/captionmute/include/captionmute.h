#ifndef __UTPLCAPTIONMUTE_H__
#define __UTPLCAPTIONMUTE_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/base/gstadapter.h>
#include <gst/audio/audio-info.h>

G_BEGIN_DECLS

#define GST_UTPLCAPTIONMUTE_TYPE (gst_utplcaptionmute_get_type())

#define GST_UTPLCAPTIONMUTE_PROC(obj)       (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_UTPLCAPTIONMUTE_TYPE, GstUtplCaptionMute))
#define GST_UTPLCAPTIONMUTE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),  GST_UTPLCAPTIONMUTE_TYPE, GstUtplCaptionMuteClass))
#define GST_IS_UTPLCAPTIONMUTE_PROC(obj)    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_UTPLCAPTIONMUTE_TYPE))
#define GST_IS_UTPLCAPTIONMUTE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  GST_UTPLCAPTIONMUTE_TYPE))


typedef struct _GstUtplCaptionMute {
    GstBaseTransform parent;

    gboolean pendingMute;
    gboolean currentMute;
} GstUtplCaptionMute;

typedef struct _GstUtplCaptionMuteClass {
    GstAudioFilterClass parent;
} GstUtplCaptionMuteClass;

GST_ELEMENT_REGISTER_DECLARE(utplcaptionmute)

GType gst_utplcaptionmute_get_type(void);

G_END_DECLS

#endif /* __UTPLCAPTIONMUTE_H__ */
