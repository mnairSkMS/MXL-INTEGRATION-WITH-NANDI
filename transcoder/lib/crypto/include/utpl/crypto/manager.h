#ifndef __UTPL_CRYPTO_MANAGER_H__
#define __UTPL_CRYPTO_MANAGER_H__

#include "utpl/common/utils/class.h"

#include "utpl/crypto/hasher.h"
#include "utpl/crypto/cipher.h"
#include "utpl/crypto/signature.h"

namespace utpl {
    namespace crypto {
        class CryptoManager {
            UTPL_CLASS_INTERFACE(CryptoManager) {}

            public:
                virtual std::unique_ptr<Hasher>    newHasher(HashAlgorithm algo) = 0;
                virtual std::unique_ptr<Cipher>    newCipher(CipherAlgorithm algo, CipherMode mode) = 0;
                virtual std::unique_ptr<Signature> newSignature(SignatureAlgorithm algo) = 0;

                virtual std::unique_ptr<Key> generateKey(size_t bitsSize) = 0;
                virtual std::vector<uint8_t> genRandom(size_t bytes) = 0;

                virtual std::unique_ptr<Key> loadKey(const std::string &keyPath) = 0;
                virtual std::unique_ptr<Key> loadKey(const void *keyData, size_t keyDataSize) = 0;
                virtual std::unique_ptr<Key> loadKey(const std::vector<uint8_t> keyData) {
                    return this->loadKey(keyData.data(), keyData.size());
                }

                virtual std::unique_ptr<PublicKey>  loadPublicKey(const std::string &keyPath) = 0;
                virtual std::unique_ptr<PublicKey>  loadPublicKey(const void *keyData, size_t keyDataSize) = 0;
                virtual std::unique_ptr<PublicKey>  loadPublicKey(const std::vector<uint8_t> keyData) {
                    return this->loadPublicKey(keyData.data(), keyData.size());
                }

                virtual std::unique_ptr<PrivateKey> loadPrivateKey(const std::string &keyPath) = 0;
                virtual std::unique_ptr<PrivateKey> loadPrivateKey(const void *keyData, size_t keyDataSize) = 0;
                virtual std::unique_ptr<PrivateKey> loadPrivateKey(const std::vector<uint8_t> keyData) {
                    return this->loadPrivateKey(keyData.data(), keyData.size());
                }
        };

        CryptoManager &getCryptoManager();
    }
}

#endif /* __UTPL_CRYPTO_MANAGER_H__ */