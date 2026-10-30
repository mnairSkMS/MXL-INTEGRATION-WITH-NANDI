
#include "utpl/common/debug.h"

#include "utpl/pipeline/utils.h"

#include "passthrough.h"

using namespace utpl;


transcoder::PasstroughConnector::PasstroughConnector(
    const std::string &name, std::shared_ptr<InputStream> inputStream, const StreamParameters &outputParameters
) : _outputParameters(outputParameters) {
    this->_inputStream = inputStream;
    this->_bin         = pipeline::Bin::create(name);
}

std::shared_ptr<transcoder::InputStream> transcoder::PasstroughConnector::getStream() {
    return this->_inputStream;
}

pipeline::PadPtr transcoder::PasstroughConnector::peekPad() {
    if (this->_identity) {
        return this->_identity->getPad("src");
    }

    return nullptr;
}

pipeline::PadPtr transcoder::PasstroughConnector::getPad(const StreamParameters &parameters) {
    this->_pads.push_back(
        this->_bin->addPad(std::string(), nullptr, true)
    );

    return *this->_pads.rbegin();
}

const transcoder::StreamParameters &transcoder::PasstroughConnector::getStreamParameters() {
    return this->_outputParameters;
}

void transcoder::PasstroughConnector::prepare(pipeline::BinPtr &bin) {
    if (! this->_inputPad) {
        this->_identity = pipeline::utils::createIdentity(this->_bin);

        bin->add(this->_bin);

        this->_inputPad = bin->addPad(
            this->_bin->getName(), this->_identity->getPad("sink")
        );
    }
}

void transcoder::PasstroughConnector::link() {
    auto pad = this->_identity->getPad("src");

    if (this->_pads.size() > 1) {
        this->_splitter = this->_bin->add(pipeline::Element::make("tee"));

        pad->link(this->_splitter->getPad("sink"));
    }

    for (auto &ghostPad : this->_pads) {
        if (this->_splitter) {
            auto queue = pipeline::utils::createQueue(this->_bin, std::string(), 16, 0, 0);

            this->_splitter->requestPad("src_%u")->link(
                queue->getPad("sink")
            );

            ghostPad->setTarget(
                queue->getPad("src")
            );

        } else {
            ghostPad->setTarget(pad);
        }
    }

    if (this->_inputPad) {
        if (! this->_inputPad->getPeer()) {
            this->_inputStream->requestPad(true)->link(
                this->_inputPad
            );
        }
    }
    
}

void transcoder::PasstroughConnector::unlink() {
    if (this->_inputPad) {
        auto peerPadPtr = this->_inputPad->getPeer();

        if (peerPadPtr) {
            auto peerPad = std::make_shared<pipeline::Pad>(peerPadPtr);

            peerPad->unlink();

            this->_inputStream->releasePad(peerPad);
        }
    }
}

void transcoder::PasstroughConnector::cleanup() {
    if (this->_inputPad) {
        this->unlink();

        this->_inputPad = nullptr;
    }
}

pipeline::ElementPtr transcoder::PasstroughConnector::getNode() {
    return this->_bin;
}