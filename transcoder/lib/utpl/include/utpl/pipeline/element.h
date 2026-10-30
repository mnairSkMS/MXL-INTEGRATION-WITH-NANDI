#ifndef __UTPL_PIPELINE_ELEMENT_H__
#define __UTPL_PIPELINE_ELEMENT_H__

#include <functional>
#include <mutex>
#include <memory>

#include "utpl/common/utils/gstreamer/wrapper.h"

#include "utpl/pipeline/types.h"
#include "utpl/pipeline/pad.h"

namespace utpl {
    namespace pipeline {
        class Element;

        using ElementPtr = std::shared_ptr<Element>;

        class Element : public GstWrapper<GstElementPtr> {
            public:
                using OnPadAddedCallback    = std::function<bool(PadPtr &pad)>;
                using OnPadRemovedCallback  = std::function<bool(PadPtr &pad)>;
                using OnAllPadsCallback     = std::function<bool()>;
                using OnStateChangeCallback = std::function<bool()>;
                using OnGstMessageCallback  = std::function<bool(GstMessage *, bool &handled)>;

            public:
                Element(const GstElementPtr &ptr);
                virtual ~Element();

                const std::string &getName() const override;

            public:
                void link(ElementPtr &other);

                void onPadAdded(OnPadAddedCallback callback);
                void onPadRemoved(OnPadRemovedCallback callback);
                void onAllPads(OnAllPadsCallback callback);
                void onSignal(const std::string &signalName, GCallback callback, void *data);
                void onMessage(OnGstMessageCallback callback);
                
                void onReady(OnStateChangeCallback callback);
                void onPause(OnStateChangeCallback callback);
                void onPlay(OnStateChangeCallback callback);
                void onNull(OnStateChangeCallback callback);

                template<typename Type>
                void setProperty(const std::string &name, const Type &value) {
                    g_object_set((*this)->get(), name.c_str(), value, nullptr);
                }

                template<typename Type>
                Type getProperty(const std::string &name) {
                    Type t;
                    g_object_get((*this)->get(), name.c_str(), &t, nullptr);
                    return t;
                }

                void ready(std::function<void(bool)> callback = {});
                void pause(std::function<void(bool)> callback = {});
                void play(std::function<void(bool)> callback = {});
                void null(std::function<void(bool)> callback = {});

                bool syncStateWithParent();

                PadPtr requestPad(const std::string &name);
                void   releasePad(PadPtr pad);
                void   releasePad(const GstPadPtr &pad);

                PadPtr getPad(const std::string &name);
                const std::vector<PadPtr> &getPads(bool src) const;

                void syncWithParent();
                void lockState(bool lock);

                static ElementPtr make(const std::string &elementName, const std::string &factoryName);
                static ElementPtr make(const std::string &factoryName);

            protected:
                void onStateChanged(GstState state);
                bool onMessageArrived(GstMessage *msg) const;

            private:
                static void _onPadAdded(GstElement *element, GstPad *pad, Element *self);
                static void _onPadRemoved(GstElement * element, GstPad *pad, Element *self);
                static void _onNoMorePads(GstElement * element, Element *self);

                void _setState(GstState state, std::function<void(bool)> callback);

            private:
                std::string _name;
                
                mutable std::mutex _mutex;

                std::vector<OnPadAddedCallback>   _onPadAddedCallbacks;
                std::vector<OnPadRemovedCallback> _onPadRemovedCallbacks;
                std::vector<OnAllPadsCallback>    _onAllPadsCallbacks;
                
                mutable std::vector<OnGstMessageCallback> _onGstMessageCallbacks;

                std::vector<guint> _signals;

                std::vector<PadPtr> _srcPads;
                std::vector<PadPtr> _sinkPads;

                std::function<void(bool)> _pendingCallback;
                GstState                  _pendingState;

                std::unordered_map<GstState, std::vector<OnStateChangeCallback>> _onStateChangeCallbacks;
        };
    }
}

#endif /* __UTPL_PIPELINE_ELEMENT_H__ */