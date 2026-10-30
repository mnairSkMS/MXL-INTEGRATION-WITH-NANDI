#ifndef __UTPL_CRYPTO_CRYPTER_H__
#define __UTPL_CRYPTO_CRYPTER_H__

#include "utpl/common/utils/class.h"
#include "utpl/crypto/types.h"

#include <vector>
#include <string>

namespace utpl {
    namespace crypto {
        class Cipher {
            UTPL_CLASS_INTERFACE(Cipher) {}

            public:
                virtual void init(bool encrypt, const Key &key, const CipherParameters *parameters = nullptr) = 0;
                
                virtual std::vector<uint8_t> update(const void *data, size_t dataSize) = 0;
                virtual std::vector<uint8_t> update(const std::vector<uint8_t> &data) {
                    return this->update(data.data(), data.size());
                }

                virtual std::vector<uint8_t> final(const void *data, size_t dataSize) = 0;
                virtual std::vector<uint8_t> final(const std::vector<uint8_t> &data = {}) {
                    return this->final(data.data(), data.size());
                }

                virtual void updateAAD(const void *data, size_t dataSize) = 0;
                virtual void updateAAD(const std::vector<uint8_t> &aad) { 
                    this->updateAAD(aad.data(), aad.size());
                }

                virtual const std::vector<uint8_t> &getIv() const = 0;
        };
    }
}

#endif /* __UTPL_CRYPTO_CRYPTER_H__ */