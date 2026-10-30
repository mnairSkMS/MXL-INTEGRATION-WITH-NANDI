#ifndef __DLBPATENC_TYPES_H__
#define __DLBPATENC_TYPES_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef enum _GstDlbPatLoudProperties {
    PROPERTY_0,

    PROPERTY_REG_TYPE,
    PROPERTY_CHANNELS,
    PROPERTY_MODE,
    PROPERTY_DIALNORM,
    PROPERTY_DIALINT,
    PROPERTY_SOURCE_CHANNELS,

    PROPERTY_LAST
} GstDlbPatLoudProperties;

GType dlbpatloud_reg_type_getType();
GType dlbpatloud_channels_getType();
GType dlbpatloud_mode_getType();
GType dlbpatloud_dialint_getType();

G_END_DECLS

#endif /* __DLBPATENC_TYPES_H__ */