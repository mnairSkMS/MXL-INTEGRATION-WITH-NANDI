#ifndef __UTPLMUTE_H__
#define __UTPLMUTE_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/base/gstadapter.h>
#include <gst/audio/audio-info.h>

G_BEGIN_DECLS

#define GST_UTPLAUDIOMUTE_TYPE (gst_utplaudiomute_get_type())

#define GST_UTPLAUDIOMUTE_PROC(obj)       (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_UTPLAUDIOMUTE_TYPE, GstUtplAudioMute))
#define GST_UTPLAUDIOMUTE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),  GST_UTPLAUDIOMUTE_TYPE, GstUtplAudioMuteClass))
#define GST_IS_UTPLAUDIOMUTE_PROC(obj)    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_UTPLAUDIOMUTE_TYPE))
#define GST_IS_UTPLAUDIOMUTE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  GST_UTPLAUDIOMUTE_TYPE))


typedef struct _GstUtplAudioMute {
    GstAudioFilter parent;

    gboolean pendingMute;
    gboolean currentMute;
} GstUtplAudioMute;

typedef struct _GstUtplAudioMuteClass {
    GstAudioFilterClass parent;
} GstUtplAudioMuteClass;

GST_ELEMENT_REGISTER_DECLARE(utplaudiomute)

GType gst_utplaudiomute_get_type(void);

G_END_DECLS

#endif /* __UTPLMUTE_H__ */
