#ifndef __UTPL_PIPELINE_PIPE_H__
#define __UTPL_PIPELINE_PIPE_H__

#include <memory>
#include <functional>

#include "utpl/pipeline/bin.h"

namespace utpl {
    namespace pipeline {
        class Pipe;

        using PipePtr = std::shared_ptr<Pipe>;

        class Pipe : public Bin {
            public:
                using OnEosCallback = std::function<void(Pipe &pipe)>;
                using OnErrorCallback = std::function<void(Pipe &pipe, const std::string &message)>;

            public:
                Pipe(const GstElementPtr &ptr);
                ~Pipe();

                void loop();
                void interrupt();

                static PipePtr create(const std::string &name = "");
                static PipePtr parse(const std::string &desc);

                void onEos(OnEosCallback callback);
                void onError(OnErrorCallback callback);

            private:
                static gboolean _busCallback(GstBus *bus, GstMessage *message, Pipe *self);
                static bool _notifyMessage(const Bin &bin, GstMessage *msg);

            private:
                std::vector<OnEosCallback>   _eosCallbacks;
                std::vector<OnErrorCallback> _errorCallbacks;

                GMainLoop *_pipelineLoop;
                GstBus    *_bus;
        };
    }
}


#endif /* __UTPL_PIPELINE_PIPE_H__ */