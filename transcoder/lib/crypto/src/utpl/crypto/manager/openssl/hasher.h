#ifndef __UTPL_CRYPTO_OPENSSL_HASHER_H__
#define __UTPL_CRYPTO_OPENSSL_HASHER_H__

#include <openssl/evp.h>

#include "utpl/crypto/hasher.h"
#include "utpl/crypto/types.h"

namespace utpl {
    namespace crypto {
        class OpensslHasher : public Hasher {
            public:
                OpensslHasher(crypto::HashAlgorithm algo);
                ~OpensslHasher();

                void reset() override;
                void update(const void *data, size_t dataSize) override;
                std::vector<uint8_t> final() override;

            private:
                const EVP_MD *md;
                EVP_MD_CTX *ctx;
        };
    }
}

#endif /* __UTPL_CRYPTO_OPENSSL_HASHER_H__ */