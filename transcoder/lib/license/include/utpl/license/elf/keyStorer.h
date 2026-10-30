#ifndef __UTPL_LICENSE_ELF_KEYSTORER_H__
#define __UTPL_LICENSE_ELF_KEYSTORER_H__

#include <string>
#include <vector>
#include <cinttypes>

namespace utpl {
    namespace license {
        class ElfKeyStorer {
            public:
                ElfKeyStorer();

                void setPublicKey(const std::vector<uint8_t> &key);
                void setEncryptionKey(const std::vector<uint8_t> &key);

                void store(const std::string &filePath);

            private:
                std::vector<uint8_t> publicKey;
                std::vector<uint8_t> encKey;
        };
    }
}

#endif /* __UTPL_LICENSE_ELF_KEYSTORER_H__ */