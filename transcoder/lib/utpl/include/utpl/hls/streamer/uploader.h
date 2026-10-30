#ifndef __UTPL_HLS_STREAMER_UPLOADER_H__
#define __UTPL_HLS_STREAMER_UPLOADER_H__

#include <memory>
#include <functional>

#include "utpl/common/utils/class.h"
#include "utpl/common/types.h"
#include "utpl/common/time.h"

#include "utpl/hls/segment/storage.h"

namespace utpl {
    namespace hls {
        class Uploader {
            UTPL_CLASS_INTERFACE(Uploader) {}

            public:
                static const size_t RETRY_NONE     = 0;
                static const size_t RETRY_INFINITE = -1;

            public:
                using DispatchResultCallback = std::function<void(utpl::Error)>;

                class Capabilities {
                    public:
                        Capabilities(bool mkdir, bool retry) {
                            this->_mkdir = mkdir;
                            this->_retry = retry;
                        }

                        bool canMkdir() const {
                            return this->_mkdir;
                        }

                        bool canRetry() const {
                            return this->_retry;
                        }

                    private:
                        bool _mkdir;
                        bool _retry;
                };

            public:
                virtual void start() = 0;
                virtual void stop() = 0;

                virtual const Capabilities &getCapabilities() const = 0;

                virtual void dispatchUpload(
                    std::shared_ptr<hls::SegmentStorage> storage, 
                    const std::string                   &path, 
                    Timestamp                            timeout, 
                    size_t                               retries, 
                    Timestamp                            retryInterval,
                    DispatchResultCallback               onResultCallback = {}
                ) = 0;

                virtual void dispatchRemoval(
                    const std::string     &path, 
                    Timestamp              timeout, 
                    size_t                 retries, 
                    Timestamp              retryInterval,
                    DispatchResultCallback onResultCallback = {}
                ) = 0;
        };
    }
}

#endif /* __UTPL_HLS_STREAMER_UPLOADER_H__ */