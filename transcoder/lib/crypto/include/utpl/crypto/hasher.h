#ifndef __UTPL_CRYPTO_HASHER_H__
#define __UTPL_CRYPTO_HASHER_H__

#include "utpl/common/utils/class.h"

#include <vector>

namespace utpl {
    namespace crypto {
        class Hasher {
            UTPL_CLASS_INTERFACE(Hasher) {}

            public:
                virtual void reset() = 0;
                virtual void update(const void *data, size_t dataSize) = 0;
                virtual std::vector<uint8_t> final() = 0;

                void update(const std::vector<uint8_t> &data) {
                    this->update(data.data(), data.size());
                }
        };
    }
}

#endif /* __UTPL_CRYPTO_HASHER_H__ */