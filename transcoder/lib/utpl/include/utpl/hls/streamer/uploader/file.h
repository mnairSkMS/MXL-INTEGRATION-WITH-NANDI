#ifndef __UTPL_HLS_STREAMER_UPLOADER_FILE__
#define __UTPL_HLS_STREAMER_UPLOADER_FILE__

#include <condition_variable>
#include <mutex>
#include <thread>
#include <list>

#include "utpl/hls/streamer/uploader/base.h"

namespace utpl {
    namespace hls {
        class FileUploader : public BaseUploader {
            public:
                FileUploader(const std::string &basePath);

            protected:
                utpl::Error doUpload(const std::string &path, std::shared_ptr<hls::SegmentStorage> storage, Timestamp timeout) override;
                utpl::Error doRemoval(const std::string &path, Timestamp timeout) override;
                utpl::Error doMkir(const std::string &path, Timestamp timeout) override;
                utpl::Error doStat(const std::string &path, Timestamp timeout, bool &exist, bool &isDirectory) override;

            private:
                std::string getPath(const std::string &path);

            private:
                std::string _basePath;
        };
    }
}

#endif /* __UTPL_HLS_STREAMER_UPLOADER_FILE__ */