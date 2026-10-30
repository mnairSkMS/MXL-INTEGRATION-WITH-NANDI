#ifndef __APP_TRANSCODER_API_GRPC_H__
#define __APP_TRANSCODER_API_GRPC_H__

#include <memory>

#include <grpcpp/grpcpp.h>

#include "application/transcoder/api.h"

namespace app {
    class TranscoderGrpcApi : public TranscoderApi {
        public:
            void start(Transcoder *transcoder) override;
            void stop() override;

        private:
            std::shared_ptr<::grpc::Service> _service;
            std::unique_ptr<::grpc::Server>  _server;
    };
}

#endif /* __APP_TRANSCODER_API_GRPC_H__ */