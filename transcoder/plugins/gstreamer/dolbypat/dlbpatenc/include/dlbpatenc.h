#ifndef __DLBPATENC_H__
#define __DLBPATENC_H__

#include <gst/gst.h>
#include <gst/audio/gstaudioencoder.h>

#include "common/dolby.h"
#include "common/utils.h"
#include "types.h"

#include "dlb_paec_params_runtime.h"

G_BEGIN_DECLS

#define GST_DLBPATENC_TYPE (gst_dlbpatenc_get_type())

#define GST_DLBPATENC_ENC(obj)         (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_DLBPATENC_TYPE, GstDlbPatEnc))
#define GST_DLBPATENC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),  GST_DLBPATENC_TYPE, GstDlbPatEncClass))
#define GST_IS_DLBPATENC_DEC(obj)      (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_DLBPATENC_TYPE))
#define GST_IS_VORBIS_ENC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  GST_DLBPATENC_TYPE))


typedef struct _GstDlbPatEnc {
    GstAudioEncoder parent;

    gsize           paecInputSize;
    gsize           paecOutputSize;
    dlb_paec_state *paecState;

    gboolean isDrained;
    // Holds the number of buffers processed in handle_frame callback that
    // did not provide any output (codec latency)
    gsize    processedFrames;
    gint     dialnorm;

    // Output timestamps. GstAudioEncoder stamps each output buffer with its first pending input sample, but PAEC
    // still holds `lagSamples` of earlier input whose encoding that buffer carries (its latency, taken in once at
    // the first frame). pre_push moves the timestamp back by that amount. Sample counts since the last finish:
    guint64  pendingInSamples;
    guint64  pendingOutSamples;
    gboolean outSamplesKnown;
    gint64   lagSamples;
    // Values for the buffer being pushed by finish_frame
    gint64   pushLagSamples;
    guint64  pushOutSamples;

    gint                 outputChannels;
    DlbPatCodec          outputCodec;
    GstDlbPatEncDataRate outputDataRate;
    GstAdapter          *outputAdapter;

    int drcline;      // dlb_paec_drc_profile
    int drcrf;        // dlb_paec_drc_profile
    int surroundTrim; // dlb_paec_surround_trim
    int heightTrim;   // dlb_paec_height_trim
    int dmixmod;      // dlb_paec_ddp_dmixmod
    int ltrtcmixlev;
    int ltrtsurmixlev;
    int lorocmixlev;
    int lorosurmixlev;
} GstDlbPatEnc;

typedef struct _GstDlbPatEncClass {
    GstAudioEncoderClass parent;
} GstDlbPatEncClass;

GST_ELEMENT_REGISTER_DECLARE(dlbpatenc)

GType gst_dlbpatenc_get_type(void);

G_END_DECLS

#endif /* __DLBPATENC_H__ */
