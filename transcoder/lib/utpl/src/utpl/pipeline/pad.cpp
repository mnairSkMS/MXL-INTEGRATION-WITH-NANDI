#include "utpl/common/types/exception.h"
#include "utpl/common/debug.h"

#include "utpl/pipeline/pad.h"

using namespace utpl;


pipeline::Pad::Pad(const GstPadPtr &ptr) : GstWrapper(ptr) {
    this->_eventProbeId = 0;

    if (ptr) {
        this->_name = GST_PAD_NAME(ptr.get());
    }
}

pipeline::Pad::~Pad() {
    this->unblock();

    if (this->_eventProbeId) {
        gst_pad_remove_probe((*this)->get(), this->_eventProbeId);

        this->_eventProbeId  = 0;
        this->_eventCallback = 0;
    }
}

const std::string &pipeline::Pad::getName() const {
    return this->_name;
}

void pipeline::Pad::link(const PadPtr &other) {
    if (! (*this) || ! (*other.get())) {
        throw utpl::InvalidArgumentException("Cannot link pads. One or both pads are null!");
    }

    auto *srcElement = gst_pad_get_parent_element((*this)->get());
    auto *dstElement = gst_pad_get_parent_element((*other)->get());

    {
        GstPadLinkReturn linkResult;

        if (GST_PAD_IS_SRC((*this)->get()) && GST_PAD_IS_SINK((*other)->get())) {
            TRACE(("Linking pads {}:{}/src -> {}:{}/sink", 
                srcElement ? GST_ELEMENT_NAME(srcElement) : "<null>", this->getName(), 
                dstElement ? GST_ELEMENT_NAME(dstElement) : "<null>", other->getName()
            ));

            linkResult = gst_pad_link((*this)->get(), (*other)->get());

        } else if (GST_PAD_IS_SINK((*this)->get()) && GST_PAD_IS_SRC((*other)->get())) {
            TRACE(("Linking pads {}:{}/sink -> {}:{}/src", 
                dstElement ? GST_ELEMENT_NAME(dstElement) : "<null>", other->getName(),
                srcElement ? GST_ELEMENT_NAME(srcElement) : "<null>", this->getName()
            ));

            linkResult = gst_pad_link((*other)->get(), (*this)->get());

        } else {
            throw utpl::InvalidArgumentException("Cannot link pads. Both are the same type!");
        }

        switch (linkResult) {
            case GST_PAD_LINK_OK:
                break;

            case GST_PAD_LINK_WRONG_HIERARCHY:
                throw utpl::InvalidArgumentException("Cannot link pads. Received GST_PAD_LINK_WRONG_HIERARCHY status.");

            case GST_PAD_LINK_WAS_LINKED:
                throw utpl::InvalidArgumentException("Cannot link pads. Received GST_PAD_LINK_WAS_LINKED status.");

            case GST_PAD_LINK_WRONG_DIRECTION:
                throw utpl::InvalidArgumentException("Cannot link pads. Received GST_PAD_LINK_WRONG_DIRECTION status.");

            case GST_PAD_LINK_NOFORMAT:
                throw utpl::InvalidArgumentException("Cannot link pads. Received GST_PAD_LINK_NOFORMAT status.");

            case GST_PAD_LINK_NOSCHED:
                throw utpl::InvalidArgumentException("Cannot link pads. Received GST_PAD_LINK_NOSCHED status.");

            case GST_PAD_LINK_REFUSED:
                throw utpl::InvalidArgumentException("Cannot link pads. Received GST_PAD_LINK_REFUSED status.");
        }
    }
}

void pipeline::Pad::unlink() {
    auto peer = this->getPeer();

    if (! (*this) || ! peer.get()) {
        WARN(("Pad is not linked!"));

    } else {
        auto *srcElement = gst_pad_get_parent_element((*this)->get());
        auto *dstElement = gst_pad_get_parent_element(peer.get());

        if (peer) {
            bool ret;

            if (GST_PAD_IS_SRC((*this)->get()) && GST_PAD_IS_SINK(peer.get())) {
                TRACE(("Unlinking pads {}:{} -> {}:{}", 
                    srcElement ? GST_ELEMENT_NAME(srcElement) : "<null>", this->getName(), 
                    dstElement ? GST_ELEMENT_NAME(dstElement) : "<null>", GST_PAD_NAME(peer.get())
                ));

                ret = gst_pad_unlink((*this)->get(), peer.get());

            } else if (GST_PAD_IS_SINK((*this)->get()) && GST_PAD_IS_SRC(peer.get())) {
                TRACE(("Unlinking pads {}:{} -> {}:{}", 
                    dstElement ? GST_ELEMENT_NAME(dstElement) : "<null>", GST_PAD_NAME(peer.get()),
                    srcElement ? GST_ELEMENT_NAME(srcElement) : "<null>", this->getName()
                ));

                ret = gst_pad_unlink(peer.get(), (*this)->get());
            }

            if (! ret) {
                ERR(("Unable to unlink pads!"));
            }
        }
    }
}

pipeline::GstPadPtr pipeline::Pad::getPeer() {
    return pipeline::GstPadPtr::wrap(gst_pad_get_peer((*this)->get()));
}

bool pipeline::Pad::isLinked() const {
    if (*this) {
        auto pad = pipeline::GstPadPtr::wrap(gst_pad_get_peer((*this)->get()));
        if (pad) {
            return true;
        }
    }

    return false;
}

bool pipeline::Pad::isSource() const {
    auto *pad = (*this)->get();
    if (pad) {
        return GST_PAD_IS_SRC(pad);
    }
    
    return false;
}

bool pipeline::Pad::isSink() const {
    auto *pad = (*this)->get();
    if (pad) {
        return GST_PAD_IS_SINK(pad);
    }
    
    return false;
}

pipeline::Caps pipeline::Pad::getCaps() const {
    auto caps = pipeline::GstCapsPtr::wrap(gst_pad_get_current_caps((*this)->get()));
    if (! caps) {
        caps = pipeline::GstCapsPtr::wrap(gst_pad_query_caps((*this)->get(), nullptr));
    }

    return caps;
}

pipeline::GstElementPtr pipeline::Pad::getParent() const {
    return GstElementPtr::take(GST_ELEMENT_CAST(gst_pad_get_parent((*this)->get())));
}

guint pipeline::Pad::block(bool downstream, BlockCallback callback) {
    GstPadProbeType type;

    if (downstream) {
        type = (GstPadProbeType)(GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM | GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM);
    } else {
        type = (GstPadProbeType)(GST_PAD_PROBE_TYPE_BLOCK_UPSTREAM | GST_PAD_PROBE_TYPE_QUERY_UPSTREAM); 
    }

    auto id = gst_pad_add_probe(
        (*this)->get(), type, (GstPadProbeCallback) _onSourcePadBlocked, this, nullptr
    );

    if (id > 0) {
        std::lock_guard lock(this->_callbacksGuard);

        this->_blockCallbacks[id] = callback;
    }

    return id;
}

guint pipeline::Pad::addProbe(const std::vector<GstPadProbeType> &types, ProbeCallback callback) {
    GstPadProbeType mergedType = GST_PAD_PROBE_TYPE_INVALID;

    for (auto type : types) {
        mergedType = (GstPadProbeType)(mergedType | type);
    }

    {
        std::lock_guard lock(this->_callbacksGuard);

        auto id = gst_pad_add_probe(
            (*this)->get(), mergedType, (GstPadProbeCallback) _onProbe, this, nullptr
        );

        if (id > 0) {
            this->_probeCallbacks.emplace(id, callback);
        }

        return id;
    }
}

void pipeline::Pad::removeProbe(guint probeId) {
    std::lock_guard lock(this->_callbacksGuard);

    auto it = this->_probeCallbacks.find(probeId);
    if (it == this->_probeCallbacks.end()) {
        ERR(("Probe with ID {} was not found in pad {}", probeId, this->getName()));

    } else {
        gst_pad_remove_probe((*this)->get(), it->first);

        this->_probeCallbacks.erase(it);
    }
}

guint pipeline::Pad::probeCurrentPts(PtsCallBack callback) {
    return this->addProbe({ GST_PAD_PROBE_TYPE_BUFFER }, [this, callback](GstPadProbeInfo *info) {
        auto *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        auto *event = gst_pad_get_sticky_event((*this)->get(), GST_EVENT_SEGMENT, 0);

        GstClockTime runningTime = 0;
        GstClockTime pts = GST_BUFFER_PTS(buffer);

        if (event) {
            const GstSegment *segment;

            gst_event_parse_segment(event, &segment);
            gst_event_unref(event);
        
            runningTime = gst_segment_to_running_time(segment, GST_FORMAT_TIME, GST_BUFFER_PTS (buffer));
            
        } else {
            WARN(("Unable to parse event for segment; falling back to pts."));
            runningTime = pts;
        }

        runningTime += GST_BUFFER_DURATION(buffer);

        callback(pts, runningTime);

        return false;
    });
}

guint pipeline::Pad::probeIdle(IdleCallback callback) {
    return this->addProbe({ GST_PAD_PROBE_TYPE_IDLE }, [callback](GstPadProbeInfo *info) {
        return callback();
    });
}

void pipeline::Pad::unblock(guint id) {
    std::lock_guard lock(this->_callbacksGuard);

    {
        std::vector<guint> toRemove;

        if (id == -1) {
            for (auto &block : this->_blockCallbacks) {
                toRemove.push_back(block.first);
            }

            this->_blockCallbacks.clear();

        } else {
            toRemove.push_back(id);

            if (this->_blockCallbacks.erase(id) != 1) {
                ERR(("Block callback for ID {} was  not found in registered callbacks"));
            }
        }

        for (auto id : toRemove) {
            gst_pad_remove_probe((*this)->get(), id);
        }
    }
}

void pipeline::Pad::event(bool downstream, std::function<bool(GstEvent *event, bool &discard)> callback) {
    this->_eventCallback = callback;

    {
        GstPadProbeType type;

        if (downstream) {
            type = (GstPadProbeType)(GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM);

        } else {
            type = (GstPadProbeType)(GST_PAD_PROBE_TYPE_EVENT_UPSTREAM);
        }

        gst_pad_add_probe(
            (*this)->get(), type, (GstPadProbeCallback) _onEventCallback, this, nullptr
        );
    }
}

GstClockTime pipeline::Pad::getTimePosition() {
    gint64 pos;

    if (gst_pad_query_position((*this)->get(), GST_FORMAT_TIME, &pos)) {
        return pos;
    }

    return GST_CLOCK_TIME_NONE;
}

GstClockTime pipeline::Pad::getTimeOffset() {
    return gst_pad_get_offset((*this)->get());
}

void pipeline::Pad::setTimeOffset(GstClockTime offset) {
    gst_pad_set_offset((*this)->get(), offset);
}

void pipeline::Pad::pushEvent(GstEvent *event) {
    if (! gst_pad_push_event((*this)->get(), event)) {
        ERR(("Unable to push event to PAD!"));
    }
}

GstPadProbeReturn pipeline::Pad::_onSourcePadBlocked(GstPad *pad, GstPadProbeInfo *info, Pad *self) {
    std::lock_guard lock(self->_callbacksGuard);

    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
        GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);

        if (! GST_EVENT_IS_SERIALIZED (event)) {
            // do not block on sticky or out of band events otherwise the allocation query from demuxer might block the loop thread
            DBG(("Letting OOB event through"));

            return GST_PAD_PROBE_PASS;
        }

    } else if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM) {
        GstQuery *query = GST_PAD_PROBE_INFO_QUERY(info);

        if (! GST_QUERY_IS_SERIALIZED(query)) {
            // do not block on non-serialized queries
            DBG(("Letting non-serialized query through"));
        
            return GST_PAD_PROBE_PASS;
        }

        if (! gst_pad_has_current_caps(pad)) {
            /* do not block on allocation queries before we have caps,
            * this would deadlock because we are doing no autoplugging
            * without caps.
            * TODO: Try to do autoplugging based on the query caps
            */
            DBG(("Letting serialized query before caps through"));
            
            return GST_PAD_PROBE_PASS;
        }
    }

    {
        auto id = GST_PAD_PROBE_INFO_ID(info);

        auto it = self->_blockCallbacks.find(id);
        if (it == self->_blockCallbacks.end()) {
            ERR(("Callback for ID {} was not registered. This should never happen", id));

            return GST_PAD_PROBE_REMOVE;
        }

        if (it->second && ! it->second()) {
            self->_blockCallbacks.erase(it);

            return GST_PAD_PROBE_REMOVE;
        }
    }

    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn pipeline::Pad::_onEventCallback(GstPad *pad, GstPadProbeInfo *info, Pad *self) {
    GstPadProbeReturn ret = GST_PAD_PROBE_OK;

    if (! self->_eventCallback) {
        self->_eventProbeId = 0;

        ret = GST_PAD_PROBE_REMOVE;

    } else {
        GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);

        bool discard = false;

        if (! self->_eventCallback(event, discard)) {
            self->_eventProbeId  = 0;
            self->_eventCallback = {};


            ret = GST_PAD_PROBE_REMOVE;
        }

        if (discard) {
            ret = GST_PAD_PROBE_DROP;
        }
    }

    return ret;
}

GstPadProbeReturn pipeline::Pad::_onProbe(GstPad *srcpad, GstPadProbeInfo *info, Pad *self) {
    GstPadProbeReturn ret = GST_PAD_PROBE_REMOVE;

    {
        std::lock_guard lock(self->_callbacksGuard);

        auto it = self->_probeCallbacks.find(info->id);

        if (it != self->_probeCallbacks.end()) {
            if (it->second(info)) {
                ret = GST_PAD_PROBE_OK;

            } else {
                self->_probeCallbacks.erase(it);
            }
        }
    }

    return ret;
}

/* ######################## GhostPad ######################## */

pipeline::GhostPad::GhostPad(const GstPadPtr &ptr) : Pad(ptr) {
}

void pipeline::GhostPad::setTarget(PadPtr target) {
    GstPad *targetPtr = nullptr;

    if (target) {
        targetPtr = (*target)->get();
    }

    gst_ghost_pad_set_target(GST_GHOST_PAD((*this)->get()), targetPtr);
}

pipeline::GstPadPtr pipeline::GhostPad::getTarget() {
    return pipeline::GstPadPtr::wrap(
        gst_ghost_pad_get_target(GST_GHOST_PAD((*this)->get()))
    );
}

pipeline::GhostPadPtr pipeline::GhostPad::create(GstPadDirection direction, const std::string &name) {
    return std::make_shared<GhostPad>(
        pipeline::GstPadPtr::take(
            gst_ghost_pad_new_no_target(name.empty() ? nullptr : name.c_str(), direction)
        )
    );
}

pipeline::GhostPadPtr pipeline::GhostPad::create(GstPadPtr pad, const std::string &name) {
    return std::make_shared<GhostPad>(
        pipeline::GstPadPtr::take(
            gst_ghost_pad_new(name.empty() ? nullptr : name.c_str(), pad.get())
        )
    );
}
