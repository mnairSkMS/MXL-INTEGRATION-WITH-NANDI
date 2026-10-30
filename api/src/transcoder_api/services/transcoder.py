import os
from typing import List

from ..model.rest_api import (
    InputList as RESTInputList,
    Input as RESTInput,
    InputStream as RESTInputStream,
    InputStreamState as RESTInputStreamState,
    InputStreamList as RESTInputStreamList,
    InputUpdateParameters as RESTUpdateParameters,
    ID as RESTId,
    ErrorCode as RESTErrorCode,
)

from ..exceptions import TranscoderApiException

import grpc

import transcoder_pb2
import transcoder_pb2_grpc


# Every call below is issued with this deadline. Without one, a wedged engine blocks
# the caller forever - including the reconciler thread, which would then stop applying
# state while the API carried on answering normally.
GRPC_TIMEOUT_SECONDS = float(os.environ.get("GRPC_TIMEOUT_SECONDS", "5"))


def _getChannel() -> grpc.Channel:
    return grpc.insecure_channel("unix:///tmp/transcoder.grpc.socket")


def _mapRpcInputToRestInput(rpcInput):
    restInput = RESTInput()

    restInput.id = RESTId(rpcInput.id)

    if rpcInput.currentStreamId:
        restInput.currentStreamId = RESTId(rpcInput.currentStreamId)

    restStreams = list()

    for stream in rpcInput.streams:
        restStream = RESTInputStream()

        restStream.id = RESTId(stream.id)
        restStream.uri = stream.uri

        match stream.state:
            case transcoder_pb2.StreamState.CREATED:
                restStream.state = RESTInputStreamState.CREATED
            case transcoder_pb2.StreamState.BUFFERED:
                restStream.state = RESTInputStreamState.BUFFERED
            case transcoder_pb2.StreamState.BUFFERING:
                restStream.state = RESTInputStreamState.BUFFERING
            case transcoder_pb2.StreamState.UNKNOWN:
                restStream.state = RESTInputStreamState.UNKNOWN

        restStreams.append(restStream)

    restInput.streams = RESTInputStreamList(restStreams)

    return restInput


def getInputs() -> RESTInputList:
    ret = list()

    with _getChannel() as channel:
        stub = transcoder_pb2_grpc.TranscoderApiStub(channel)

        try:
            response = stub.getInputs(
                transcoder_pb2_grpc.google_dot_protobuf_dot_empty__pb2.Empty(),
                timeout=GRPC_TIMEOUT_SECONDS,
            )

            for input in response.inputs:
                ret.append(_mapRpcInputToRestInput(input))

        except grpc.RpcError as err:
            raise TranscoderApiException(
                message=str(err.code()) + ": " + err.details(), code=RESTErrorCode.UNAVAILABLE
            )

    return RESTInputList(ret)


def getInput(id: RESTId):
    ret = RESTInput()

    with _getChannel() as channel:
        stub = transcoder_pb2_grpc.TranscoderApiStub(channel)

        try:
            getInputParameters = transcoder_pb2.GetInputParameters()

            getInputParameters.id = str(id)

            response = stub.getInput(getInputParameters, timeout=GRPC_TIMEOUT_SECONDS)

            ret = _mapRpcInputToRestInput(response)
        except grpc.RpcError as err:
            match err.code():
                case grpc.StatusCode.INVALID_ARGUMENT:
                    raise TranscoderApiException(
                        message="grpc." + str(err.code()) + ": " + err.details(),
                        code=RESTErrorCode.INVALID_REQUEST,
                        http_code=404,
                    )

                case _:
                    raise TranscoderApiException(
                        message="grpc." + str(err.code()) + ": " + err.details(),
                        code=RESTErrorCode.UNAVAILABLE,
                    )

    return ret


def updateInput(id: RESTId, updateParams: RESTUpdateParameters):
    ret = RESTInput()

    with _getChannel() as channel:
        stub = transcoder_pb2_grpc.TranscoderApiStub(channel)

        try:
            if updateParams.currentStreamId:
                setInputParameters = transcoder_pb2.SetInputStreamParameters()

                setInputParameters.inputId = str(id)
                setInputParameters.streamId = str(updateParams.currentStreamId.root)

                response = stub.setInputStream(setInputParameters, timeout=GRPC_TIMEOUT_SECONDS)

                ret = _mapRpcInputToRestInput(response)

            else:
                raise TranscoderApiException(
                    message="Stream ID to be set cannot be null",
                    code=RESTErrorCode.INVALID_REQUEST,
                    http_code=400,
                )

        except grpc.RpcError as err:
            match err.code():
                case grpc.StatusCode.INVALID_ARGUMENT:
                    raise TranscoderApiException(
                        message="grpc." + str(err.code()) + ": " + err.details(),
                        code=RESTErrorCode.INVALID_REQUEST,
                        http_code=404,
                    )

                case grpc.StatusCode.FAILED_PRECONDITION:
                    raise TranscoderApiException(
                        message="grpc." + str(err.code()) + ": " + err.details(),
                        code=RESTErrorCode.INVALID_REQUEST,
                        http_code=400,
                    )

                case _:
                    raise TranscoderApiException(
                        message="grpc." + str(err.code()) + ": " + err.details(),
                        code=RESTErrorCode.UNAVAILABLE,
                    )

    return ret


def applyPlaylistSnapshot(input_id: str, playlist_id: str, revision: int, items: list[dict]):
    """Apply an already-persisted desired queue to a PlaylistInput.

    The engine operation is idempotent: callers can retry after a process/API failure.
    """
    with _getChannel() as channel:
        stub = transcoder_pb2_grpc.TranscoderApiStub(channel)
        request = transcoder_pb2.ApplyPlaylistSnapshotRequest(
            inputId=input_id, playlistId=playlist_id, revision=revision
        )
        for item in items:
            request.items.add(
                id=item["id"], uri=item["uri"], attributes=item.get("attributes", "")
            )
        try:
            response = stub.applyPlaylistSnapshot(request, timeout=GRPC_TIMEOUT_SECONDS)
        except grpc.RpcError as err:
            if err.code() == grpc.StatusCode.INVALID_ARGUMENT:
                raise TranscoderApiException(
                    message="grpc." + str(err.code()) + ": " + err.details(),
                    code=RESTErrorCode.INVALID_REQUEST,
                    http_code=400,
                )
            if err.code() == grpc.StatusCode.FAILED_PRECONDITION:
                raise TranscoderApiException(
                    message="grpc." + str(err.code()) + ": " + err.details(),
                    code=RESTErrorCode.INVALID_REQUEST,
                    http_code=409,
                )
            raise TranscoderApiException(
                message="grpc." + str(err.code()) + ": " + err.details(),
                code=RESTErrorCode.UNAVAILABLE,
            )
    return response


def switchInputStream(input_id: str, stream_id: str):
    """Switch a live input using the existing public gRPC operation."""
    return updateInput(
        # updateInput accepts the UUID value used by the generated public
        # router. Passing the RootModel itself stringifies as
        # ``root=UUID('...')`` and the engine cannot match that input ID.
        RESTId.model_validate(input_id).root,
        RESTUpdateParameters(currentStreamId=RESTId.model_validate(stream_id)),
    )


def getAdBreakState() -> dict:
    """Whether a locally inserted ad break is on air right now.

    The engine is the only authority for this. A break ends on a cue-in or on the
    configured maxBreakDuration, and a client that sent the cue-out cannot see the
    second of those happen - so an operator UI that tracked only its own clicks
    would keep showing a break that finished a minute ago.
    """
    with _getChannel() as channel:
        stub = transcoder_pb2_grpc.TranscoderApiStub(channel)

        try:
            response = stub.getAdBreakState(
                transcoder_pb2_grpc.google_dot_protobuf_dot_empty__pb2.Empty()
            )

        except grpc.RpcError as err:
            raise TranscoderApiException(
                message="grpc." + str(err.code()) + ": " + err.details(),
                code=RESTErrorCode.UNAVAILABLE,
            )

    return {"onAir": response.onAir}


PREVIEW_MODES = {
    "off": transcoder_pb2.PREVIEW_OFF,
    "still": transcoder_pb2.PREVIEW_STILL,
    "live": transcoder_pb2.PREVIEW_LIVE,
}


def setInputPreview(id, mode: str, ttlMs: int) -> dict:
    """Renew preview demand for one input; the engine stops by itself ttlMs later.

    Returns where the engine writes the JPEG and how many of the input's video streams
    carry a preview tap (0 while the input has no linked video yet).
    """
    with _getChannel() as channel:
        stub = transcoder_pb2_grpc.TranscoderApiStub(channel)

        try:
            response = stub.setInputPreview(
                transcoder_pb2.SetInputPreviewRequest(inputId=str(id), mode=PREVIEW_MODES[mode], ttlMs=ttlMs),
                timeout=GRPC_TIMEOUT_SECONDS,
            )
        except grpc.RpcError as err:
            raise TranscoderApiException(
                message="grpc." + str(err.code()) + ": " + err.details(),
                code=RESTErrorCode.INVALID_REQUEST if err.code() == grpc.StatusCode.INVALID_ARGUMENT else RESTErrorCode.UNAVAILABLE,
                http_code=404 if err.code() == grpc.StatusCode.INVALID_ARGUMENT else 503,
            )

    return {"framePath": response.framePath, "taps": response.taps}
