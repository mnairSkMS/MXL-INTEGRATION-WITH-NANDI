#ifndef __UTPL_LICENSE_UTILS_ELF_H__
#define __UTPL_LICENSE_UTILS_ELF_H__

#include <string>
#include <vector>

#include <gelf.h>

namespace utpl {
    namespace license {
        class ElfReader {
            public:
                class Section {
                    public:
                        Section(const std::string &name, int index, Elf_Data *data, Elf64_Word type);

                        int getIndex() const;
                        const std::string &getName() const;

                        const void *data() const;
                        size_t length() const;
                        bool isStringTab() const;
                        bool isDynamic() const;

                    private:
                        int         _index;
                        std::string _name;
                        Elf_Data   *_data;
                        Elf64_Word  _type;
                };

            public:
                ElfReader();
                ~ElfReader();

                bool load(const std::string &path);
                void close();

                const std::vector<Section> &getSections() const;
                const Section *getSection(const std::string &sectionName) const;

            private:
                int fd;
                Elf *elf;

                std::vector<Section> sections;
        };
    }
}

#endif /* __UTPL_LICENSE_UTILS_ELF_H__ */
