#ifndef __UTPL_LICENSE_UTILS_ELF_MODIFIER_H__
#define __UTPL_LICENSE_UTILS_ELF_MODIFIER_H__

#include <vector>
#include <string>
#include <cinttypes>

namespace utpl {
    namespace license {
        class ElfModifier {
            public:
                static void addSection(const std::string &path, const std::string &sectionName, const std::vector<uint8_t> &sectionData) {
                    addSection(path, sectionName, sectionData.data(), sectionData.size());
                }
                
                static void addSection(const std::string &path, const std::string &sectionName, const std::string &sectionData) {
                    addSection(path, sectionName, sectionData.data(), sectionData.size());
                }

                static void addSection(const std::string &path, const std::string &sectionName, const void *data, size_t dataSize);
                static void removeSection(const std::string &path, const std::string &sectionName);
        };
    }
}

#endif /* __UTPL_LICENSE_UTILS_ELF_MODIFIER_H__ */