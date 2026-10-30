#ifndef __UTPL_CRYPTO_MANAGER_OPENSSL_CIPHER_H__
#define __UTPL_CRYPTO_MANAGER_OPENSSL_CIPHER_H__

#include <openssl/evp.h>

#include "utpl/crypto/cipher.h"
#include "utpl/crypto/manager.h"

namespace utpl {
    namespace crypto {
        class OpensslCipher: public crypto::Cipher {
            public:
                OpensslCipher(CryptoManager *manager, CipherAlgorithm algo, CipherMode mode);
                ~OpensslCipher();

                void init(bool encrypt, const Key &key, const CipherParameters *parameters) override;

                std::vector<uint8_t> update(const void *data, size_t dataSize) override;
                std::vector<uint8_t> final(const void *data, size_t dataSize) override;

                void updateAAD(const void *data, size_t dataSize) override;

                const std::vector<uint8_t> &getIv() const override;

            private:
                void _final(unsigned char *outm, int *outl);
                void _update(unsigned char *out, int *outl, const void *in, int inl);

                void processAAD();

                void init(const crypto::Key &key);
                void term();

            private:
                EVP_CIPHER_CTX *ctx;
                EVP_CIPHER     *cipher;

                bool encrypt;

                CipherAlgorithm algo;
                CipherMode      mode;
                CryptoManager  *manager;

                std::vector<uint8_t> iv;
                
                std::unique_ptr<std::vector<uint8_t>> inBuffer;
                std::unique_ptr<std::vector<uint8_t>> aad;
        };
    }
}

#endif /* __UTPL_CRYPTO_MANAGER_OPENSSL_CIPHER_H__ */