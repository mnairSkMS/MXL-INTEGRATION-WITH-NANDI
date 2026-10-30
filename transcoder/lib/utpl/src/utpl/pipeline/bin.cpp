#include "utpl/pipeline/bin.h"

#include "utpl/common/debug.h"

using namespace utpl;


pipeline::Bin::Bin(const pipeline::GstElementPtr &ptr) : Element(ptr) {
    GstIterator *it;
    GValue data = { 0, };

    it = gst_bin_iterate_elements(GST_BIN(ptr.get()));

    bool done = false;
    while (! done) {
        switch (gst_iterator_next(it, &data)) {
            case GST_ITERATOR_OK:
                {
                    GstElement *child = GST_ELEMENT(g_value_get_object(&data));

                    this->_children.push_back(
                        std::make_shared<pipeline::Element>(GstElementPtr::take(child))
                    );

                    g_value_reset (&data);
                }
                break;

            case GST_ITERATOR_RESYNC:
                gst_iterator_resync (it);
                break;
            
            default:
            case GST_ITERATOR_DONE:
                done = TRUE;
                break;
        }
    }

    g_value_unset(&data);
    gst_iterator_free(it);
}

pipeline::ElementPtr pipeline::Bin::add(const ElementPtr &wrapper) {
    std::lock_guard guard(this->_childrenLock);

    auto it = std::find_if(this->_children.begin(), this->_children.end(), [&wrapper](const ElementPtr &el) {
        if (el == wrapper) {
            return true;
        }

        return false;
    });

    if (it != this->_children.end()) {
        ERR(("Element '{}' already added to the bin!", wrapper->getName()));

    } else {
        this->_children.push_back(std::move(wrapper));

        if (! gst_bin_add(GST_BIN((*this)->get()), (*wrapper)->get())) {
            ERR(("Unable to add node '{}' to bin '{}'!", wrapper->getName(), this->getName()));

        } else {
            (*wrapper)->release();
        }
    }

    return wrapper;
}

void pipeline::Bin::remove(const ElementPtr &wrapper) {
    std::lock_guard guard(this->_childrenLock);

    auto it = std::find_if(this->_children.begin(), this->_children.end(), [&wrapper](const ElementPtr &el) {
        if (el == wrapper) {
            return true;
        }

        return false;
    });

    if (it == this->_children.end()) {
        std::string name = "<null>";

        if (wrapper) {
            name = GST_ELEMENT_NAME(wrapper.get());
        }

        ERR(("No such Node element '{}' ({}) was attached to the bin '{}'", name, (void *) wrapper.get(), this->getName()));

    } else {
        (*wrapper)->ref();

        if (! gst_bin_remove(GST_BIN((*this)->get()), (*wrapper)->get())) {
            ERR(("Unable to remove Node from bin!"));
            
            (*wrapper)->unref();
        }

        this->_children.erase(it);
    }
}

pipeline::ElementPtr pipeline::Bin::get(const std::string &name) {
    std::lock_guard guard(this->_childrenLock);

    auto it = std::find_if(this->_children.begin(), this->_children.end(), [&name](const ElementPtr &el) {
        if (el->getName() == name) {
            return true;
        }

        return false;
    });

    if (it != this->_children.end()) {
        return *it;
    }

    for (auto &node : this->_children) {
        auto *nextBin = dynamic_cast<pipeline::Bin *>(node.get());
        if (nextBin) {
            auto ret = nextBin->get(name);
            if (ret) {
                return ret;
            }
        }
    }

    return nullptr;
}

void pipeline::Bin::forEach(std::function<void(const ElementPtr &)> callback) const {
    std::lock_guard guard(this->_childrenLock);

    for (auto &p : this->_children) {
        callback(p);
    }
}

void pipeline::Bin::forEach(std::function<void(ElementPtr &)> callback) {
    std::lock_guard guard(this->_childrenLock);

    for (auto &p : this->_children) {
        callback(p);
    }
}

bool pipeline::Bin::contains(GstBin *bin, const std::string &name, bool recursive) {
    bool ret = false;

    auto *iter = gst_bin_iterate_elements(bin);

    GValue item = G_VALUE_INIT;

    bool iterationDone = false;
    while (! iterationDone && ! ret) {
        switch (gst_iterator_next(iter, &item)) {
            case GST_ITERATOR_OK:
                {
                    GstElement *element = GST_ELEMENT(g_value_get_object(&item));

                    if (strcmp(GST_ELEMENT_NAME(element), name.c_str()) == 0) {
                        ret = true;

                    } else if (GST_IS_BIN(element)) {
                        ret = contains(GST_BIN(element), name, recursive);
                    }

                    g_value_reset(&item);
                }
                break;

            case GST_ITERATOR_RESYNC:
                gst_iterator_resync(iter);
                break;

              case GST_ITERATOR_ERROR:
              case GST_ITERATOR_DONE:
                iterationDone = TRUE;
                break;
        }
    }

    g_value_unset(&item);
    gst_iterator_free(iter);

    return ret;
}

bool pipeline::Bin::contains(const std::string &name, bool recursiveSearch) {
    return this->contains(GST_BIN((*this)->get()), name, recursiveSearch);
}

pipeline::GhostPadPtr pipeline::Bin::addPad(const std::string &name, PadPtr targetPad, bool srcDirection) {
    GstPad *target = nullptr;

    if (targetPad) {
        target = (*targetPad)->get();
    }

    GstPadPtr wrapped;

    if (target) {
        wrapped = GstPadPtr::take(
            gst_ghost_pad_new(name.empty() ? nullptr : name.c_str(), target)
        );

    } else {
        wrapped = GstPadPtr::take(
            gst_ghost_pad_new_no_target(
                name.empty() ? nullptr : name.c_str(), srcDirection ? GstPadDirection::GST_PAD_SRC : GstPadDirection::GST_PAD_SINK
            )
        );
    }

    if (gst_element_add_pad((*this)->get(), *wrapped)) {
        auto ret = std::static_pointer_cast<GhostPad>(this->getPad(GST_PAD_NAME(*wrapped)));
        
        return ret;

    } else {
        ERR(("Unable to add ghost pad to bin!"));
    }

    return nullptr;
}

void pipeline::Bin::removePad(PadPtr pad) {
    if (! gst_element_remove_pad((*this)->get(), (*pad)->get())) {
        ERR(("Unable to remove pad: {} from bin: {}", pad->getName(), this->getName()));
    }
}

pipeline::BinPtr pipeline::Bin::create(const std::string &name) {
    return std::make_shared<Bin>(pipeline::GstElementPtr::wrap(gst_bin_new(name.empty() ? nullptr : name.c_str())));
}

pipeline::BinPtr pipeline::Bin::parse(const std::string &desc) {
    return parse("", desc);
}

pipeline::BinPtr pipeline::Bin::parse(const std::string &name, const std::string &desc) {
    auto ret = pipeline::GstElementPtr::wrap(gst_parse_bin_from_description(desc.c_str(), true, NULL));
    
    if (! name.empty()) {
        gst_element_set_name(*ret, name.c_str());
    }

    return std::make_shared<Bin>(ret);
}

bool pipeline::Bin::isBin(const ElementPtr &ptr) {
    if (ptr) {
        if (GST_IS_BIN((*ptr)->get())) {
            return true;
        }
    }

    return false;
}
