#ifndef __DLBPATDEC_H__
#define __DLBPATDEC_H__

#include <gst/gst.h>
#include <gst/audio/gstaudiodecoder.h>

#include "common/dolby.h"

G_BEGIN_DECLS

#define GST_DLBPATDEC_TYPE (gst_dlbpatdec_get_type())

#define GST_DLBPATDEC_DEC(obj)         (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_DLBPATDEC_TYPE, GstDlbPatDec))
#define GST_DLBPATDEC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),  GST_DLBPATDEC_TYPE, GstDlbPatDecClass))
#define GST_IS_DLBPATDEC_DEC(obj)      (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_DLBPATDEC_TYPE))
#define GST_IS_VORBIS_DEC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  GST_DLBPATDEC_TYPE))


typedef struct _GstDlbPatDec {
    GstAudioDecoder parent;

    gsize                 padcInputSize;
    dlb_padc_state       *padcState;
    dlb_pamif_md_segment *padcMd;

    gboolean isDrained;
    gint     outputChannels;

    // Channel count announced by the parser (acmod + LFE), stamped on the output blocks for dlbpatmx
    guint    sourceChannels;

    // Leading PADC output that is decoder delay, not audio: dropped so output sample N is input sample N
    guint    primingSamples;

    // Sample counts of the frames given to PADC whose audio has not been output yet. PADC returns a frame's
    // audio while decoding later frames, so each frame's audio is collected into one buffer and finished
    // with that frame - the base class then stamps it with the frame's own timestamp.
    GQueue     pendingFrames;
    GstBuffer *frameBuffer;
    guint      frameFilled;
} GstDlbPatDec;

typedef struct _GstDlbPatDecClass {
    GstAudioDecoderClass parent;
} GstDlbPatDecClass;

GST_ELEMENT_REGISTER_DECLARE(dlbpatdec)

GType gst_dlbpatdec_get_type(void);

G_END_DECLS

#endif /* __DLBPATDEC_H__ */
