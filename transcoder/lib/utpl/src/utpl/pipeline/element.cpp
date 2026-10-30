#include "utpl/common/types/exception.h"
#include "utpl/common/debug.h"

#include "utpl/pipeline/element.h"

using namespace utpl;


void pipeline::Element::_onPadAdded(GstElement *element, GstPad *padPtr, Element *self) {
    auto pad = std::make_shared<Pad>(GstPadPtr::take(padPtr));

    {
        std::unique_lock lock(self->_mutex);

        if (pad->isSink()) {
            self->_sinkPads.push_back(pad);

        } else {
            self->_srcPads.push_back(pad);
        }
    }

    TRACE(("CALL element '{}', pad '{}'", GST_ELEMENT_NAME(element), GST_PAD_NAME(padPtr)));

    auto it = self->_onPadAddedCallbacks.begin();
    while (it != self->_onPadAddedCallbacks.end()) {
        if (it->operator()(pad)) {
            ++it;

        } else {
            it = self->_onPadAddedCallbacks.erase(it);
        }
    }
}

void pipeline::Element::_onPadRemoved(GstElement * element, GstPad *padPtr, Element *self) {
    auto pad = std::make_shared<Pad>(GstPadPtr::take(padPtr));

    TRACE(("CALL element '{}', pad '{}'", GST_ELEMENT_NAME(element), GST_PAD_NAME(padPtr)));

    {
        std::unique_lock lock(self->_mutex);

        if (pad->isSink()) {
            auto it = self->_sinkPads.erase(
                std::remove(self->_sinkPads.begin(), self->_sinkPads.end(), pad), 
                self->_sinkPads.end()
            );

        } else {
            auto it = self->_srcPads.erase(
                std::remove(self->_srcPads.begin(), self->_srcPads.end(), pad), 
                self->_srcPads.end()
            );
        }
    }

    auto it = self->_onPadRemovedCallbacks.begin();
    while (it != self->_onPadRemovedCallbacks.end()) {
        if (it->operator()(pad)) {
            ++it;

        } else {
            it = self->_onPadRemovedCallbacks.erase(it);
        }
    }
}

void pipeline::Element::_onNoMorePads(GstElement * element, Element *self) {
    auto it = self->_onAllPadsCallbacks.begin();
    while (it != self->_onAllPadsCallbacks.end()) {
        if (it->operator()()) {
            ++it;

        } else {
            it = self->_onAllPadsCallbacks.erase(it);
        }
    }
}

pipeline::Element::Element(const GstElementPtr &ptr) : GstWrapper(ptr) {
    this->_pendingCallback = {};
    this->_pendingState    = GST_STATE_NULL;

    if (ptr) {
        this->_name = GST_ELEMENT_NAME(*ptr);

        this->onSignal("pad-added",    G_CALLBACK(_onPadAdded), this);
        this->onSignal("pad-removed",  G_CALLBACK(_onPadRemoved), this);
        this->onSignal("no-more-pads", G_CALLBACK(_onNoMorePads), this);

        {
            auto *it = gst_element_iterate_pads((*this)->get());

            std::unique_lock lock(this->_mutex);

            bool done = false;
            while (! done) {
                GValue padptr = { 0, };

                switch (gst_iterator_next (it, &padptr)) {
                    case GST_ITERATOR_OK:
                        {
                            auto pad = std::make_shared<Pad>(
                                GstPadPtr::take(
                                    reinterpret_cast<GstPad *>(g_value_get_object(&padptr))
                                )
                            );

                            if (pad->isSink()) {
                                this->_sinkPads.push_back(pad);

                            } else {
                                this->_srcPads.push_back(pad);
                            }
                        }
                        break;

                    case GST_ITERATOR_DONE:
                        done = true;
                        break;

                    case GST_ITERATOR_RESYNC:
                        gst_iterator_resync (it);
                        break;

                    case GST_ITERATOR_ERROR:
                        break;
                }

                g_value_unset (&padptr);
            }

            gst_iterator_free(it);
        }
    }
}

pipeline::Element::~Element() {
    auto *gstElement = (*this)->get();

    if (gstElement) {
        for (auto sig : this->_signals) {
            g_signal_handler_disconnect(gstElement, sig);
        }
    }

    this->_signals.clear();
}

const std::string &pipeline::Element::getName() const {
    return this->_name;
}

void pipeline::Element::link(ElementPtr &other) {
    if (! *this || ! other) {

    }

    if (! gst_element_link((*this)->get(), (*other)->get())) {
        throw InvalidArgumentException("Error linking elements '" + this->getName() + "' -> '" + other->getName() + "'");
    }
}

void pipeline::Element::syncWithParent() {
    gst_element_sync_state_with_parent((*this)->get());
}

void pipeline::Element::lockState(bool lock) {
    gst_element_set_locked_state((*this)->get(), lock);
}

void pipeline::Element::onPadAdded(OnPadAddedCallback callback) {
    this->_onPadAddedCallbacks.push_back(callback);
}

void pipeline::Element::onPadRemoved(OnPadRemovedCallback callback) {
    this->_onPadRemovedCallbacks.push_back(callback);
}

void pipeline::Element::onAllPads(OnAllPadsCallback callback) {
    this->_onAllPadsCallbacks.push_back(callback);
}

void pipeline::Element::onSignal(const std::string &signalName, GCallback callback, void *data) {
    guint sig = g_signal_connect((*this)->get(), signalName.c_str(), callback, data);
    if (sig <= 0) {
        throw Exception("Unable to subscribe signal '" + signalName + "'");
    }

    this->_signals.push_back(sig);
}

void pipeline::Element::onMessage(OnGstMessageCallback callback) {
    this->_onGstMessageCallbacks.push_back(callback);
}

void pipeline::Element::onReady(OnStateChangeCallback callback) {
    this->_onStateChangeCallbacks[GST_STATE_READY].push_back(callback);
}

void pipeline::Element::onPause(OnStateChangeCallback callback) {
    this->_onStateChangeCallbacks[GST_STATE_PAUSED].push_back(callback);
}

void pipeline::Element::onPlay(OnStateChangeCallback callback) {
    this->_onStateChangeCallbacks[GST_STATE_PLAYING].push_back(callback);
}

void pipeline::Element::onNull(OnStateChangeCallback callback) {
    this->_onStateChangeCallbacks[GST_STATE_NULL].push_back(callback);
}

void pipeline::Element::_setState(GstState state, std::function<void(bool)> callback) {
    if (! *this) {
        ERR(("Trying to change state of null element!"));

        if (callback) {
            callback(false);
        }

    } else {
        this->_pendingCallback = {};

        auto ret = gst_element_set_state((*this)->get(), state);
        if (callback) {
            switch (ret) {
                case GST_STATE_CHANGE_SUCCESS:
                case GST_STATE_CHANGE_NO_PREROLL:
                    callback(true);
                    break;

                case GST_STATE_CHANGE_FAILURE:
                    callback(false);
                    break;

                case GST_STATE_CHANGE_ASYNC:
                    this->_pendingCallback = callback;
                    this->_pendingState    = state;
                    break;
            }
        }
    }
}

void pipeline::Element::ready(std::function<void(bool)> callback) {
    this->_setState(GST_STATE_READY, callback);
}

void pipeline::Element::pause(std::function<void(bool)> callback) {
    this->_setState(GST_STATE_PAUSED, callback);
}

void pipeline::Element::play(std::function<void(bool)> callback) {
    this->_setState(GST_STATE_PLAYING, callback);
}

void pipeline::Element::null(std::function<void(bool)> callback) {
    this->_setState(GST_STATE_NULL, callback);
}


bool pipeline::Element::syncStateWithParent() {
    if (! *this) {
        ERR(("Trying to synchronize state of null element!"));

        return false;
    }

    return gst_element_sync_state_with_parent((*this)->get());
}

pipeline::PadPtr pipeline::Element::requestPad(const std::string &name) {
    return std::make_shared<Pad>(GstPadPtr::wrap(gst_element_request_pad_simple((*this)->get(), name.c_str())));
}

void pipeline::Element::releasePad(PadPtr pad) {
    if (pad) {
        this->releasePad(**pad);
    }
}

void pipeline::Element::releasePad(const GstPadPtr &pad) {
    if (pad) {
        gst_element_release_request_pad((*this)->get(), pad.get());
    }
}

pipeline::PadPtr pipeline::Element::getPad(const std::string &name) {
    std::unique_lock lock(this->_mutex);

    {
        auto it = std::find_if(this->_srcPads.begin(), this->_srcPads.end(), [&name](const PadPtr &ptr) {
            return (ptr->getName() == name);
        });

        if (it != this->_srcPads.end()) {
            return *it;
        }
    }

    {
        auto it = std::find_if(this->_sinkPads.begin(), this->_sinkPads.end(), [&name](const PadPtr &ptr) {
            return (ptr->getName() == name);
        });

        if (it != this->_sinkPads.end()) {
            return *it;
        }
    }

    WARN(("Pad of name '{}' was not found in element '{}'", name, this->getName()));

    return nullptr;
}

const std::vector<pipeline::PadPtr> &pipeline::Element::getPads(bool src) const {
    std::unique_lock lock(this->_mutex);
    
    if (src) {
        return this->_srcPads;

    } else {
        return this->_sinkPads;
    }
}

pipeline::ElementPtr pipeline::Element::make(const std::string &factoryName) {
    return make("", factoryName);
}

pipeline::ElementPtr pipeline::Element::make(const std::string &elementName, const std::string &factoryName) {
    return std::make_shared<Element>(
        GstElementPtr::wrap(
            gst_element_factory_make(factoryName.c_str(), elementName.empty() ? nullptr : elementName.c_str())
        )
    );
}

void pipeline::Element::onStateChanged(GstState state) {
    {
        auto &handlers = this->_onStateChangeCallbacks[state];

        auto it = handlers.begin();
        while (it != handlers.end()) {
            if ((*it)()) {
                ++it;

            } else {
                it = handlers.erase(it);
            }
        }
    }

    if (this->_pendingCallback) {
        if (this->_pendingState == state) {
            auto cb = this->_pendingCallback;

            this->_pendingCallback = {};

            cb(true);
        }
    }
}

bool pipeline::Element::onMessageArrived(GstMessage *msg) const {
    bool ret = false;
    auto it = this->_onGstMessageCallbacks.begin();

    {
        bool handled = false;

        while (it != this->_onGstMessageCallbacks.end()) {
            if ((*it)(msg, handled)) {
                ++it;

            } else {
                it = this->_onGstMessageCallbacks.erase(it);
            }
        }

        ret = ret || handled;
    }

    return ret;
}
