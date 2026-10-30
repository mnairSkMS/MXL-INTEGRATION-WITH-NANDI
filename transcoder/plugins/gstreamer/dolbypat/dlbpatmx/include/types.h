#ifndef __DLBPADMX_TYPES_H__
#define __DLBPADMX_TYPES_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef enum _GstDlbPadmxProperties {
    PROPERTY_0,

    PROPERTY_OUT_CHANNELS,
    PROPERTY_IN_CHANNELS,
    PROPERTY_MODE,

    PROPERTY_LAST
} GstDlbPadmxProperties;

typedef enum _GstDlbPadmxMode {
    DLB_PADMX_MODE_AUTO,
    DLB_PADMX_MODE_DOWNMIX,
    DLB_PADMX_MODE_UPMIX
} GstDlbPadmxMode;

#define DLBPADMX_INPUT_CHANNELS_UNKNOWN -1

// Follow the source channel count carried by each input block (transcoder value, never passed to PADMX)
#define DLBPADMX_INPUT_CHANNELS_SOURCE  -2

// As DLBPADMX_INPUT_CHANNELS_SOURCE, but a 2.0, 5.1 or 5.1.4 source rendered to 5.1 or 5.1.4 is analysed by PADMX
// automatic detection, so stereo carried in a wider declared layout (silent C/LFE/surrounds) is upmixed
// (transcoder value, never passed to PADMX)
#define DLBPADMX_INPUT_CHANNELS_SOURCE_AUTO -3

GType dlbpadmx_outputChannels_getType();
GType dlbpadmx_inputChannels_getType();
GType dlbpadmx_mode_getType();

G_END_DECLS

#endif /* __DLBPADMX_TYPES_H__ */