#ifndef __UTPL_CRYPTO_SIGNATURE_H__
#define __UTPL_CRYPTO_SIGNATURE_H__

#include "utpl/common/utils/class.h"

#include "utpl/crypto/types.h"

namespace utpl {
    namespace crypto {
        class Signature {
            UTPL_CLASS_INTERFACE(Signature) {}

            public:
                virtual void initSign(const crypto::PrivateKey &key) = 0;
                virtual std::vector<uint8_t> sign() = 0;

                virtual void update(const void *data, size_t dataSize) = 0;
                virtual void update(const std::string &data) {
                    this->update(data.data(), data.length());
                }
                
                virtual void update(const std::vector<uint8_t> &data) {
                    this->update(data.data(), data.size());
                }

                virtual void initVerify(const crypto::PublicKey &key) = 0;
                
                virtual bool verify(const void *data, size_t dataSize) = 0;
                virtual bool verify(const std::vector<uint8_t> &signature) {
                    return this->verify(signature.data(), signature.size());
                };
        };
    }
}

#endif /* __UTPL_CRYPTO_SIGNATURE_H__ */