#ifndef __UTPL_LICENSE_FILE_SERIALIZER_H__
#define __UTPL_LICENSE_FILE_SERIALIZER_H__

#include <vector>
#include <istream>

#include "utpl/common/utils/class.h"
#include "utpl/crypto/types.h"

#include "utpl/license.h"
#include "utpl/license/file.h"

namespace utpl {
    namespace license {
        class LicenseFileSerializer {
            UTPL_CLASS_INTERFACE(LicenseFileSerializer) {}

            public:
                virtual std::vector<uint8_t> serialize(const LicenseFile &licenseFile) = 0;
                virtual LicenseFile deserialize(const std::vector<uint8_t> &data) = 0;

                virtual LicenseFile deserialize(std::istream &stream);
        };
    }
}

#endif /* __UTPL_LICENSE_FILE_SERIALIZER_H__ */