#include <openssl/aes.h>

#include "utpl/common/types/exception.h"
#include "utpl/crypto/cipher.h"

#include "cipher.h"


using namespace utpl;

#define TAG_LENGTH 16

crypto::OpensslCipher::OpensslCipher(CryptoManager *manager, crypto::CipherAlgorithm algo, crypto::CipherMode mode) {
    this->algo    = algo;
    this->mode    = mode;
    this->manager = manager;

    this->ctx    = nullptr;
    this->cipher = nullptr;
}

crypto::OpensslCipher::~OpensslCipher() {
    this->term();
}

void crypto::OpensslCipher::init(bool encrypt, const crypto::Key &key, const crypto::CipherParameters *parameters) {
    this->term();

    this->encrypt = encrypt;

    if (parameters != nullptr) {
        if (this->mode == crypto::CipherMode::GCM) {
            auto *params = dynamic_cast<const crypto::CipherIvParameters *>(parameters);

            if (params) {
                this->iv = params->getIv();
            }
        }
    }

    this->init(key);

    if (this->aad) {
        this->aad->clear();

    } else {
        this->aad = std::make_unique<std::vector<uint8_t>>();
    }

    if (! this->encrypt) {
        this->inBuffer = std::make_unique<std::vector<uint8_t>>();
    }
}

std::vector<uint8_t> crypto::OpensslCipher::update(const void *data, size_t dataSize) {
    std::vector<uint8_t> ret;

    this->processAAD();

    if (this->encrypt) {
        int written = 0;

        ret.resize(dataSize);

        this->_update(ret.data(), &written, data, dataSize);

        if (ret.size() != dataSize) {
            ret.resize(dataSize);
        }

    } else {
        this->inBuffer->insert(this->inBuffer->end(), (uint8_t *) data, ((uint8_t *) data) + dataSize);
    }

    return ret;
}

std::vector<uint8_t> crypto::OpensslCipher::final(const void *data, size_t dataSize) {
    std::vector<uint8_t> ret;
    int retWritten = 0;

    this->processAAD();

    if (this->encrypt) {
        ret.resize(dataSize + AES_BLOCK_SIZE + TAG_LENGTH);

        this->_update(ret.data(), &retWritten, data, dataSize);

    } else {
        // TODO: Optimize - skip redundant copy
        this->inBuffer->insert(this->inBuffer->end(), (uint8_t *) data, ((uint8_t *) data) + dataSize);

        if (this->inBuffer->size() < TAG_LENGTH) {
            throw utpl::InvalidArgumentException("Input too short - need tag");
        }

        size_t remainingToUpdate = this->inBuffer->size() - TAG_LENGTH;

        ret.resize(remainingToUpdate + AES_BLOCK_SIZE);

        this->_update(ret.data(), &retWritten, this->inBuffer->data(), remainingToUpdate);

        if (EVP_CIPHER_CTX_ctrl(this->ctx, EVP_CTRL_GCM_SET_TAG, TAG_LENGTH, this->inBuffer->data() + remainingToUpdate) != 1) {
            throw utpl::Exception("Failed to set tag");
        }
    }

    {
        int written;

        this->_final(ret.data() + retWritten, &written);

        retWritten += written;
    }

    if (this->encrypt) {
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_LENGTH, ret.data() + retWritten) != 1) {
            throw utpl::Exception("Error getting GCM tag");
        }

        retWritten += TAG_LENGTH;
    }

    if (ret.size() != retWritten) {
        ret.resize(retWritten);
    }

    return ret;
}

void crypto::OpensslCipher::updateAAD(const void *data, size_t dataSize) {
    if (! this->aad) {
        throw utpl::InvalidStateException("Update has been called; no more AAD data");
    }

    this->aad->insert(this->aad->end(), (uint8_t *) data, ((uint8_t *) data) + dataSize);
}

const std::vector<uint8_t> &crypto::OpensslCipher::getIv() const {
    return this->iv;
}

void crypto::OpensslCipher::_final(unsigned char *outm, int *outl) {
    int ret;

    if (this->encrypt) {
        ret = EVP_EncryptFinal_ex(ctx, outm, outl);

    } else {
        ret = EVP_DecryptFinal_ex(ctx, outm, outl);
    }

    if (ret != 1) {
        throw utpl::Exception("GCM TAG Verification has failed");
    }
}

void crypto::OpensslCipher::_update(unsigned char *out, int *outl, const void *in, int inl) {
    int ret;

    if (this->encrypt) {
        ret = EVP_EncryptUpdate(this->ctx, out, outl, (const unsigned char *) in, inl);

    } else {
        ret = EVP_DecryptUpdate(this->ctx, out, outl, (const unsigned char *) in, inl);
    }
    if (ret != 1) {
        throw utpl::Exception("Error updating data!");
    }
}

void crypto::OpensslCipher::processAAD() {
    if (this->aad && ! this->aad->empty()) {
        int tmp;
        
        this->_update(nullptr, &tmp, this->aad->data(), this->aad->size());

        this->aad.reset();
    }
}

void crypto::OpensslCipher::init(const crypto::Key &key) {
    {
        std::string cipherName;

        switch (algo) {
            case crypto::CipherAlgorithm::AES:
                cipherName = "AES";
                break;
                
            default:
                throw utpl::InvalidArgumentException("Not supported cipher algorithm " + std::to_string((int)algo));
        }

        cipherName += ("-" + std::to_string(key.size()));

        switch (mode) {
            case crypto::CipherMode::GCM:
                cipherName += "-GCM";
                break;

            default:
                throw utpl::InvalidArgumentException("Not supported cipher mode " + std::to_string((int)mode));
        }

        this->cipher = EVP_CIPHER_fetch(nullptr, cipherName.c_str(), nullptr);
        if (this->cipher == nullptr) {
            throw utpl::InvalidArgumentException("Not supported cipher name '" + cipherName + "'");
        }
    }

    this->ctx = EVP_CIPHER_CTX_new();
    if (! this->ctx) {
        throw utpl::NoFreeMemoryException("Failed to create cipher context");
    }

    {
        size_t expectedIvLength = EVP_CIPHER_get_iv_length(this->cipher);

        if (expectedIvLength > 0) {
            if (this->iv.empty()) {
                this->iv = this->manager->genRandom(expectedIvLength);

            } else if (this->iv.size() != expectedIvLength) {
                throw utpl::InvalidArgumentException("Invalid IV given. Expected " + std::to_string(expectedIvLength) + " bytes but have " + std::to_string(this->iv.size()));
            }
        }
    }

    if (this->encrypt) {
        if (EVP_EncryptInit_ex(this->ctx, this->cipher, NULL, key.raw().data(), this->iv.data()) != 1) {
            utpl::Exception("Failed to initialize encryption");
        }

    } else {
        if (EVP_DecryptInit(this->ctx, this->cipher, key.raw().data(), this->iv.data())) {
            utpl::Exception("Failed to initialize decryption");
        }
    }
}

void crypto::OpensslCipher::term() {
    if (this->cipher) {
        EVP_CIPHER_free(this->cipher);

        this->cipher = nullptr;
    }
    
    if (this->ctx) {
        EVP_CIPHER_CTX_free(this->ctx);

        this->ctx = nullptr;
    }

    this->iv.clear();
}
