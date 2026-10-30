#include <filesystem>
#include <fstream>

#include "utpl/common/debug.h"

#include "utpl/hls/streamer/uploader/file.h"

using namespace utpl;


hls::FileUploader::FileUploader(const std::string &basePath) : BaseUploader(Capabilities(true, false)), _basePath(basePath) {

}

utpl::Error hls::FileUploader::doUpload(const std::string &path, std::shared_ptr<hls::SegmentStorage> storage, Timestamp timeout) {
    utpl::Error ret = utpl::Error::NO_ERROR;

    {
        std::filesystem::path p = this->getPath(path);

        // Written beside the target and renamed over it, so a web server serving this directory
        // never hands out a truncated playlist, manifest or segment while it is being rewritten.
        std::filesystem::path tmp = p;
        tmp += ".tmp";

        DBG(("Writing file {} with {}B of data", p.string(), storage->length()));

        {
            std::ofstream f(tmp.string());

            if (f.is_open()) {
                uint8_t buffer[4096];

                size_t totalRead = 0;

                size_t read = 0;
                do {
                    read = storage->read(buffer, totalRead, sizeof(buffer));
                    if (read) {
                        f.write((const char *) buffer, read);

                        totalRead += read;
                    }
                } while (read > 0);

                f.close();

                std::error_code ec;
                std::filesystem::rename(tmp, p, ec);

                if (ec) {
                    ERR(("Unable to move {} into place: {}", tmp.string(), ec.message()));

                    std::filesystem::remove(tmp, ec);

                    ret = Error::UNKNOWN_ERROR;
                }

            } else {
                ret = Error::UNKNOWN_ERROR;
            }
        }
    }

    return ret;
}

Error hls::FileUploader::doRemoval(const std::string &path, Timestamp timeout) {
    std::filesystem::path p = this->getPath(path);

    if (std::filesystem::exists(p)) {
        if (! std::filesystem::remove(p)) {
            return Error::UNKNOWN_ERROR;
        }
    }

    return Error::NO_ERROR;
}

Error hls::FileUploader::doMkir(const std::string &path, Timestamp timeout) {
    std::filesystem::path p = this->getPath(path);

    if (std::filesystem::create_directory(p)) {
        return Error::NO_ERROR;
    }

    return Error::UNKNOWN_ERROR;
}

Error hls::FileUploader::doStat(const std::string &path, Timestamp timeout, bool &exist, bool &isDirectory) {
    std::filesystem::path p = this->getPath(path);

    exist = std::filesystem::exists(p);
    if (exist) {
        isDirectory = std::filesystem::is_directory(p);

    } else {
        isDirectory = false;
    }

    return Error::NO_ERROR;
}

std::string hls::FileUploader::getPath(const std::string &path) {
    auto fullPath = path.empty() ? this->_basePath : this->_basePath + "/" + path;
    
    return std::filesystem::path(fullPath);
}