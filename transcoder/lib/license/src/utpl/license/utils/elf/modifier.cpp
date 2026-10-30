#include <fstream>

#include <unistd.h>
#include <fcntl.h>

#include <boost/process.hpp>

#include "utpl/common/types/exception.h"
#include "utpl/license/utils/elf/modifier.h"
#include "utpl/license/debug.h"

using namespace utpl;


static boost::filesystem::path getObjcopyPath() {
    static boost::filesystem::path ret = boost::process::search_path("objcopy");

    if (ret.empty()) {
        throw NotSupportedException("'objcopy' binary not found.");
    }

    return ret;
}

void license::ElfModifier::addSection(const std::string &path, const std::string &sectionName, const void *data, size_t dataSize) {
    std::string sectionFilePath = path + ".sec.bin";

    try {
        std::ofstream out(sectionFilePath);

        out.write((char *) data, dataSize);
        out.close();

        int callRet = boost::process::system(getObjcopyPath(), "--add-section", sectionName + "=" + sectionFilePath, path, boost::process::std_out > stdout, boost::process::std_err > stderr);
        if (callRet != 0) {
            throw utpl::InvalidArgumentException("Cannot add section '" + sectionName + "' to file '" + path + "'");
        }

    } catch (const utpl::Exception &e) {
        throw;

    } catch (const std::exception &e) {
        LIC_ERR(("Cought exception: {}", e.what()));

        throw utpl::InvalidArgumentException("Cannot add section '" + sectionName + "' to file '" + path + "'");
    }
}

void license::ElfModifier::removeSection(const std::string &path, const std::string &sectionName) {
    int callRet = boost::process::system(getObjcopyPath(), "--remove-section", sectionName, path, boost::process::std_out > stdout, boost::process::std_err > stderr);
    if (callRet != 0) {
        throw utpl::InvalidArgumentException("Cannot add section '" + sectionName + "' to file '" + path + "'");
    }
}