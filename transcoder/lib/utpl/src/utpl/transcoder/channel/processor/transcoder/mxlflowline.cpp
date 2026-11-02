#include "utpl/common/debug.h"

#include "utpl/pipeline/utils.h"

#include "mxlflowline.h"

using namespace utpl;

transcoder::MxlFlowline::MxlFlowline(const std::string &name, const transcoder::StreamParameters &outParameters) : _name(name), _outputParameters(outParameters) {
}

void transcoder::MxlFlowline::addOutput(transcoder::OutputStream *stream) {
    this->_outputs.push_back(stream);
}

bool transcoder::MxlFlowline::compatible(const transcoder::StreamParameters &) {
    // One flow per stream: a second MXL output asking for the same media gets a flowline of its own.
    return false;
}

void transcoder::MxlFlowline::prepare(const transcoder::StreamParameters &, pipeline::BinPtr &bin) {
    this->_identity = pipeline::utils::createIdentity(bin, this->_name + "-raw");

    // A pad on the channel's bin, as the encoding flowline has: the output is a bin of its own.
    this->_src = bin->addPad(this->_name + "-src", this->_identity->getPad("src"));
}

pipeline::PadPtr transcoder::MxlFlowline::getInputPad() {
    return this->_identity->getPad("sink");
}

const transcoder::StreamParameters &transcoder::MxlFlowline::getParameters() const {
    return this->_outputParameters;
}

void transcoder::MxlFlowline::link() {
    DBG(("Linking MXL flowline '{}' to {} output stream(s)", this->_name, this->_outputs.size()));

    for (auto *output : this->_outputs) {
        this->_src->link(output->getPad());
    }
}
