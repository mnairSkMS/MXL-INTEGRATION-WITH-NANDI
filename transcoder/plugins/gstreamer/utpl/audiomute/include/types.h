#ifndef __UTPLMUTE_TYPES_H__
#define __UTPLMUTE_TYPES_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef enum _GstUtplAudioMuteProperties {
    PROPERTY_0,

    PROPERTY_MUTE,

    PROPERTY_LAST
} GstUtplAudioMuteProperties;

G_END_DECLS

#endif /* __UTPLMUTE_TYPES_H__ */