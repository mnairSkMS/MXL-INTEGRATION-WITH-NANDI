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

void transcoder::BaseInputStream::setMxlTap(const MxlTapParameters &parameters, bool keepWhileUnlinked) {
    this->_mxlTapParameters = parameters;
    this->_mxlTapKept       = keepWhileUnlinked;
}

// The sink pad of a new MXL tap for this stream, or null when the stream gets none: the input is not
// configured for it, the stream is neither decoded video nor decoded audio, or its flow is taken.
pipeline::PadPtr transcoder::BaseInputStream::_createMxlTap() {
    if (! this->_mxlTapParameters.isEnabled() || this->_mxlTapAttached) {
        return nullptr;
    }

    // Linked again after an unlink(): the same tap, so the flow goes on.
    if (this->_mxlTap) {
        this->_bin->add(this->_mxlTap->getBin());
        this->_mxlTapAttached = true;

        return std::make_shared<pipeline::Pad>(
            pipeline::GstPadPtr::wrap(gst_element_get_static_pad((*this->_mxlTap->getBin())->get(), "sink"))
        );
    }

    MxlTap::Kind kind;

    if (this->_streamParameters.hasVideoParameters()) {
        kind = MxlTap::Kind::VIDEO;

    } else if (this->_streamParameters.hasAudioParameters()) {
        kind = MxlTap::Kind::AUDIO;

    } else {
        return nullptr;
    }

    this->_mxlTap = MxlTap::create(this->_mxlTapParameters, kind);

    if (! this->_mxlTap) {
        return nullptr;
    }

    this->_bin->add(this->_mxlTap->getBin());
    this->_mxlTapAttached = true;

    return std::make_shared<pipeline::Pad>(
        pipeline::GstPadPtr::wrap(gst_element_get_static_pad((*this->_mxlTap->getBin())->get(), "sink"))
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

    // Their bins were tee branches, so the loop above has already stopped and removed them. A kept
    // tap stays: its side pipeline goes on, and the next link() hangs its branch on the new tee.
    this->_preview        = nullptr;
    this->_mxlTapAttached = false;

    if (! this->_mxlTapKept) {
        this->_mxlTap = nullptr;
    }
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

    // Only decoded media is copied to MXL; a passthrough stream is still compressed.
    pipeline::PadPtr mxlSink;

    if (! isPastrhrough && ! this->_decodedPads.empty()) {
        mxlSink = this->_createMxlTap();
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
        if (list.size() > 1 || previewSink || mxlSink) {
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

        // Linked straight to the tee, like the preview: the tap's own leaky queue is the only
        // thing that can hold a buffer, so it can never hold the on-air branches back.
        if (mxlSink) {
            auto teePad = this->_splitter->requestPad("src_%u");
            auto result = gst_pad_link((*teePad)->get(), (*mxlSink)->get());

            if (result == GST_PAD_LINK_OK) {
                this->_mxlTap->getBin()->syncWithParent();

            } else {
                // A tap must never cost the stream: drop it and carry on without one.
                WARN(("MXL tap disabled for stream {}: link failed ({})", (void *) this, gst_pad_link_get_name(result)));

                this->_splitter->releasePad(teePad);
                this->_bin->remove(this->_mxlTap->getBin());
                this->_mxlTap         = nullptr;
                this->_mxlTapAttached = false;
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
