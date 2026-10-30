#ifndef __UTPL_HLS_STREAMER_UPLOADER_HTTP__
#define __UTPL_HLS_STREAMER_UPLOADER_HTTP__

#include <cpr/cpr.h>

#include <condition_variable>
#include <mutex>
#include <thread>
#include <list>

#include "utpl/hls/streamer/uploader/base.h"

namespace utpl {
    namespace hls {
        class HttpUploader : public BaseUploader {
            public:
                HttpUploader(const std::string &serverUrl, bool dav, bool chunked);
                
                void setAuthorization(const utpl::Credentials &Credentials, Authentication auth);

            protected:
                utpl::Error doUpload(const std::string &path, std::shared_ptr<hls::SegmentStorage> storage, Timestamp timeout) override;
                utpl::Error doRemoval(const std::string &path, Timestamp timeout) override;
                utpl::Error doMkir(const std::string &path, Timestamp timeout) override;
                utpl::Error doStat(const std::string &path, Timestamp timeout, bool &exist, bool &isDirectory) override;

            private:
                cpr::Url getUrl(const std::string &path);

            private:
                std::string _serverUrl;

                std::unique_ptr<cpr::Session> _session;
                std::set<std::string> _existingDirectories;
                bool _chunked;
        };
    }
}

#endif /* __UTPL_HLS_STREAMER_UPLOADER_HTTP__ */