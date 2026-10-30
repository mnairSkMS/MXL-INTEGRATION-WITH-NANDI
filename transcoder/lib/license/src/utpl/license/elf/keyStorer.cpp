#include "utpl/common/utils.h"

#include "utpl/crypto/manager.h"

#include "utpl/license/config.h"
#include "utpl/license/elf/keyStorer.h"
#include "utpl/license/utils/elf/reader.h"
#include "utpl/license/utils/elf/hasher.h"
#include "utpl/license/utils/elf/modifier.h"

#include "utpl/license/debug.h"

using namespace utpl;

// Based on https://github.com/dynup/kpatch/blob/master/kpatch-build/kpatch-elf.c

license::ElfKeyStorer::ElfKeyStorer() {
}

void license::ElfKeyStorer::setPublicKey(const std::vector<uint8_t> &key) {
    this->publicKey = key;
}

void license::ElfKeyStorer::setEncryptionKey(const std::vector<uint8_t> &key) {
    this->encKey = key;
}

void license::ElfKeyStorer::store(const std::string &filePath) {
    std::string keySectionData;

    // Generate keydata section
    {
        auto &crypto = utpl::crypto::getCryptoManager();

        std::vector<uint8_t> elfHash;

        // Calculate hash of elf sections
        {
            auto hasher = crypto.newHasher(UTPL_LICENSE_KEY_SECTION_HASH_ALGORITHM);

            license::ElfHasher elfHasher(filePath);

            elfHash = elfHasher.hash(*hasher, { UTPL_LICENSE_KEY_SECTION_NAME });
        }

        // Create section data
        {
            auto encKey = crypto.loadKey(elfHash);

            auto cipher = crypto.newCipher(
                UTPL_LICENSE_KEY_SECTION_CIPHER_ALGORITHM, 
                UTPL_LICENSE_KEY_SECTION_CIPHER_MODE
            );

            const std::string toEncrypt = 
                utpl::utils::toBase64(this->encKey) + 
                UTPL_LICENSE_KEY_SECTION_DELIMITER +
                utpl::utils::toBase64(this->publicKey)
            ;

            cipher->init(true, *encKey);

            keySectionData = 
                utpl::utils::toBase64(cipher->getIv()) +
                UTPL_LICENSE_KEY_SECTION_DELIMITER +
                utpl::utils::toBase64(
                    cipher->final(toEncrypt.data(), toEncrypt.length())
                )
            ;
        }
    }

    license::ElfModifier::removeSection(filePath, UTPL_LICENSE_KEY_SECTION_NAME);

    license::ElfModifier::addSection(filePath, UTPL_LICENSE_KEY_SECTION_NAME, keySectionData);
}