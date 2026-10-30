#ifndef __UTPL_LICENSE_CONFIG_H__
#define __UTPL_LICENSE_CONFIG_H__

#include "utpl/crypto/types.h"
#include "utpl/common/time.h"

#define UTPL_LICENSE_LOGGER_NAME "license"

#define UTPL_LICENSE_ENV_KEY_PRIVATE_VAR_NAME    "LICENSE_PRIVATE_KEY"
#define UTPL_LICENSE_ENV_KEY_PUBLIC_VAR_NAME     "LICENSE_PUBLIC_KEY"
#define UTPL_LICENSE_ENV_KEY_ENCRYPTION_VAR_NAME "LICENSE_ENCRYPTION_KEY"

#define UTPL_LICENSE_KEY_SECTION_NAME             ".skn"
#define UTPL_LICENSE_KEY_SECTION_DELIMITER        '.'
#define UTPL_LICENSE_KEY_SECTION_HASH_ALGORITHM   utpl::crypto::HashAlgorithm::SHA256
#define UTPL_LICENSE_KEY_SECTION_CIPHER_ALGORITHM utpl::crypto::CipherAlgorithm::AES
#define UTPL_LICENSE_KEY_SECTION_CIPHER_MODE      utpl::crypto::CipherMode::GCM

#define UTPL_LICENSE_SIGNATURE_ALGO               crypto::SignatureAlgorithm::ED25519
#define UTPL_LICENSE_EPOCH_START                  utpl::Time::fromIso8601("2020-01-01")
#define UTPL_LICENSE_TTL_MAX                      36525 // 100 years since start of epoch

#endif /* __UTPL_LICENSE_CONFIG_H__ */