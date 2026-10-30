#include "utpl/common/utils/class.h"
#include "utpl/common/time.h"

#include "utpl/license.h"

namespace utpl {
    namespace license {
        class LicenseUtils {
            UTPL_CLASS_NOBUILD_NOCOPY(LicenseUtils);

            public:
                static bool isValid(const License &license, utpl::Timestamp timestamp) {
                    return ! isExpired(license, timestamp) && ! isNotYetValid(license, timestamp);
                }

                static bool isExpired(const License &license, utpl::Timestamp timestamp) {
                    auto startTs  = license.getModel().getValidSince();
                    auto duration = license.getModel().getTtl();

                    if (startTs + duration < timestamp) {
                        return true;
                    }

                    return false;
                }

                static bool isNotYetValid(const License &license, utpl::Timestamp timestamp) {
                    auto startTs  = license.getModel().getValidSince();

                    if (timestamp < startTs) {
                        return true;
                    }

                    return false;
                }

                static bool isValid(const License &license) {
                    return isValid(license, utpl::Time::getUTCTimestamp());
                }

                static bool isExpired(const License &license) {
                    return isExpired(license, utpl::Time::getUTCTimestamp());
                }
                static bool isNotYetValid(const License &license) {
                    return isNotYetValid(license, utpl::Time::getUTCTimestamp());
                }
        };
    }
}