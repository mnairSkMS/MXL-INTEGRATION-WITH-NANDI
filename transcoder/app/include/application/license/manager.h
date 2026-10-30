#ifndef __APPLICATION_LICENSE_MANAGER_H__
#define __APPLICATION_LICENSE_MANAGER_H__

#include <functional>
#include <memory>

#include <glib.h>
#include <cpr/cpr.h>

#include "utpl/license.h"
#include "utpl/common/logger.h"

namespace app {
    class LicenseManager {
        public:
            enum class LicenseStatus {
                UNKNOWN,
                INVALID,
                VALID
            };

            using LicenseExpiredCallback = std::function<void(LicenseStatus status)>;

        public:
            LicenseManager();

            void configure(const utpl::license::License &license, const std::string &licensePath, const std::string &licenseServerUri, const utpl::crypto::PublicKey &licensePublicKey);
            void start(LicenseExpiredCallback callback);
            void stop();

        private:
            static bool _onCheckLicense(LicenseManager *self);
            
        private:
            LicenseStatus _processTokenResponse(cpr::Response &response);

        private:
            utpl::license::License license;
            std::string            licenseServer;
            std::string            licensePath;
            std::string            licenseData;
            std::vector<uint8_t>   licensePublicKey;
            LicenseStatus          licenseStatus;
            guint                  timerId;
            LicenseExpiredCallback callback;

            std::unique_ptr<cpr::Session> httpSession;
            size_t                        httpAttempts;

            std::string     sessionToken;
            utpl::Timestamp sessionTokenCheckTimestamp;
    };
}

#endif /* __APPLICATION_LICENSE_MANAGER_H__ */