#include "utpl/crypto/manager.h"

#include "manager/openssl/manager.h"

using namespace utpl;

crypto::CryptoManager &crypto::getCryptoManager() {
    static crypto::OpensslCryptoManager managerImpl;

    return managerImpl;
}