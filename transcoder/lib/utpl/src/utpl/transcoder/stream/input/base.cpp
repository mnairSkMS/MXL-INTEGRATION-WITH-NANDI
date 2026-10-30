#include "utpl/common/debug.h"

#include "utpl/transcoder/stream/input/base.h"
#include "utpl/pipeline/utils.h"

using namespace utpl;

transcoder::BaseInputStream::BaseInputStream(pipeline::BinPtr bin) {
    this->_bin = bin;
}

transcoder::BaseInputStream::~BaseInputStream() {
    this->unlink();

    if (! this->_decodedPads.empty()) {
        auto pads = this->_decodedPads;

        for (auto pad : pads) {
            this->releasePad(pad);
        }
    }

    if (! this->_passthroughPads.empty()) {
        auto pads = this->_passthroughPads;

        for (auto pad : pads) {
            this->releasePad(pad);
        }
    }
}

pipeline::PadPtr transcoder::BaseInputStream::requestPad(bool passthrough) {
    auto &pads = passthrough ? this->_passthroughPads : this->_decodedPads;

    pads.push_back(this->_bin->addPad("", nullptr, true));

    return *pads.rbegin();   
}

void transcoder::BaseInputStream::releasePad(pipeline::PadPtr pad) {
    std::vector<pipeline::GhostPadPtr> *list = &this->_passthroughPads;

    auto it = std::find_if(list->begin(), list->end(), [&pad](const pipeline::GhostPadPtr &_pad) {
        return (*pad)->get() == (*_pad)->get();
    });

    if (it == list->end()) {
        list = &this->_decodedPads;

        it = std::find_if(list->begin(), list->end(), [&pad](const pipeline::GhostPadPtr &_pad) {
            return (*pad)->get() == (*_pad)->get();
        });
    }

    if (it == list->end()) {
        ERR(("Pad {} is not an element of this stream", pad->getName()));

    } else {
        if ((*it)->getPeer()) {
            WARN(("Pad {} is still linked - unlinking it before removal", pad->getName()));

            (*it)->unlink();
        }

        this->_bin->removePad(std::reinterpret_pointer_cast<pipeline::Pad>(*it));

        list->erase(it);
    }
}

pipeline::PadPtr transcoder::BaseInputStream::peekPad(bool passthrough) {
    if (passthrough) {
        return this->_passthroughPad;

    } else {
        return this->_decodedPad;
    }
}

const transcoder::StreamParameters &transcoder::BaseInputStream::getStreamParameters() const {
    return this->_streamParameters;
}

void transcoder::BaseInputStream::link() {
    if (! this->_decodedPads.empty()) {
        auto pad = this->getSrcPad(false);
        if (pad) {
            this->linkSrcPad(pad, false);
        }
    }

    if (! this->_passthroughPads.empty()) {
        auto pad = this->getSrcPad(true);
        if (pad) {
            this->linkSrcPad(pad, true);
        }
    }
}

pipeline::PadPtr transcoder::BaseInputStream::_createPreview(bool encoded) {
    this->_preview = std::make_unique<PreviewTap>(this, encoded);

    this->_bin->add(this->_preview->getBin());

    return std::make_shared<pipeline::Pad>(
        pipeline::GstPadPtr::wrap(gst_element_get_static_pad((*this->_preview->getBin())->get(), "sink"))
    );
}

void transcoder::BaseInputStream::unlink() {
    for (auto &pad : this->_decodedPads) {
        pad->setTarget(nullptr);
    }

    if (this->_decodedPad && this->_decodedPad->getPeer()) {
        this->_decodedPad->unlink();
    }

    for (auto &pad : this->_passthroughPads) {
        pad->setTarget(nullptr);
    }

    if (this->_passthroughPad && this->_passthroughPad->getPeer()) {
        this->_passthroughPad->unlink();
    }

    if (this->_splitter) {
        this->_splitter->null();

        for (auto &padPtr : this->_splitter->getPads(true)) {
            auto pad = pipeline::Pad(padPtr->getPeer());
            if (pad) {
                pipeline::Element parent(pad.getParent());

                if (parent) {
                    parent.null();

                    this->_bin->remove(
                        this->_bin->get(parent.getName())
                    );
                }
            }
        }

        this->_bin->remove(this->_splitter);
        this->_splitter = nullptr;
    }

    // Its bin was a tee branch, so the loop above has already stopped and removed it.
    this->_preview = nullptr;
}

void transcoder::BaseInputStream::setStreamParameters(const StreamParameters &parameters) {
    this->_streamParameters = parameters;
}

void transcoder::BaseInputStream::linkSrcPad(pipeline::PadPtr pad, bool isPastrhrough) {
    // Subclasses call this directly too (a decoder's pads arrive later), so the preview is
    // attached here rather than in link(). A video stream feeds it from frames the on-air
    // path already decodes when there are any; only a passthrough-only stream makes the
    // preview decode by itself.
    pipeline::PadPtr previewSink;

    if (
        ! this->_preview &&
        this->_streamParameters.hasVideoParameters() &&
        PreviewHub::instance().isEnabled() &&
        isPastrhrough == this->_decodedPads.empty()
    ) {
        previewSink = this->_createPreview(isPastrhrough);
    }

    auto &list = isPastrhrough ? this->_passthroughPads : this->_decodedPads;

    if (isPastrhrough) {
        this->_passthroughPad = pad;

    } else {
        this->_decodedPad = pad;
    }

    if (list.empty()) {
        WARN(("No pads to link!"));
        
    } else {
        if (list.size() > 1 || previewSink) {
            this->_splitter = this->_bin->add(pipeline::Element::make("tee"));

            pad->link(this->_splitter->getPad("sink"));
        }

        // The preview starts with a valve that drops by default, then its own leaky queue,
        // so it is linked straight to the tee and can never hold the on-air branches back.
        if (previewSink) {
            auto teePad = this->_splitter->requestPad("src_%u");
            auto result = gst_pad_link((*teePad)->get(), (*previewSink)->get());

            if (result == GST_PAD_LINK_OK) {
                this->_preview->getBin()->syncWithParent();

            } else {
                // A preview must never cost the stream: drop it and carry on without one.
                GstCaps *caps = gst_pad_query_caps((*pad)->get(), nullptr);
                gchar   *desc = caps ? gst_caps_to_string(caps) : nullptr;

                WARN(("Input preview disabled for stream {}: link failed ({}), source caps {}",
                    (void *) this, gst_pad_link_get_name(result), desc ? desc : "?"));

                g_free(desc);
                if (caps) {
                    gst_caps_unref(caps);
                }

                this->_splitter->releasePad(teePad);
                this->_bin->remove(this->_preview->getBin());
                this->_preview = nullptr;
            }
        }

        for (auto &ghostPad : list) {
            if (this->_splitter) {
                auto queue = pipeline::utils::createQueue(this->_bin, std::string(), 16, 0, 0);
                
                this->_splitter->requestPad("src_%u")->link(
                    queue->getPad("sink")
                );

                ghostPad->setTarget(queue->getPad("src"));

                queue->syncWithParent();

            } else {
                ghostPad->setTarget(pad);
            }
        }

        if (this->_splitter) {
            this->_splitter->syncWithParent();
        }
    }
}

pipeline::BinPtr transcoder::BaseInputStream::getBin() {
    return this->_bin;
}
