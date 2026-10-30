#include "utpl/common/types/exception.h"

#include "utpl/license/utils/elf/hasher.h"
#include "utpl/license/utils/elf/reader.h"

#include "utpl/license/debug.h"

using namespace utpl;


license::ElfHasher::ElfHasher(const std::string &path) : path(path) {
}

std::vector<uint8_t> license::ElfHasher::hash(crypto::Hasher &hasher, const std::set<std::string> &ignoredSections) {
    license::ElfReader reader;

    LIC_DBG(("Calculating hash of file '{}'", this->path));

    if (! reader.load(this->path)) {
        throw utpl::InvalidArgumentException("File " + this->path + " does not exist or is not readable or it not an ELF!");
    }

    std::vector<uint8_t> partialHash;

    for (auto section : reader.getSections()) {
        auto *data = section.data();
        
        if (data != nullptr) {
            if (section.isStringTab()) {
                LIC_DBG(("Skipping string table section '{}'", section.getName()));

                continue;
            }

            if (section.isDynamic()) {
                LIC_DBG(("Skipping dynamic section '{}'", section.getName()));

                continue;
            }

            if (ignoredSections.find(section.getName()) != ignoredSections.end()) {
                LIC_DBG(("Ignoring section '{}'", section.getName()));

                continue;
            }

            {
                LIC_DBG(("hashing section '{}'", section.getName()));

                hasher.reset();
                hasher.update(data, section.length());
                
                auto sectionHash = hasher.final();

                partialHash.insert(partialHash.end(), sectionHash.begin(), sectionHash.end());
            }
        }
    }

    LIC_DBG(("Hashing partial hashes of size: {}", partialHash.size()));

    hasher.reset();
    hasher.update(partialHash);

    return hasher.final();
}