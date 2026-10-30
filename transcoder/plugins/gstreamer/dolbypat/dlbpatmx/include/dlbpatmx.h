#ifndef __DLBPATMX_H__
#define __DLBPATMX_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/base/gstadapter.h>
#include <gst/audio/audio-info.h>

#include "common/dolby.h"
#include "common/timeline.h"
#include "types.h"

G_BEGIN_DECLS

#define GST_DLBPATMX_TYPE (gst_dlbpatmx_get_type())

#define GST_DLBPATMX_PROC(obj)       (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_DLBPATMX_TYPE, GstDlbPatMx))
#define GST_DLBPATMX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),  GST_DLBPATMX_TYPE, GstDlbPatMxClass))
#define GST_IS_DLBPATMX_PROC(obj)    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_DLBPATMX_TYPE))
#define GST_IS_DLBPATMX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  GST_DLBPATMX_TYPE))


typedef struct _GstDlbPatMx {
    GstBaseTransform parent;

    gsize                 padmxInputSize;
    dlb_padmx_state      *padmxState;
    gsize                 padmxOutputSize;
    dlb_pamif_md_segment *padmxMd;
    gsize                 padmxLatencySamples;

    GstDlbPadmxMode downmixMode;

    // Processed buffers and the serialized events between them, in push order
    GQueue                          outputQueue;
    DlbPatTimeline                  timeline;
    dlb_padmx_target_speaker_config outputChannels;
    GstAudioInfo                    outputInfo;

    dlb_padmx_input_speaker_config  inputChannels;
    gboolean                        inputRawPcm;
    gboolean                        inputNeedReorder;
    guint8                         *inputPcmBuffer;
    GstAudioInfo                    inputInfo;
    GstCaps                        *inputCaps;

    // in-channels=source: configuration follows the source channel count of the blocks
    gboolean                        sourceMode;
    // in-channels=source-auto: as source, with PADMX auto detection for 2.0/5.1/5.1.4 sources
    gboolean                        sourceAuto;
    guint                           sourceChannels;
    dlb_padmx_renderer_mode         sourceRenderer;
    guint64                         instanceSamples;

    GstAudioChannelPosition channelOrderGst[DLB_PAIO_PCM_NUM_CHANNELS];
    GstAudioChannelPosition channelOrderDlb[DLB_PAIO_PCM_NUM_CHANNELS];
} GstDlbPatMx;

typedef struct _GstDlbPatMxClass {
    GstBaseTransformClass parent;
} GstDlbPatMxClass;

GST_ELEMENT_REGISTER_DECLARE(dlbpatmx)

GType gst_dlbpatmx_get_type(void);

G_END_DECLS

#endif /* __DLBPATMX_H__ */
