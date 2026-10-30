#include "utpl/common/debug.h"

#include "utpl/pipeline/pipe.h"

using namespace utpl;


pipeline::Pipe::Pipe(const GstElementPtr &ptr) : Bin(ptr) {
    this->_bus          = nullptr;
    this->_pipelineLoop = g_main_loop_new(nullptr, false);

    this->_bus = gst_pipeline_get_bus(GST_PIPELINE((*this)->get()));

    gst_bus_add_watch_full(this->_bus, G_PRIORITY_DEFAULT, (GstBusFunc)_busCallback, this, nullptr);
}


pipeline::Pipe::~Pipe() {
    this->null();

    gst_bus_remove_watch(this->_bus);

    gst_object_unref(this->_bus);

    g_main_loop_unref(this->_pipelineLoop);
}


void pipeline::Pipe::loop() {
    g_main_loop_run(this->_pipelineLoop);
}


void pipeline::Pipe::interrupt() {
    g_main_loop_quit(this->_pipelineLoop);
}


pipeline::PipePtr pipeline::Pipe::create(const std::string &name) {
    return std::make_shared<Pipe>(pipeline::GstElementPtr::wrap(gst_pipeline_new(name.empty() ? nullptr : name.c_str())));
}

pipeline::PipePtr pipeline::Pipe::parse(const std::string &desc) {
    pipeline::PipePtr ret;

    {
        GError *error = nullptr;

        auto *pipe = gst_parse_launch(desc.c_str(), &error);
        if (error) {
            ERR(("Error parsing pipeline descritpion: '{}': ", desc.c_str(), error->message));

            g_error_free(error);

        } else {
            ret = std::make_shared<Pipe>(pipeline::GstElementPtr::wrap(pipe));
        }
    }

    return ret;
}

void pipeline::Pipe::onEos(OnEosCallback callback) {
    this->_eosCallbacks.push_back(callback);
}


void pipeline::Pipe::onError(OnErrorCallback callback) {
    this->_errorCallbacks.push_back(callback);
}


bool pipeline::Pipe::_notifyMessage(const pipeline::Bin &bin, GstMessage *msg) {
    bool ret = false;

    bin.forEach([msg, &ret](const ElementPtr &element) {
        ret = ret || ((const pipeline::Pipe *) element.get())->onMessageArrived(msg);

        {
            pipeline::Bin *nextBin = dynamic_cast<pipeline::Bin *>(element.get());
            if (nextBin) {
                ret = ret || _notifyMessage(*nextBin, msg);
            }
        }
    });

    return ret;
}


gboolean pipeline::Pipe::_busCallback(GstBus *bus, GstMessage *message, Pipe *self) {
    std::string elementName = GST_MESSAGE_SRC_NAME(message);
    Element    *element     = nullptr;
    //  A race condition might occur, so we need to hold the element pointer.
    ElementPtr  elementPtr;

    {
        if (elementName == self->getName()) {
            element = self;

        } else {
            elementPtr = self->get(elementName);
        }

        if (elementPtr) {
            element = elementPtr.get();
        }
    }

    bool handled = _notifyMessage(*self, message);

    switch (message->type) {
        case GST_MESSAGE_STATE_CHANGED:
            {
                GstState oldState;
                GstState newState;
                GstState pendingState;

                gst_message_parse_state_changed(message, &oldState, &newState, &pendingState);

                if (element) {
                    ((Pipe *)element)->onStateChanged(newState);
                }
            }
            break;

        case GST_MESSAGE_ASYNC_DONE:
            // Setting a state the element is already in - which is what adding an
            // output to an already running pipeline does - returns ASYNC but posts no
            // STATE_CHANGED, because the state itself does not change. ASYNC_DONE is
            // then the only completion the element ever gets, so a callback waiting on
            // that state has to be resolved from here too, or it is never called.
            if (element) {
                GstState currentState = GST_STATE_VOID_PENDING;

                gst_element_get_state(GST_ELEMENT(message->src), &currentState, nullptr, 0);

                ((Pipe *)element)->onStateChanged(currentState);
            }
            break;

        case GST_MESSAGE_ERROR:
        case GST_MESSAGE_WARNING:
            if (! handled) {
                GError *gerror = nullptr;
                gchar  *debug  = nullptr;

                if (message->type == GST_MESSAGE_ERROR) {
                    gst_message_parse_error (message, &gerror, &debug);

                } else {
                    gst_message_parse_warning (message, &gerror, &debug);
                }

                if (message->type == GST_MESSAGE_ERROR)  {
                    ERR(("Gstreamer reported an error on element '{}': code: {}, message: {}", elementName, gerror ? gerror->code : -1, debug));

                    for (auto &callback : self->_errorCallbacks) {
                        callback(*self, debug);
                    }

                } else {
                    ERR(("Gstreamer reported a warning on element '{}': code: {}, message: {}", elementName, gerror ? gerror->code : -1, debug));
                }

                if (gerror) {
                    g_error_free (gerror);
                }

                if (debug) {
                    g_free (debug);
                }
            }
            break;

        case GST_MESSAGE_EOS:
            {
                DBG(("GST_MESSAGE_EOS from element: {}", GST_MESSAGE_SRC_NAME(message)));

                for (auto &callback : self->_eosCallbacks) {
                    callback(*self);
                }
            }
            break;

        case GST_MESSAGE_BUFFERING:
            {
                gint percent = 0;

                gst_message_parse_buffering(message, &percent);
            }
            break;

        case GST_MESSAGE_CLOCK_LOST:
            {
                DBG(("GST_MESSAGE_CLOCK_LOST Clock lost!"));

                gst_element_set_state((*self)->get(), GST_STATE_PAUSED);
                gst_element_set_state((*self)->get(), GST_STATE_PLAYING);
            }
            break;

        case GST_MESSAGE_ELEMENT:
            {
                DBG(("GST_MESSAGE_ELEMENT {}", elementName));
            }
            break;

        // For future use
        case GST_MESSAGE_TAG:
            {
                GstTagList *list = NULL;

                TRACE(("GST_MESSAGE_TAG"));

                gst_message_parse_tag(message, &list);
                {
                    gchar *value = NULL;

                    if (gst_tag_list_get_string(list, GST_TAG_TITLE, &value)) {
                        TRACE(("title: '%s'", value));

                        g_free(value);
                    }

                    if (gst_tag_list_get_string(list, GST_TAG_ARTIST, &value)) {
                        TRACE(("artist: '%s'", value));

                        g_free(value);
                    }

                    if (gst_tag_list_get_string(list, GST_TAG_ALBUM, &value)) {
                        TRACE(("album: '%s'", value));

                        g_free(value);
                    }
                }
                gst_tag_list_free(list);
            }
            break;
    }

    return true;
}
