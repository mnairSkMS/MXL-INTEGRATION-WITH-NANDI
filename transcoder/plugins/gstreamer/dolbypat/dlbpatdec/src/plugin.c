#include "common/plugin.h"
#include "dlbpatdec.h"


static gboolean plugin_init (GstPlugin * plugin) {
    return GST_ELEMENT_REGISTER (dlbpatdec, plugin);
}


GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dlbpatdec,
    "Dolby Pro Decoder based on Dolby Pro Transcoder kit.",
    plugin_init, 
    VERSION, 
    GST_LICENSE, 
    GST_PACKAGE_NAME, 
    GST_PACKAGE_ORIGIN
)