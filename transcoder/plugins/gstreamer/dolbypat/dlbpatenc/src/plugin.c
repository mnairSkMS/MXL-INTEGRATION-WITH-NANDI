#include "common/plugin.h"
#include "dlbpatenc.h"


static gboolean plugin_init (GstPlugin * plugin) {
    return GST_ELEMENT_REGISTER (dlbpatenc, plugin);
}


GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dlbpatenc,
    "Dolby Pro Encoder based on Dolby Pro Transcoder kit.",
    plugin_init, 
    VERSION, 
    GST_LICENSE, 
    GST_PACKAGE_NAME, 
    GST_PACKAGE_ORIGIN
)