#include "utpl/common/utils.h"

#include "utpl/crypto/manager.h"

#include "utpl/license/config.h"
#include "utpl/license/debug.h"
#include "utpl/license/exception.h"
#include "utpl/license/elf/keyLoader.h"
#include "utpl/license/utils/elf/hasher.h"
#include "utpl/license/utils/elf/reader.h"

using namespace utpl;


license::ElfKeyLoader::ElfKeyLoader() {

}

void license::ElfKeyLoader::load(const std::string &path) {
    std::vector<uint8_t> elfHash;

    auto &crypto = crypto::getCryptoManager();

    // Calculate hash of elf sections
    {
        auto hasher = crypto.newHasher(UTPL_LICENSE_KEY_SECTION_HASH_ALGORITHM);

        license::ElfHasher elfHasher(path);

        elfHash = elfHasher.hash(*hasher, { UTPL_LICENSE_KEY_SECTION_NAME });
    }

    {
        license::ElfReader reader;

        if (! reader.load(path)) {
            throw license::GenericLicenseException("Unable to load ELF file sections!");
        }

        const auto *keySection = reader.getSection(UTPL_LICENSE_KEY_SECTION_NAME);
        if (keySection == nullptr) {
            throw license::GenericLicenseException("Unable to load ELF key section!");
        }

        {
            auto keySectionString = std::string((char *) keySection->data(), keySection->length());

            auto encodedKeyElements = utils::split(keySectionString, UTPL_LICENSE_KEY_SECTION_DELIMITER);
            if (encodedKeyElements.size() != 2) {
                throw license::GenericLicenseException("Number of section data elements is wrong");
            }

            auto cipher = crypto.newCipher(UTPL_LICENSE_KEY_SECTION_CIPHER_ALGORITHM, UTPL_LICENSE_KEY_SECTION_CIPHER_MODE);

            {
                auto key = crypto.loadKey(elfHash);

                crypto::CipherIvParameters params(
                    utils::fromBase64(encodedKeyElements[0])
                );

                cipher->init(false, *key, &params);
            }

            auto decrypted = cipher->final(utils::fromBase64(encodedKeyElements[1]));

            {
                std::string decoderString((char *) decrypted.data(), decrypted.size());

                auto decodedKeyElements = utils::split(decoderString, UTPL_LICENSE_KEY_SECTION_DELIMITER);
                if (decodedKeyElements.size() != 2) {
                    throw license::GenericLicenseException("Number of key data elements is wrong");
                }

                this->encKey = crypto.loadKey(
                    utils::fromBase64(decodedKeyElements[0])
                );

                this->publicKey = crypto.loadPublicKey(
                    utils::fromBase64(decodedKeyElements[1])
                );
            }
        }
    }
}

const crypto::PublicKey &license::ElfKeyLoader::getPublicKey() {
    return *this->publicKey;
}

const crypto::Key &license::ElfKeyLoader::getEncryptionKey() {
    return *this->encKey;
}