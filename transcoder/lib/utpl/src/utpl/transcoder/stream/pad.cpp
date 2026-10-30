#include "utpl/common/types/exception.h"

#include "utpl/transcoder/stream/pad.h"

using namespace utpl;

transcoder::PadStream::PadStream(const StreamParameters &parameters) : _parameters(parameters) {
}

transcoder::PadStream::PadStream(pipeline::PadPtr pad) {
    this->_parameters = StreamParameters::fromCaps(pad->getCaps());
}

const transcoder::StreamParameters &transcoder::PadStream::getStreamParameters() const {
    return this->_parameters;
}

void transcoder::PadStream::link() {
}

void transcoder::PadStream::unlink() {
}