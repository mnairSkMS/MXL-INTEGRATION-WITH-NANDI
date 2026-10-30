#include <filesystem>

#include "utpl/common/debug.h"

#include "utpl/hls/streamer/uploader/base.h"

using namespace utpl;


class hls::BaseUploader::DispatchHandler {
    public:
        DispatchHandler(const std::string &path, Timestamp timeout, size_t retriesCount, Timestamp retryInterval, DispatchResultCallback callback) : _path(path) {
            this->_timeout       = timeout;
            this->_retriesCount  = retriesCount;
            this->_retryInterval = retryInterval;
            this->_callback      = callback;
        }

        virtual ~DispatchHandler() {
        }

        virtual Error handle(BaseUploader *self) = 0;
        virtual void  cancel(BaseUploader *self) = 0;
        
        virtual void callResultCallback(Error error) {
            if (this->_callback) {
                this->_callback(error);
            }
        }

        virtual size_t getRetriesCount() const {
            return this->_retriesCount;
        }

        virtual Timestamp getRetryInterval() const {
            return this->_retryInterval;
        }

        virtual Timestamp getTimeout() const {
            return this->_timeout;
        }

        virtual const std::string &getPath() const {
            return this->_path;
        }

    private:
        size_t                 _retriesCount;
        Timestamp              _retryInterval;
        Timestamp              _timeout;
        DispatchResultCallback _callback;
        std::string            _path;
};

class hls::BaseUploader::StopDispatchHandler : public hls::BaseUploader::DispatchHandler {
    public:
        StopDispatchHandler() : DispatchHandler(std::string(), 0, 0, 0, {}) {
        }

        Error handle(BaseUploader *self) override {
            return Error::NO_ERROR;
        }
        
        void cancel(BaseUploader *self) override {
        }
};

std::string getPathForIndex(const std::string &str) {
    if (str.empty()) {
        return "/";
    }

    return str;
}

class hls::BaseUploader::UploadDispatchHandler : public hls::BaseUploader::DispatchHandler {
    public:
        UploadDispatchHandler(
            std::set<std::string> &existingDirectories, 
            const std::string &path, 
            bool mkdirs, 
            std::shared_ptr<hls::SegmentStorage> storage, 
            Timestamp timeout, 
            size_t    retries,
            Timestamp retryInterval,
            DispatchResultCallback onResultCallback
        ) : hls::BaseUploader::DispatchHandler(path, timeout, retries, retryInterval, onResultCallback), _existingDirectories(existingDirectories) {
            this->_mkdirs  = mkdirs;
            this->_storage = storage;
        }

        Error handle(BaseUploader *self) override {
            utpl::Error ret = utpl::Error::NO_ERROR;

            if (this->_mkdirs) {
                bool exists, isDir;

                ret = self->doStat("", this->getTimeout(), exists, isDir);
                if (! exists) {
                    ERR(("Could not upload {}, target URI does not exist", this->getPath()));
                    ret = utpl::Error::INVALID_ARGUMENT;
                }
            }
            
            if (ret == utpl::Error::NO_ERROR) {
                auto upload = true;

                std::string parentPathString;


                if (this->_mkdirs) {
                    auto parentPath = std::filesystem::path(this->getPath()).parent_path();
                    
                    parentPathString = parentPath.string();

                    if (! parentPathString.empty()) {
                        parentPathString.push_back('/');
                    }

                    bool exists = this->_existingDirectories.find(getPathForIndex(parentPathString)) != this->_existingDirectories.end();
                    if (! exists) {
                        bool isDir;

                        ret = self->doStat(parentPathString, this->getTimeout(), exists, isDir);
                        if (ret == utpl::Error::NO_ERROR) {
                            if (exists && ! isDir) {
                                if (! isDir) {
                                    ret = self->doRemoval(parentPathString, this->getTimeout());
                                    if (ret == utpl::Error::NO_ERROR) {
                                        exists = false;
                                        isDir  = false;
                                    }
                                }
                            }
                        }

                        if (ret == utpl::Error::NO_ERROR) {
                            if (! exists) {
                                std::vector<std::string> dirsToMake {
                                    parentPathString
                                };

                                do {
                                    parentPath       = parentPath.parent_path();
                                    parentPathString = parentPath.string();

                                    if (! parentPathString.empty()) {
                                        parentPathString.push_back('/');
                                    }

                                    ret = self->doStat(parentPathString, this->getTimeout(), exists, isDir);
                                    if (ret != Error::NO_ERROR) {
                                        break;
                                    }

                                    if (! exists) {
                                        dirsToMake.push_back(parentPathString);

                                    } else {
                                        this->_existingDirectories.insert(getPathForIndex(parentPathString));

                                        while (parentPath.has_parent_path()) {
                                            parentPath       = parentPath.parent_path();
                                            parentPathString = parentPath.string() + "/";

                                            this->_existingDirectories.insert(getPathForIndex(parentPathString));
                                        }

                                        this->_existingDirectories.insert(getPathForIndex(""));

                                        break;
                                    }
                                } while (parentPath.has_parent_path() || ! parentPathString.empty());

                                std::reverse(dirsToMake.begin(), dirsToMake.end());

                                for (auto &p : dirsToMake) {
                                    ret = self->doMkir(p, this->getTimeout());
                                    if (ret == utpl::Error::NO_ERROR) {
                                        this->_existingDirectories.insert(getPathForIndex(p));
                                    }
                                }

                            } else {
                                this->_existingDirectories.insert(getPathForIndex(parentPathString));

                                while (parentPath.has_parent_path()) {
                                    parentPath       = parentPath.parent_path();
                                    parentPathString = parentPath.string() + "/";

                                    this->_existingDirectories.insert(getPathForIndex(parentPathString));
                                }

                                this->_existingDirectories.insert(getPathForIndex(""));
                            }
                        }

                        if (ret != Error::NO_ERROR || ! exists) {
                            upload = false;
                        }
                    }
                }

                if (upload) {
                    ret = self->doUpload(this->getPath(), this->_storage, this->getTimeout());
                    if (ret != Error::NO_ERROR) {
                        if (! parentPathString.empty()) {
                            this->_existingDirectories.erase(parentPathString);
                        }
                    }
                }
            }

            return ret;
        }

        void cancel(BaseUploader *self) override {
        }

    private:
        bool _mkdirs;
        std::shared_ptr<hls::SegmentStorage> _storage;
        std::set<std::string> &_existingDirectories;
};

class hls::BaseUploader::RemovalDispatchHandler : public hls::BaseUploader::DispatchHandler {
    public:
        RemovalDispatchHandler(
            const std::string &path, Timestamp timeout, size_t retries, Timestamp retryInterval, DispatchResultCallback onResultCallback
        ) : hls::BaseUploader::DispatchHandler(path, timeout, retries, retryInterval, onResultCallback) {
        }

        Error handle(BaseUploader *self) override {
            return self->doRemoval(this->getPath(), this->getTimeout());
        }

        void cancel(BaseUploader *self) override {
        }
};


hls::BaseUploader::RetryContext::RetryContext(std::shared_ptr<DispatchHandler> handler) : handler(handler) {
    this->retryTimestamp = utpl::Time::getMonotonicTimestamp() + this->handler->getRetryInterval();
    this->retryCounter   = 0;
    this->lastError      = Error::NO_ERROR;
}

bool hls::BaseUploader::RetryContext::hasRemainingAttempts() const {
    auto retryCount = this->handler->getRetriesCount();

    return  retryCount > 0 && this->retryCounter < (retryCount - 1);
}

bool hls::BaseUploader::RetryContext::isRetryAllowed() const {
    if (utpl::Time::getMonotonicTimestamp() >= this->retryTimestamp) {
        return true;
    }

    return false;
}

void hls::BaseUploader::RetryContext::scheduleNextRetry() {
    if (this->hasRemainingAttempts()) {
        this->retryCounter++;

        this->retryTimestamp = utpl::Time::getMonotonicTimestamp() + this->handler->getRetryInterval();
    }
}

void hls::BaseUploader::RetryContext::cancel(BaseUploader *self) {
    this->retryCounter = this->handler->getRetriesCount() - 1;

    this->handler->cancel(self);
}


hls::BaseUploader::BaseUploader(const Capabilities &caps) : _caps(caps)  {
}

hls::BaseUploader::~BaseUploader() {
    this->stop();
}

void hls::BaseUploader::start() {
    if (! this->_thread) {
        this->_thread = std::make_unique<std::thread>(&BaseUploader::_threadRoutine, this);
    }
}

void hls::BaseUploader::stop() {
    if (this->_thread) {
        {
            std::scoped_lock lock(this->_mutex);

            for (auto &m : this->_queue) {
                m->cancel(this);
            }

            this->_queue.clear();
            this->_queue.push_back(std::make_unique<StopDispatchHandler>());
        
            this->_cond.notify_all();
        }

        this->_thread->join();

        this->_thread.reset();
    }
}

const hls::BaseUploader::Capabilities &hls::BaseUploader::getCapabilities() const {
    return this->_caps;
}

void hls::BaseUploader::_dispatch(std::unique_ptr<DispatchHandler> handler) {
    this->_queue.push_back(std::move(handler));

    this->_cond.notify_all();
}

void hls::BaseUploader::dispatchUpload(
    std::shared_ptr<hls::SegmentStorage> storage, 
    const std::string                   &path, 
    Timestamp                            timeout, 
    size_t                               retries, 
    Timestamp                            retryInterval,
    DispatchResultCallback               onResultCallback
) {
    std::scoped_lock lock(this->_mutex);

    for (auto &el : this->_retryQueue) {
        if (el.handler->getPath() == path) {
            el.cancel(this);
        }
    }

    this->_dispatch(
        std::make_unique<UploadDispatchHandler>(this->_existingDirectories, path, this->getCapabilities().canMkdir(), storage, timeout, retries, retryInterval, onResultCallback)
    );
}

void hls::BaseUploader::dispatchRemoval(const std::string &path, Timestamp timeout, size_t retries, Timestamp retryInterval, DispatchResultCallback onResultCallback) {
    std::scoped_lock lock(this->_mutex);

    for (auto &el : this->_retryQueue) {
        if (el.handler->getPath() == path) {
            el.cancel(this);
        }
    }

    this->_dispatch(
        std::make_unique<RemovalDispatchHandler>(path, timeout, retries, retryInterval, onResultCallback)
    );
}

void hls::BaseUploader::_threadRoutine() {
    TRACE(("STARTED"));

    utpl::Timestamp waitMilliseconds = -1;

    do {
        std::unique_lock<std::mutex> lock(this->_mutex);

        auto currentTime = utpl::Time::getMonotonicTimestamp();

        {
            utpl::Timestamp maxWait  = std::numeric_limits<utpl::Timestamp>::max();
            utpl::Timestamp waitTime = maxWait;

            if (! this->_queue.empty()) {
                waitTime = 0;

            } else {
                if (! this->_retryQueue.empty()) {
                    for (auto &r : this->_retryQueue) {
                        waitTime = std::min(waitTime, r.retryTimestamp - currentTime);
                    }

                    if (waitTime < 0) {
                        waitTime = 0;
                    }
                }
            }

            if (waitTime == maxWait) {
                this->_cond.wait(lock);

            } else if (waitTime != 0) {
                this->_cond.wait_for(lock, std::chrono::milliseconds(waitTime));
            }
        }

        if (! this->_queue.empty()) {
            auto handler = std::move(this->_queue.front());

            this->_queue.pop_front();

            lock.unlock();

            if (dynamic_cast<StopDispatchHandler *>(handler.get())) {
                break;
            }

            auto callRet = handler->handle(this);
            if (callRet == Error::NO_ERROR) {
                handler->callResultCallback(callRet);

            } else {
                if (this->getCapabilities().canRetry()) {
                    auto retry = RetryContext(std::move(handler));

                    retry.lastError = callRet;

                    this->_retryQueue.push_back(retry);
                }
            }

        } else if (! this->_retryQueue.empty()) {
            // Remove expired
            auto it = this->_retryQueue.begin();
            while (it != this->_retryQueue.end()) {
                if (! it->hasRemainingAttempts()) {
                    it->handler->callResultCallback(it->lastError);

                    it = this->_retryQueue.erase(it);

                } else {
                    ++it;
                }
            }

            // Retry
            for (auto it = this->_retryQueue.begin(); it != this->_retryQueue.end(); it++) {
                if (it->isRetryAllowed()) {
                    DBG(("Retrying to upload {}, {}/{}", it->handler->getPath(), it->retryCounter + 1, it->handler->getRetriesCount()));

                    lock.unlock();
                    {
                        it->lastError = it->handler->handle(this);
                    }
                    lock.lock();

                    if (it->lastError == Error::NO_ERROR) {
                        it->handler->callResultCallback(it->lastError);

                        this->_retryQueue.erase(it);

                    } else {
                        if (it->hasRemainingAttempts()) {
                            it->scheduleNextRetry();

                        } else {
                            it->handler->callResultCallback(it->lastError);

                            this->_retryQueue.erase(it);
                        }
                    }

                    break;
                }
            }
        }
    } while (true);

    if (! this->_queue.empty()) {
        TRACE(("Still scheduled for upload:"));
        for (auto &r : this->_queue) {
            TRACE(("   {}", r->getPath()));
        }
    }

    if (! this->_retryQueue.empty()) {
        TRACE(("Still scheduled for retry:"));
        for (auto &r : this->_retryQueue) {
            TRACE(("   {}", r.handler->getPath()));
        }
    }

    TRACE(("Base uploader task has finished"));
}

void hls::BaseUploader::doCancel(DispatchResultCallback onResultCallback) {
    DBG(("CALL"));

    if (onResultCallback) {
        onResultCallback(utpl::Error::CANCELLED);
    }
}
