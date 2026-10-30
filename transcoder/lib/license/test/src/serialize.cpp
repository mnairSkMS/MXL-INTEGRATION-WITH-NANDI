#include <gtest/gtest.h>

#include "utpl/crypto/cipher.h"
#include "utpl/crypto/manager.h"

#include "utpl/license.h"
#include "utpl/license/model.h"
#include "utpl/license/file/serializer/json.h"
#include "utpl/license/serializer/json.h"
#include "utpl/license/signature.h"

using namespace utpl;


TEST(lib_license_serialize, json_serialize) {
    static const std::vector<uint8_t> key = {
        0x87, 0x1e, 0xfd, 0x07, 0x8c, 0x37, 0x66, 0xdb, 0xf7, 0x93, 0x65, 0x6a,
        0x33, 0x26, 0x1c, 0x6a, 0xa9, 0x46, 0xb5, 0x3c, 0xf2, 0x8d, 0x94, 0x78,
        0x96, 0x0a, 0xd6, 0x6e, 0xd8, 0x80, 0xbf, 0xff
    };

    {
        license::License srcLicense;
        license::License outLicense;
        
        {
            srcLicense.getMetadata().setCreationTime(Time::getUTCTimestamp());
            srcLicense.getModel().setId("abc-xyz-123");
            srcLicense.getModel().setName("Test license");
            srcLicense.getModel().setValidSince(Time::fromIso8601("2025-05-01"));
            srcLicense.getModel().setTtl(std::chrono::duration_cast<std::chrono::milliseconds>(Time::days(30)).count());
        }

        {
            license::LicenseFile plainLicenseFile;

            auto &crypto = utpl::crypto::getCryptoManager();
            auto cipherKey = crypto.loadKey(key);

            auto serializer = std::make_unique<license::JsonLicenseSerializer>();
            auto fileSerializer = std::make_unique<license::JsonLicenseFileSerializer>();

            plainLicenseFile.setData(
                serializer->serialize(srcLicense, *cipherKey)
            );

            auto out = fileSerializer->serialize(plainLicenseFile);
            auto licenseFile = fileSerializer->deserialize(out);
        
            outLicense = serializer->deserialize(licenseFile.getData(), *cipherKey);
        }

        ASSERT_EQ(srcLicense.getModel().getId(), outLicense.getModel().getId());
        ASSERT_EQ(srcLicense.getModel().getName(), outLicense.getModel().getName());
        ASSERT_EQ(srcLicense.getModel().getTtl(), outLicense.getModel().getTtl());
        ASSERT_EQ(srcLicense.getModel().getValidSince(), outLicense.getModel().getValidSince());

        ASSERT_EQ(srcLicense.getMetadata().getCreationTime(), outLicense.getMetadata().getCreationTime());
    }
}