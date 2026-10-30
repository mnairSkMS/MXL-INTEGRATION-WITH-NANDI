#ifndef UTPL_LICENSE_FILE_SERIALIZER_JSON_H
#define UTPL_LICENSE_FILE_SERIALIZER_JSON_H

#include "utpl/license.h"
#include "utpl/license/file/serializer.h"

namespace utpl {
    namespace license {
        class JsonLicenseFileSerializer : public LicenseFileSerializer {
            public:
                std::vector<uint8_t> serialize(const LicenseFile &file) override;

                LicenseFile deserialize(const std::vector<uint8_t> &data) override;
        };
    }
}

#endif /* UTPL_LICENSE_FILE_SERIALIZER_JSON_H__ */