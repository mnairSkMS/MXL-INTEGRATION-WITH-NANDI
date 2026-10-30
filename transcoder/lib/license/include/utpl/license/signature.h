#ifndef __UTPL_LICENSE_SIGNATURE_H__
#define __UTPL_LICENSE_SIGNATURE_H__

#include <vector>

#include "utpl/common/utils/class.h"

#include "utpl/crypto/types.h"

#include "utpl/license.h"


namespace utpl {
    namespace license {
        class LicenseSignature {
            public:
                bool verify(const std::vector<uint8_t> &licenseData, const std::vector<uint8_t> &licenseSignature, const crypto::PublicKey &publicKey);
                std::vector<uint8_t> sign(const std::vector<uint8_t> &licenseData, const crypto::PrivateKey &privateKey);

        };
    }
}

#endif /* __UTPL_LICENSE_SIGNATURE_H__ */