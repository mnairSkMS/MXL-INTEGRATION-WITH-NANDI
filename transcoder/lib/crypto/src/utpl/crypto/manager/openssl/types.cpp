#include <openssl/x509.h>

#include "utpl/common/types/exception.h"
#include "types.h"

using namespace utpl;


crypto::OpensslKey::OpensslKey(const std::vector<uint8_t> &data) : _raw(data) {
}

crypto::OpensslKey::OpensslKey(const void *data, size_t dataSize) : _raw((uint8_t *) data, ((uint8_t *) data) + dataSize) {
}

const std::vector<uint8_t> &crypto::OpensslKey::raw() const {
    return this->_raw;
}

size_t crypto::OpensslKey::size() const {
    return this->_raw.size() * 8;
}

crypto::OpensslPrivateKey::OpensslPrivateKey(const void *data, size_t dataSize) : _key(data, dataSize) {
    BIO *bio = BIO_new_mem_buf(data, dataSize);

    this->_evpKey = d2i_PrivateKey_bio(bio, nullptr);

    BIO_free_all(bio);

    if (this->_evpKey == nullptr) {
        throw utpl::InvalidArgumentException("Unable to load private key");
    }
}

crypto::OpensslPrivateKey::~OpensslPrivateKey() {
    if (this->_evpKey) {
        EVP_PKEY_free(this->_evpKey);
    }
}

crypto::OpensslPublicKey::OpensslPublicKey(const void *data, size_t dataSize) : _key(data, dataSize) {
    BIO *bio = BIO_new_mem_buf(data, dataSize);

    this->_evpKey = d2i_PUBKEY_bio(bio, nullptr);

    BIO_free_all(bio);

    if (this->_evpKey == nullptr) {
        throw utpl::InvalidArgumentException("Unable to load public key");
    }
}

crypto::OpensslPublicKey::~OpensslPublicKey() {
    if (this->_evpKey) {
        EVP_PKEY_free(this->_evpKey);
    }
}