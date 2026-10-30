#ifndef __UTPL_LICENSE_ELF_KEYLOADER_H__
#define __UTPL_LICENSE_ELF_KEYLOADER_H__

#include <string>
#include <vector>
#include <memory>
#include <inttypes.h>

#include "utpl/crypto/types.h"

namespace utpl {
    namespace license {
        class ElfKeyLoader {
            public:
                ElfKeyLoader();

                void load(const std::string &path);

                const crypto::PublicKey &getPublicKey();
                const crypto::Key       &getEncryptionKey();

            private:
                std::unique_ptr<const crypto::PublicKey> publicKey;
                std::unique_ptr<const crypto::Key>       encKey;
        };
    }
}

#endif /* __UTPL_LICENSE_ELF_KEYLOADER_H__ */