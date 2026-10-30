#include <openssl/evp.h>

#include "utpl/crypto/types.h"

namespace utpl {
    namespace crypto {
        class OpensslKey : public Key {
            public:
                OpensslKey(const std::vector<uint8_t> &data);
                OpensslKey(const void *data, size_t dataSize);

                const std::vector<uint8_t> &raw() const override;

                /**
                 * Returns size in bits
                 */
                size_t size() const override;

            private:
                std::vector<uint8_t> _raw;
        };

        class OpensslPrivateKey : public PrivateKey {
            public:
                OpensslPrivateKey(const void *data, size_t dataSize);
                ~OpensslPrivateKey();

                const std::vector<uint8_t> &raw() const override {
                    return this->_key.raw();
                }

                size_t size() const override {
                    return this->_key.size();
                }

                const EVP_PKEY *getHandle() const {
                    return this->_evpKey;
                }

            private:
                OpensslKey _key;
                EVP_PKEY  *_evpKey;
        };

        class OpensslPublicKey : public PublicKey {
            public:
                OpensslPublicKey(const void *data, size_t dataSize);
                ~OpensslPublicKey();

                const std::vector<uint8_t> &raw() const override {
                    return this->_key.raw();
                }

                size_t size() const override {
                    return this->_key.size();
                }

                const EVP_PKEY *getHandle() const {
                    return this->_evpKey;
                }

            private:
                OpensslKey _key;
                EVP_PKEY  *_evpKey;
        };
    }
}