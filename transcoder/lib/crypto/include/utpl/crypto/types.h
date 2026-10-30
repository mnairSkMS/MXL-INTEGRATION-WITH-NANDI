#ifndef __UTPL_CRYPTO_TYPES_H__
#define __UTPL_CRYPTO_TYPES_H__

#include <memory>
#include <vector>
#include <variant>

#include "utpl/common/utils/class.h"

namespace utpl {
    namespace crypto {
        enum class HashAlgorithm {
            SHA256
        };

        enum class SignatureAlgorithm {
            ED25519
        };  

        enum class CipherAlgorithm {
            AES
        };

        enum class CipherMode {
            GCM
        };

        class Key {
            UTPL_CLASS_INTERFACE(Key) {}

            public:
                virtual const std::vector<uint8_t> &raw() const = 0;

                /**
                 * Returns size in bits
                 */
                virtual size_t size() const = 0;
        };

        class PublicKey : public Key {
            UTPL_CLASS_INTERFACE(PublicKey) {}
        };

        class PrivateKey : public Key {
            UTPL_CLASS_INTERFACE(PrivateKey) {}
        };

        class KeyPair {
            public:
                KeyPair(std::unique_ptr<PublicKey> publicKey, std::unique_ptr<PrivateKey> privateKey) {
                    this->_private = std::move(privateKey);
                    this->_public  = std::move(publicKey);
                }

                const PublicKey *getPublicKey() const {
                    return this->_public.get();
                }

                const PrivateKey *getPrivateKey() const {
                    return this->_private.get();
                }

            private:
                std::unique_ptr<PublicKey>  _public;
                std::unique_ptr<PrivateKey> _private;
        };

        class CipherParameters {
            public:
                virtual ~CipherParameters() {}
        };

        class CipherIvParameters : public CipherParameters {
            public:
                CipherIvParameters() {
                }

                CipherIvParameters(const std::vector<uint8_t> iv) : iv(iv) {
                }

                CipherIvParameters(const void *data, size_t dataSize) : iv((const uint8_t *) data, (const uint8_t *) data + dataSize) {
                }

                const std::vector<uint8_t> getIv() const {
                    return this->iv;
                }

            private:
                std::vector<uint8_t> iv;
        };
    }
}

#endif /* __UTPL_CRYPTO_TYPES_H__ */