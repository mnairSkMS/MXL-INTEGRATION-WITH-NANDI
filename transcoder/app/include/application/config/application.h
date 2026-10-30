#ifndef __APPLICATION_CONFIG_APPLICATION_H__
#define __APPLICATION_CONFIG_APPLICATION_H__

#include <string>

#include "utpl/transcoder/types.h"

namespace app {
    class ApplicationParameters {
        public:
            ApplicationParameters() {
            }

            const std::string &getDbPath() const {
                return this->_dbPath;
            }

            void setDbPath(const std::string &dbPath) {
                this->_dbPath = dbPath;
            }

            const std::string &getLicenseServerUri() const {
                return this->_licenseServerUri;
            }

            void setLicenseServerUri(const std::string &uri) {
                this->_licenseServerUri = uri;
            }

            const std::vector<utpl::transcoder::GstPluginParameters> &getGstPluginsParameters() const {
                return this->_pluginParameters;
            }

            void addGstPluginParameters(const utpl::transcoder::GstPluginParameters &params) {
                this->_pluginParameters.push_back(params);
            }

        private:
            std::string _dbPath;
            std::string _licenseServerUri;

            std::vector<utpl::transcoder::GstPluginParameters> _pluginParameters;
    };
}

#endif /* __APPLICATION_CONFIG_APPLICATION_H__ */