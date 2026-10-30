#include <openssl/x509.h>
#include <openssl/err.h>

#include "utpl/common/types/exception.h"
#include "utpl/common/debug.h"

#include "types.h"

#include "signature.h"

using namespace utpl;

crypto::OpensslSignature::OpensslSignature(crypto::SignatureAlgorithm algo) {
    this->algo  = algo;
    this->mdCtx = nullptr;
}

crypto::OpensslSignature::~OpensslSignature() {
    this->term();
}

void crypto::OpensslSignature::initSign(const crypto::PrivateKey &key) {
    this->term();

    this->signMode = true;

    this->init(nullptr, &key);
}

std::vector<uint8_t> crypto::OpensslSignature::sign() {
    std::vector<uint8_t> ret;

    if (! this->signMode) {
        throw utpl::InvalidStateException("Not in sigining mode");
    }

    {
        size_t sigLen;

        if (this->buffer) {
            if (! EVP_DigestSign(this->mdCtx, nullptr, &sigLen, this->buffer->data(), this->buffer->size())) {
                throw utpl::InvalidStateException("EVP_DigestSign() returned error");
            }

            ret.resize(sigLen);

            if (! EVP_DigestSign(this->mdCtx, ret.data(), &sigLen, this->buffer->data(), this->buffer->size())) {
                throw utpl::InvalidStateException("EVP_DigestSign() returned error");
            }

            this->buffer->clear();

        } else {
            EVP_DigestSignFinal(this->mdCtx, nullptr, &sigLen);

            ret.resize(sigLen);

            EVP_DigestSignFinal(this->mdCtx, ret.data(), &sigLen);
        }
    }
    
    return ret;
}

void crypto::OpensslSignature::update(const void *data, size_t dataSize) {
    if (this->mdCtx == nullptr) {
        throw utpl::InvalidStateException("Not initialized");
    }

    if (this->buffer) {
        this->buffer->insert(this->buffer->end(), (uint8_t *) data, ((uint8_t *) data) + dataSize);
        
    } else {
        if (this->signMode) {
            if (EVP_DigestSignUpdate(this->mdCtx, data, dataSize) != 1) {
                throw utpl::InvalidStateException("EVP_DigestSignUpdate() returned error");
            }

        } else {
            if (EVP_DigestVerifyUpdate(this->mdCtx, data, dataSize)) {
                throw utpl::InvalidStateException("EVP_DigestVerifyUpdate() returned error");
            }
        }
    }
}

void crypto::OpensslSignature::initVerify(const crypto::PublicKey &key) {
    this->term();

    this->signMode = false;

    this->init(&key, nullptr);
}

bool crypto::OpensslSignature::verify(const void *data, size_t dataSize) {
    bool ret = false;

    if (this->signMode) {
        throw InvalidStateException("Not in verification mode");
    }

    if (this->buffer) {
        ret = EVP_DigestVerify(this->mdCtx, (const unsigned char *) data, dataSize, this->buffer->data(), this->buffer->size()) == 1;

        this->buffer->clear();

    } else {
        ret = EVP_DigestVerifyFinal(this->mdCtx, (const unsigned char *) data, dataSize) == 1;
    }

    return ret;
}

void crypto::OpensslSignature::init(const PublicKey *publicKey, const PrivateKey *privateKey) {
    this->mdCtx = EVP_MD_CTX_new();

    // The PureEdDSA algorithm does not support the streaming mechanism of other signature algorithms using, for example, EVP_DigestUpdate(). 
    // The message to sign or verify must be passed using the one-shot EVP_DigestSign() and EVP_DigestVerify() functions.
    if (this->algo == crypto::SignatureAlgorithm::ED25519) {
        this->buffer = std::make_unique<std::vector<uint8_t>>();
    }

    {
        EVP_PKEY *keyHandle;

        bool throwException = false;

        if (publicKey) {
            const auto *key = reinterpret_cast<const OpensslPublicKey *>(publicKey);

            if (! EVP_DigestVerifyInit(this->mdCtx, nullptr, nullptr, nullptr, (EVP_PKEY *) key->getHandle())) {
                throwException = true;
            }

        } else {
            const auto *key = reinterpret_cast<const OpensslPrivateKey *>(privateKey);

            if (! EVP_DigestSignInit(this->mdCtx, nullptr, nullptr, nullptr, (EVP_PKEY *) key->getHandle())) {
                throwException = true;
            }
        }

        if (throwException) {
            throw utpl::Exception("Error initializing signing/verifying context!");
        }
    }
}

void crypto::OpensslSignature::term() {
    this->buffer.reset();

    if (this->mdCtx) {
        EVP_MD_CTX_destroy(this->mdCtx);

        this->mdCtx = nullptr;
    }
}