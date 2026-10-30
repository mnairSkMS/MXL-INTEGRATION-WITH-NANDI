#include <gst/gst.h>

#include "utpl.h"

#include "utpl/common/debug.h"


void utpl::initialize() {
    gst_init(nullptr, nullptr);
    
    utpl::Debug::initialize();

    LOGGER_ALWAYS("Initialized UTPL library, version {}", UTPL_VERSION);
}

void utpl::configureGstPlugins(const std::vector<transcoder::GstPluginParameters> &pluginParameters) {
    GstRegistry *registry = gst_registry_get();

    if (registry) {
        for (const auto &params : pluginParameters) {
            GstPluginFeature *pluginFeature = gst_registry_lookup_feature(registry, params.getName().c_str());
            if (pluginFeature) {
                gst_plugin_feature_set_rank(pluginFeature, params.getRank());

                gst_object_unref(pluginFeature);
            }
        }
    }
}
