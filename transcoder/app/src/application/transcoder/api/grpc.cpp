#include "utpl/common/debug.h"

#include "application/transcoder/api/grpc.h"

#include "proto/transcoder.grpc.pb.h"

#include <algorithm>

#include "utpl/transcoder/preview.h"


class TranscoderServiceImpl : public app::grpc::TranscoderApi::Service {
    public:
        TranscoderServiceImpl(app::Transcoder *transcoder) : _transcoder(transcoder) {
        }

        virtual ::grpc::Status getInputs(::grpc::ServerContext* context, const ::google::protobuf::Empty* request, ::app::grpc::InputList* response) {
            DBG(("CALL"));

            for (const auto input : this->_transcoder->getEngine().getChannel().getInputs()) {
                _serializeInputStatus(response->add_inputs(), input);
            }

            return ::grpc::Status::OK;
        }

        virtual ::grpc::Status getAdBreakState(::grpc::ServerContext* context, const ::google::protobuf::Empty* request, ::app::grpc::AdBreakState* response) {
            DBG(("CALL"));

            response->set_onair(this->_transcoder->getEngine().isAdBreakOnAir());

            return ::grpc::Status::OK;
        }

        virtual ::grpc::Status setInputPreview(::grpc::ServerContext* context, const ::app::grpc::SetInputPreviewRequest* request, ::app::grpc::InputPreviewState* response) {
            auto &hub = utpl::transcoder::PreviewHub::instance();

            if (! hub.isEnabled()) {
                return ::grpc::Status(::grpc::StatusCode::UNAVAILABLE, "Input preview is disabled (UTPL_PREVIEW=off)");
            }

            auto input = this->_transcoder->getEngine().getChannel().getInput(request->inputid());

            if (! input) {
                return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "Requested ID does not match any Input");
            }

            utpl::transcoder::PreviewMode mode = utpl::transcoder::PreviewMode::OFF;

            if (request->mode() == ::app::grpc::PREVIEW_STILL) {
                mode = utpl::transcoder::PreviewMode::STILL;

            } else if (request->mode() == ::app::grpc::PREVIEW_LIVE) {
                mode = utpl::transcoder::PreviewMode::LIVE;
            }

            std::vector<utpl::transcoder::InputStream *> videoStreams;

            for (const auto &stream : input->getStreams()) {
                if (stream->getStreamParameters().hasVideoParameters()) {
                    videoStreams.push_back(stream.get());
                }
            }

            // Bounded so a client that forgets to stop cannot keep a decoder running for long.
            auto ttl = std::chrono::milliseconds(std::clamp<uint32_t>(request->ttlms() ? request->ttlms() : 30000, 1000, 120000));

            response->set_taps(hub.demand(input->getId(), mode, ttl, videoStreams));
            response->set_framepath(hub.framePath(input->getId()));

            return ::grpc::Status::OK;
        }

        virtual ::grpc::Status getInput(::grpc::ServerContext* context, const ::app::grpc::GetInputParameters* request, ::app::grpc::Input* response) {
            auto ret = ::grpc::Status::OK;

            {
                auto input = this->_transcoder->getEngine().getChannel().getInput(request->id());

                DBG(("CALL"));

                if (! input) {
                    ret = ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "Requested ID does not match any Input");

                } else {
                    _serializeInputStatus(response, input);
                }
            }

            return ret;
        }

        virtual ::grpc::Status setInputStream(::grpc::ServerContext* context, const ::app::grpc::SetInputStreamParameters* request, ::app::grpc::Input* response) {
            auto ret = ::grpc::Status::OK;

            DBG(("CALL"));

            {
                std::shared_ptr<utpl::transcoder::Input> input = this->_transcoder->getEngine().getChannel().getInput(request->inputid());
                
                if (! input) {
                    ret = ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "Requested ID does not match any Input");

                } else {
                    switch (input->setCurrentStream(request->streamid())) {
                        case utpl::Error::NO_ERROR:
                            _serializeInputStatus(response, input);
                            break;

                        case utpl::Error::INVALID_ARGUMENT:
                            ret = ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "Invalid stream provided or the stream (" + request->streamid() + ") does not exist in the requested input (" + request->inputid() + ")");
                            break;

                        case utpl::Error::INVALID_STATE:
                            ret = ::grpc::Status(::grpc::StatusCode::FAILED_PRECONDITION, "Requested stream is in a state that does not allow switching to it");
                            break;

                        default:
                            ret = ::grpc::Status(::grpc::StatusCode::UNKNOWN, "Unknown error has occurred");
                            break;
                    }
                }
            }

            return ret;
        }

        virtual ::grpc::Status applyPlaylistSnapshot(
            ::grpc::ServerContext* context,
            const ::app::grpc::ApplyPlaylistSnapshotRequest* request,
            ::app::grpc::ApplyPlaylistSnapshotResponse* response
        ) {
            auto input = this->_transcoder->getEngine().getChannel().getInput(request->inputid());
            if (! input) {
                return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "Requested ID does not match any Input");
            }

            std::vector<utpl::transcoder::PlaylistSnapshotItem> items;
            items.reserve(request->items_size());
            for (const auto &item : request->items()) {
                items.push_back({ item.id(), item.uri(), item.attributes() });
            }

            switch (input->applyPlaylistSnapshot(items, request->revision())) {
                case utpl::Error::NO_ERROR:
                    response->set_revision(request->revision());
                    this->_serializeInputStatus(response->mutable_input(), input);
                    return ::grpc::Status::OK;

                case utpl::Error::INVALID_ARGUMENT:
                    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "Invalid managed playlist snapshot");

                case utpl::Error::INVALID_STATE:
                    return ::grpc::Status(::grpc::StatusCode::FAILED_PRECONDITION, "Managed playlist snapshot conflicts with the current on-air state");

                case utpl::Error::NOT_SUPPORTED:
                    return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Input is not a playlist input");

                default:
                    return ::grpc::Status(::grpc::StatusCode::UNKNOWN, "Unknown error has occurred");
            }
        }

    private:
        void _serializeInputStatus(app::grpc::Input *out, std::shared_ptr<utpl::transcoder::Input> in) {
            out->set_id(in->getId());

            {
                auto inputStatus = in->getStatus();

                for (auto &transportStatus : inputStatus.getTransportsStatus()) {
                    auto *resStream = out->add_streams();

                    resStream->set_uri(transportStatus.getUri());
                    resStream->set_id(transportStatus.getId());

                    switch (transportStatus.getState()) {
                        case utpl::transcoder::InputState::CREATED:
                            resStream->set_state(app::grpc::StreamState::CREATED);
                            break;
                            
                        case utpl::transcoder::InputState::BUFFERED:
                            resStream->set_state(app::grpc::StreamState::BUFFERED);
                            break;

                        case utpl::transcoder::InputState::BUFFERING:
                            resStream->set_state(app::grpc::StreamState::BUFFERING);
                            break;

                        default:
                            resStream->set_state(app::grpc::StreamState::UNKNOWN);
                            break;
                    }
                    
                }

                out->set_currentstreamid(inputStatus.getCurrentTransportId());
            }
        }

    private:
        app::Transcoder *_transcoder;
};


void app::TranscoderGrpcApi::start(Transcoder *transcoder) {
    std::string serverAddress("unix:///tmp/transcoder.grpc.socket");

    ::grpc::EnableDefaultHealthCheckService(true);

    this->_service = std::make_shared<TranscoderServiceImpl>(transcoder);

    {
        ::grpc::ServerBuilder builder;

        builder.AddListeningPort(serverAddress, ::grpc::InsecureServerCredentials());
        builder.RegisterService(this->_service.get());

        this->_server = builder.BuildAndStart();
    }
}

void app::TranscoderGrpcApi::stop() {
    if (this->_server) {
        this->_server->Shutdown();
        this->_server->Wait();

        this->_server.reset();
    }
}
