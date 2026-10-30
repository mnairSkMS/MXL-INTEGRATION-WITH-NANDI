#include "utpl/common/debug.h"
#include "utpl/common/types/exception.h"
#include "utpl/common/utils.h"

#include "utpl/crypto/manager.h"

#include "utpl/license/config.h"
#include "utpl/license/signature.h"

using namespace utpl;

bool license::LicenseSignature::verify(const std::vector<uint8_t> &licenseData, const std::vector<uint8_t> &licenseSignature, const crypto::PublicKey &publicKey) {
    auto signature = crypto::getCryptoManager().newSignature(UTPL_LICENSE_SIGNATURE_ALGO);

    signature->initVerify(publicKey);
    signature->update(licenseData);

    return signature->verify(licenseSignature);
}

std::vector<uint8_t> license::LicenseSignature::sign(const std::vector<uint8_t> &licenseData, const crypto::PrivateKey &privateKey) {
    auto signature = crypto::getCryptoManager().newSignature(UTPL_LICENSE_SIGNATURE_ALGO);
    
    signature->initSign(privateKey);
    signature->update(licenseData);

    return signature->sign();
}