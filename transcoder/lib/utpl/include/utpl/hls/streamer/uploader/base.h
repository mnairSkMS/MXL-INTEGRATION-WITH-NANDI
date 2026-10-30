#ifndef __UTPL_HLS_STREAMER_UPLOADER_BASE__
#define __UTPL_HLS_STREAMER_UPLOADER_BASE__

#include <condition_variable>
#include <mutex>
#include <thread>
#include <list>

#include "utpl/hls/streamer/uploader.h"

namespace utpl {
    namespace hls {
        class BaseUploader : public Uploader {
            public:
                BaseUploader(const Capabilities &caps);
                ~BaseUploader();

                void start() override;
                void stop() override;

                const Capabilities &getCapabilities() const override;

                void dispatchUpload(
                    std::shared_ptr<hls::SegmentStorage> storage, 
                    const std::string                   &path, 
                    Timestamp                            timeout, 
                    size_t                               retries, 
                    Timestamp                            retryInterval,
                    DispatchResultCallback               onResultCallback = {}
                ) override;

                void dispatchRemoval(
                    const std::string     &path, 
                    Timestamp              timeout, 
                    size_t                 retries, 
                    Timestamp              retryInterval,
                    DispatchResultCallback onResultCallback = {}
                ) override;

            private:
                class DispatchHandler;

                struct RetryContext {
                    size_t                           retryCounter;
                    utpl::Timestamp                  retryTimestamp;
                    std::shared_ptr<DispatchHandler> handler;
                    Error                            lastError;

                    RetryContext(std::shared_ptr<DispatchHandler> handler);

                    bool hasRemainingAttempts() const;
                    bool isRetryAllowed() const;
                    void scheduleNextRetry();
                    void cancel(BaseUploader *self);
                };

                void _threadRoutine();

                void _dispatch(std::unique_ptr<DispatchHandler> handler);

            private:
                class UploadDispatchHandler;
                class RemovalDispatchHandler;
                class MkdirDispatchHandler;
                class StopDispatchHandler;

            protected:
                virtual utpl::Error doUpload(const std::string &path, std::shared_ptr<hls::SegmentStorage> storage, Timestamp timeout) = 0;
                virtual utpl::Error doRemoval(const std::string &path, Timestamp timeout) = 0;
                virtual utpl::Error doMkir(const std::string &path, Timestamp timeout) = 0;
                virtual utpl::Error doStat(const std::string &path, Timestamp timeout, bool &exist, bool &isDirectory) = 0;

                virtual void doCancel(DispatchResultCallback onResultCallback);

            private:
                std::mutex _mutex;
                std::condition_variable _cond;

                std::list<std::unique_ptr<DispatchHandler>> _queue;
                std::list<RetryContext> _retryQueue;

                std::unique_ptr<std::thread> _thread;
                Capabilities _caps;

                std::set<std::string> _existingDirectories;
        };
    }
}

#endif /* __UTPL_HLS_STREAMER_UPLOADER_BASE__ */