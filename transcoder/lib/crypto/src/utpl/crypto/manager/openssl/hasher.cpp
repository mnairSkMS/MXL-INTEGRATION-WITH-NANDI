#include "utpl/common/types/exception.h"

#include "hasher.h"

using namespace utpl;

crypto::OpensslHasher::OpensslHasher(crypto::HashAlgorithm algo) {
    this->md = nullptr;

    switch (algo) {
        case crypto::HashAlgorithm::SHA256: this->md = EVP_sha256(); break;
        default:
            throw utpl::InvalidArgumentException("Not supported hashing algorithm " + std::to_string((int)algo));
    }

    this->ctx = EVP_MD_CTX_create();

    reset();
}

crypto::OpensslHasher::~OpensslHasher() {
    EVP_MD_CTX_destroy(ctx);
    
    this->ctx = nullptr;
}

void crypto::OpensslHasher::reset() {
    EVP_DigestInit_ex(ctx, this->md, nullptr);
}

void crypto::OpensslHasher::update(const void *data, size_t dataSize) {
    EVP_DigestUpdate(this->ctx, data, dataSize);
}

std::vector<uint8_t> crypto::OpensslHasher::final() {
    std::vector<uint8_t> ret(EVP_MAX_MD_SIZE, 0);

    {
        unsigned int sigLength;

        EVP_DigestFinal_ex(ctx, ret.data(), &sigLength);

        ret.resize(sigLength);
    }

    return ret;
}