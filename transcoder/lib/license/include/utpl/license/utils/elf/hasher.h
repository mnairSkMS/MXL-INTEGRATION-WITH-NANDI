#include <string>
#include <vector>
#include <set>
#include <cinttypes>

#include "utpl/crypto/hasher.h"

namespace utpl {
    namespace license {
        class ElfHasher {
            public:
                ElfHasher(const std::string &path);

                std::vector<uint8_t> hash(crypto::Hasher &hasher, const std::set<std::string> &ignoredSections = {});

            private:
                std::string path;
        };
    }
}