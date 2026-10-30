from .model.rest_api import ErrorCode


class TranscoderApiException(Exception):
    def __init__(self, message: str, code: ErrorCode, http_code: int = 500):
        self.message = message
        self.code = code
        self.http_code = http_code


class InvalidRequestException(TranscoderApiException):
    def __init__(
        self,
        message: str,
        code: ErrorCode = ErrorCode.INVALID_REQUEST,
    ):
        super().__init__(message, code, http_code=400)


class NotFoundException(TranscoderApiException):
    def __init__(self, message: str):
        super().__init__(message, ErrorCode.INVALID_REQUEST, http_code=404)


class ConflictException(TranscoderApiException):
    def __init__(self, message: str):
        super().__init__(message, ErrorCode.INVALID_REQUEST, http_code=409)


class UnprocessableException(TranscoderApiException):
    def __init__(self, message: str):
        super().__init__(message, ErrorCode.INVALID_REQUEST, http_code=422)


class GoneException(TranscoderApiException):
    """A route that used to exist and has a named replacement.

    ErrorCode is generated from api.yaml and has no GONE member; adding one would mean
    regenerating rest_api.py, so the 410 lives in the status code and the replacement route
    lives in the message.
    """

    def __init__(self, message: str):
        super().__init__(message, ErrorCode.INVALID_REQUEST, http_code=410)
