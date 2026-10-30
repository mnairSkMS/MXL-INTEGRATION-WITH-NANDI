#ifndef __DLBPATLOUD_H__
#define __DLBPATLOUD_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/base/gstadapter.h>
#include <gst/audio/audio-info.h>

#include "common/dolby.h"
#include "common/timeline.h"
#include "types.h"

G_BEGIN_DECLS

#define GST_DLBPATLOUD_TYPE (gst_dlbpatloud_get_type())

#define GST_DLBPATLOUD_PROC(obj)       (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_DLBPATLOUD_TYPE, GstDlbPatLoud))
#define GST_DLBPATLOUD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),  GST_DLBPATLOUD_TYPE, GstDlbPatLoudClass))
#define GST_IS_DLBPATLOUD_PROC(obj)    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_DLBPATLOUD_TYPE))
#define GST_IS_DLBPATLOUD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  GST_DLBPATLOUD_TYPE))


typedef struct _GstDlbPatLoud {
    GstBaseTransform parent;

    gsize                 paloudInputSize;
    dlb_paloud_state     *paloudState;
    gsize                 paloudOutputSize;
    dlb_pamif_md_segment *paloudMd;
    gsize                 paloudLatencySamples;
    
    dlb_paloud_reg_type              regulationType;
    dlb_paloud_level_mode            levelMode;
    dlb_paloud_dialogue_intelligence dialInt;
    int                              dialnorm;

    // Processed buffers and the serialized events between them, in push order
    GQueue       outputQueue;
    GstAudioInfo outputInfo;

    DlbPatTimeline timeline;

    // Channel count stamped on the output: -1 = from the input (caps or block metadata), 0 = unknown
    gint  sourceChannelsProp;
    guint sourceChannels;

    dlb_paloud_channel_config       inputChannels;
    gboolean                        inputRawPcm;
    gboolean                        inputNeedReorder;
    guint8                         *inputPcmBuffer;
    GstAudioInfo                    inputInfo;

    GstAudioChannelPosition channelOrderGst[DLB_PAIO_PCM_NUM_CHANNELS];
    GstAudioChannelPosition channelOrderDlb[DLB_PAIO_PCM_NUM_CHANNELS];

    GstCaps *inputCaps;
} GstDlbPatLoud;

typedef struct _GstDlbPatLoudClass {
    GstBaseTransformClass parent;
} GstDlbPatLoudClass;

GST_ELEMENT_REGISTER_DECLARE(dlbpatloud)

GType gst_dlbpatloud_get_type(void);

G_END_DECLS

#endif /* __DLBPATLOUD_H__ */
