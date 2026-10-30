#include <gst/gstplugin.h>

#include "common/plugin.h"
#include "audiomute.h"

static gboolean plugin_init (GstPlugin * plugin) {
    return GST_ELEMENT_REGISTER (utplaudiomute, plugin);
}


GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    utplaudiomute,
    "UTPL audio mute element.",
    plugin_init, 
    VERSION, 
    GST_LICENSE, 
    GST_PACKAGE_NAME, 
    GST_PACKAGE_ORIGIN
)