#ifndef __UTPL_CRYPTO_MANAGER_OPENSSL_SIGNATURE_H__
#define __UTPL_CRYPTO_MANAGER_OPENSSL_SIGNATURE_H__

#include <memory>
#include <openssl/evp.h>

#include "utpl/crypto/signature.h"

namespace utpl {
    namespace crypto {
        class OpensslSignature : public Signature {
            public:
                OpensslSignature(crypto::SignatureAlgorithm algo);
                ~OpensslSignature();

                void initSign(const crypto::PrivateKey &key) override;
                std::vector<uint8_t> sign() override;

                void update(const void *data, size_t dataSize) override;

                void initVerify(const crypto::PublicKey &key) override;
                bool verify(const void *data, size_t dataSize) override;

            private:
                void init(const PublicKey *publicKey, const PrivateKey *privateKey);
                void term();

            private:
                EVP_MD_CTX *mdCtx;
                bool signMode;
                std::unique_ptr<std::vector<uint8_t>> buffer;

                crypto::SignatureAlgorithm algo;
        };
    }
}

#endif /* __UTPL_CRYPTO_MANAGER_OPENSSL_SIGNATURE_H__ */