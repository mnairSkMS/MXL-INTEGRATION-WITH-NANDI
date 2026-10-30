#ifndef __APPLICATION_TRANSCODER_H__
#define __APPLICATION_TRANSCODER_H__

#include <functional>
#include <thread>

#include "utpl/common/types.h"
#include "utpl/common/time.h"

#include "utpl/transcoder/engine.h"

namespace app {
    class Transcoder {
        public:
            using StopCallback = std::function<void(utpl::Error)>;

        public:
            Transcoder();

            void configure(const utpl::transcoder::EngineParameters &parameters);

            void addEventProvider(utpl::transcoder::EventProvider *eventProvider);
            void removeEventProvider(utpl::transcoder::EventProvider *eventProvider);
      
            void start(StopCallback callback = {});
            void stop();

            utpl::transcoder::Engine &getEngine();

        private:
            void _workerRoutine();

        private:
            utpl::transcoder::Engine     _engine;
            StopCallback                 _stopCallback;
            std::unique_ptr<std::thread> _worker;
    };
}

#endif /* __APPLICATION_TRANSCODER_H__ */