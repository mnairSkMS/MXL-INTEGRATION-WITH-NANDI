#ifndef __UTPL_H__
#define __UTPL_H__

#include <utpl/transcoder/types.h>

namespace utpl {
    void initialize();

    void configureGstPlugins(const std::vector<transcoder::GstPluginParameters> &pluginParameters);
}

#endif /* __UTPL_H__ */