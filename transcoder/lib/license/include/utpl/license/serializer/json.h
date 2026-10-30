#ifndef __UTPL_LICENSE_JSON_SERIALIZER_H__
#define __UTPL_LICENSE_JSON_SERIALIZER_H__


#include "utpl/license/serializer.h"

namespace utpl {
    namespace license {
        class JsonLicenseSerializer : public LicenseSerializer {
            public:
                std::vector<uint8_t> serialize(const License &license, const crypto::Key &cipherKey) override;

                License deserialize(const std::vector<uint8_t> &data, const crypto::Key &cipherKey) override;
        };
    }
}

#endif /* __UTPL_LICENSE_JSON_SERIALIZER_H__ */