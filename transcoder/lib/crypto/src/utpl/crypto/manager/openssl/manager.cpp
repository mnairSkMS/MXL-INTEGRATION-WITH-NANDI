#include <fstream>

#include <openssl/rand.h>

#include "manager.h"
#include "cipher.h"
#include "hasher.h"
#include "signature.h"
#include "types.h"

using namespace utpl;

crypto::OpensslCryptoManager::~OpensslCryptoManager() {

}

std::unique_ptr<crypto::Hasher> crypto::OpensslCryptoManager::newHasher(HashAlgorithm algo) {
    return std::make_unique<crypto::OpensslHasher>(algo);
}

std::unique_ptr<crypto::Cipher> crypto::OpensslCryptoManager::newCipher(CipherAlgorithm algo, CipherMode mode) {
    return std::make_unique<crypto::OpensslCipher>(this, algo, mode);
}

std::unique_ptr<crypto::Signature> crypto::OpensslCryptoManager::newSignature(SignatureAlgorithm algo) {
    return std::make_unique<crypto::OpensslSignature>(algo);
}

std::unique_ptr<crypto::Key> crypto::OpensslCryptoManager::generateKey(size_t bitsSize) {
    return std::make_unique<OpensslKey>(genRandom(bitsSize / 8));
}

std::vector<uint8_t> crypto::OpensslCryptoManager::genRandom(size_t bytes) {
    std::vector<uint8_t> ret(bytes, 0);

    RAND_bytes(ret.data(), ret.size());

    return ret;
}

static std::vector<uint8_t> loadFile(const std::string &path) {
    std::vector<uint8_t> ret;

    {
        std::ifstream stream(path, std::ios::binary);

        stream.seekg(0, std::ios::end);

        ret.resize(stream.tellg());

        stream.seekg(0, std::ios::beg);
        stream.read(reinterpret_cast<char *>(ret.data()), ret.size()); 
        stream.close();
    }

    return ret;
}

std::unique_ptr<crypto::Key> crypto::OpensslCryptoManager::loadKey(const std::string &keyPath) {
    return std::make_unique<OpensslKey>(loadFile(keyPath));
}

std::unique_ptr<crypto::Key> crypto::OpensslCryptoManager::loadKey(const void *keyData, size_t keyDataSize) {
    return std::make_unique<OpensslKey>(keyData, keyDataSize);
}

std::unique_ptr<crypto::PublicKey> crypto::OpensslCryptoManager::loadPublicKey(const std::string &keyPath) {
    auto key = loadFile(keyPath);

    return std::make_unique<OpensslPublicKey>(key.data(), key.size());
}

std::unique_ptr<crypto::PublicKey> crypto::OpensslCryptoManager::loadPublicKey(const void *keyData, size_t keyDataSize) {
    return std::make_unique<OpensslPublicKey>(keyData, keyDataSize);
}

std::unique_ptr<crypto::PrivateKey> crypto::OpensslCryptoManager::loadPrivateKey(const std::string &keyPath) {
    auto key = loadFile(keyPath);

    return std::make_unique<OpensslPrivateKey>(key.data(), key.size());
}

std::unique_ptr<crypto::PrivateKey> crypto::OpensslCryptoManager::loadPrivateKey(const void *keyData, size_t keyDataSize) {
    return std::make_unique<OpensslPrivateKey>(keyData, keyDataSize);
}