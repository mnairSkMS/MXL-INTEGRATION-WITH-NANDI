#include "application/transcoder.h"

#include "utpl/common/types/exception.h"
#include "utpl/common/debug.h"


using namespace utpl;


app::Transcoder::Transcoder() {

}

void app::Transcoder::configure(const utpl::transcoder::EngineParameters &parameters) {
    this->_engine.prepare(parameters);
}

void app::Transcoder::addEventProvider(utpl::transcoder::EventProvider *eventProvider) {
    this->_engine.addEventProvider(eventProvider);
}

void app::Transcoder::removeEventProvider(utpl::transcoder::EventProvider *eventProvider) {
    this->_engine.removeEventProvider(eventProvider);
}

void app::Transcoder::start(StopCallback callback) {
    if (! this->_worker) {
        this->_stopCallback = callback;

        this->_worker = std::make_unique<std::thread>(&Transcoder::_workerRoutine, this);
        if (! this->_worker) {
            throw NoFreeMemoryException("Unable to create new transcoder thread!");
        }
    }
}

void app::Transcoder::stop() {
    this->_stopCallback = {};

    if (this->_worker) {
        try {
            this->_engine.stop([this]() {
                this->_engine.interrupt();
            });
        } catch (const utpl::Exception &ex) {
            ERR(("Transcoder thread caught an exception: {}", ex.getMessage()));
            this->_engine.interrupt();

        } catch (...) {
            ERR(("Transcoder thread caught unknown exception"));
            this->_engine.interrupt();
        }

        this->_worker->join();
        this->_worker.reset();
    }
}

utpl::transcoder::Engine &app::Transcoder::getEngine() {
    return this->_engine;
}

void app::Transcoder::_workerRoutine() {
    Error ret = Error::NO_ERROR;

    LOG(("Transcoder thread is starting"));

    try {
        this->_engine.start([]() {
            LOG(("Transcoder engine has started"));
        });

        try {
            ret = this->_engine.loop();
        } catch (...) {
            this->_engine.stop([]() {
                LOG(("Transcoder engine has stopped"));
            });

            throw;
        }

    } catch (const utpl::Exception &ex) {
        ERR(("Transcoder thread caught an exception: {}", ex.getMessage()));
        ret = ex.getCode();

    } catch (...) {
        ERR(("Transcoder thread caught unknown exception"));
        ret = Error::UNKNOWN_ERROR;
    }

    LOG(("Transcoder thread has stopped"));

    if (this->_stopCallback) {
        this->_stopCallback(ret);
    }
}
