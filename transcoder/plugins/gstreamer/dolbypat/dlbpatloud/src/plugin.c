#include "common/plugin.h"
#include "dlbpatloud.h"


static gboolean plugin_init (GstPlugin * plugin) {
    return GST_ELEMENT_REGISTER (dlbpatloud, plugin);
}


GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dlbpatloud,
    "Dolby Pro Loudness leveler based on Dolby Pro Transcoder kit.",
    plugin_init, 
    VERSION, 
    GST_LICENSE, 
    GST_PACKAGE_NAME, 
    GST_PACKAGE_ORIGIN
)