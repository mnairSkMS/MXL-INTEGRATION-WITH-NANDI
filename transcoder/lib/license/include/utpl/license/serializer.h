#ifndef __UTPL_LICENSE_SERIALIZER_H__
#define __UTPL_LICENSE_SERIALIZER_H__

#include <vector>

#include "utpl/common/utils/class.h"

#include "utpl/license.h"
#include "utpl/crypto/types.h"

namespace utpl {
    namespace license {
        class LicenseSerializer {
            UTPL_CLASS_INTERFACE(LicenseSerializer) {}

            public:
                virtual std::vector<uint8_t> serialize(const License &license, const crypto::Key &cipherKey) = 0;
                virtual License deserialize(const std::vector<uint8_t> &data, const crypto::Key &cipherKey) = 0;
        };
    }
}

#endif /* __UTPL_LICENSE_SERIALIZER_H__ */