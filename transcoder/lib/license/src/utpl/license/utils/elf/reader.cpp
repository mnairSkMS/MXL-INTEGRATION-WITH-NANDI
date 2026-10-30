#include <cstdio>
#include <unistd.h>
#include <fcntl.h>

#include "utpl/license/utils/elf/reader.h"
#include "utpl/license/debug.h"

using namespace utpl;


static bool isDwarfSection(const std::string &name) {
    return name.rfind(".debug_", 0) != std::string::npos;
}

license::ElfReader::Section::Section(const std::string &name, int index, Elf_Data *data, Elf64_Word type) : _name(name) {
    this->_index = index;
    this->_type  = type;
    this->_data  = data;
}

int license::ElfReader::Section::getIndex() const {
    return this->_index;
}

const std::string &license::ElfReader::Section::getName() const {
    return this->_name;
}

const void *license::ElfReader::Section::data() const {
    return this->_data->d_buf;
}

size_t license::ElfReader::Section::length() const {
    return this->_data->d_size;
}

bool license::ElfReader::Section::isStringTab() const {
    return this->_type == SHT_STRTAB;
}

bool license::ElfReader::Section::isDynamic() const {
    return this->_type == SHT_DYNAMIC;
}

license::ElfReader::ElfReader() {
    this->elf = nullptr;
    this->fd  = -1;
    
    elf_version(EV_CURRENT);
}

license::ElfReader::~ElfReader() {
    this->close();
}

bool license::ElfReader::load(const std::string &path) {
    bool ret = true;

    this->close();

    if (ret) {
        this->fd = open(path.c_str(), O_RDONLY);
        if (this->fd < 0) {
            LIC_ERR(("Unable to open ELF file at '{}'", path));

            ret = false;
        }
    }

    if (ret) {
        this->elf = elf_begin(this->fd, ELF_C_READ, nullptr);
        if (this->elf == nullptr) {
            LIC_ERR(("Unable initialize ELF library on file '{}', descriptor: {}", path, this->fd));

            ret = false;
        }
    }

    if (ret) {
        size_t sectionCount = 0;
        size_t stringsSectionIndex;

        if (elf_getshdrnum(this->elf, &sectionCount)) {
            LIC_ERR(("Unable to get sections count!"));

            ret = false;

        } else {
            LIC_DBG(("ELF file at {} has {} sections", path, sectionCount));

            if (sectionCount > 0) {
                if (elf_getshdrstrndx(this->elf, &stringsSectionIndex)) {
                    LIC_ERR(("Unable to get index of string section!"));

                    ret = false;

                } else {
                    for (size_t i = 0; i < sectionCount; i++) {
                        Elf_Scn *section = elf_getscn(this->elf, i);
                        if (section == nullptr) {
                            LIC_ERR(("Unable to get section object for index {}/{}", i, sectionCount));

                            ret = false;
                            break;
                        }

                        GElf_Shdr shdr;

                        if (! gelf_getshdr(section, &shdr)) {
                            LIC_ERR(("Unable to get section {} shdr header", i));

                            ret = false;
                            break;
                        }

                        std::string sectionName = elf_strptr(this->elf, stringsSectionIndex, shdr.sh_name);

                        if (shdr.sh_size != 0 && ! isDwarfSection(sectionName)) {
                            Elf_Data *elfData = elf_getdata(section, nullptr);
                            if (elfData == nullptr) {
                                LIC_ERR(("Unable to get section data!"));

                                ret = false;
                                break;
                            }

                            if (elfData->d_off != 0 || elfData->d_size != shdr.sh_size) {
                                LIC_ERR(("Unexpected data attributes for {}", sectionName));

                                ret = false;
                                break;
                            }

                            this->sections.push_back(Section(sectionName, elf_ndxscn(section), elfData, shdr.sh_type));
                        }
                    }
                }
            }
        }
    }

    return ret;
}

void license::ElfReader::close() {
    if (this->elf) {
        elf_end(this->elf);

        this->elf = nullptr;
    }

    if (this->fd >= 0) {
        ::close(this->fd);

        this->fd = -1;
    }
}

const std::vector<license::ElfReader::Section> &license::ElfReader::getSections() const {
    return this->sections;
}

const license::ElfReader::Section *license::ElfReader::getSection(const std::string &sectionName) const {
    auto it = std::find_if(this->sections.begin(), this->sections.end(), [&sectionName](const Section &sec) {
        return sec.getName() == sectionName;
    });

    if (it != this->sections.end()) {
        return &(*it);
    }

    return nullptr;
}