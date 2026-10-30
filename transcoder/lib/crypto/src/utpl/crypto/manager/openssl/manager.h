#include "utpl/crypto/manager.h"

namespace utpl {
    namespace crypto {
        class OpensslCryptoManager : public CryptoManager {
            public:
                virtual ~OpensslCryptoManager();
                
                std::unique_ptr<Hasher>    newHasher(HashAlgorithm algo) override;
                std::unique_ptr<Cipher>    newCipher(CipherAlgorithm algo, CipherMode mode) override;
                std::unique_ptr<Signature> newSignature(SignatureAlgorithm algo) override;

                std::unique_ptr<Key> generateKey(size_t bitsSize) override;
                std::vector<uint8_t> genRandom(size_t bytes) override;

                std::unique_ptr<Key> loadKey(const std::string &keyPath) override;
                std::unique_ptr<Key> loadKey(const void *keyData, size_t keyDataSize) override;

                std::unique_ptr<PublicKey>  loadPublicKey(const std::string &keyPath) override;
                std::unique_ptr<PublicKey>  loadPublicKey(const void *keyData, size_t keyDataSize) override;

                std::unique_ptr<PrivateKey> loadPrivateKey(const std::string &keyPath) override;
                std::unique_ptr<PrivateKey> loadPrivateKey(const void *keyData, size_t keyDataSize) override;
        };
    }
}