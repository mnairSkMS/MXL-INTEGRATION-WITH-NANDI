#ifndef __UTPL_PIPELINE_PAD_H__
#define __UTPL_PIPELINE_PAD_H__

#include <memory>
#include <functional>

#include "utpl/pipeline/caps.h"

namespace utpl {
    namespace pipeline {
        class Pad;
        class GhostPad;

        using PadPtr      = std::shared_ptr<Pad>;
        using GhostPadPtr = std::shared_ptr<GhostPad>;
        
        using PtsCallBack   = std::function<void(GstClockTime pts, GstClockTime runningTime)>;
        using BlockCallback = std::function<bool()>;
        using IdleCallback  = std::function<bool()>;
        using ProbeCallback = std::function<bool(GstPadProbeInfo *info)>;

        class Pad : public GstWrapper<GstPadPtr> {
            private:
                static GstPadProbeReturn _onSourcePadBlocked(GstPad *pad, GstPadProbeInfo *info, Pad *self);
                static GstPadProbeReturn _onEventCallback(GstPad *pad, GstPadProbeInfo *info, Pad *self);
                static GstPadProbeReturn _onProbe(GstPad *srcpad, GstPadProbeInfo *info, Pad *self);

            public:
                Pad(const GstPadPtr &ptr);
                ~Pad();

                const std::string &getName() const override;

            public:
                void link(const PadPtr &other);
                void unlink();
                bool isLinked() const;
                bool isSource() const;
                bool isSink() const;
                GstPadPtr getPeer();

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

                Caps getCaps() const;
                GstElementPtr getParent() const;

                guint block(bool downstream, BlockCallback callback = {});
                void unblock(guint id = -1);

                guint probeCurrentPts(PtsCallBack callback);
                guint probeIdle(IdleCallback callback);
                
                guint addProbe(const std::vector<GstPadProbeType> &types, ProbeCallback callback);
                void  removeProbe(guint probeId);

                void event(bool downstream, std::function<bool(GstEvent *event, bool &discard)> callback);

                GstClockTime getTimePosition();

                GstClockTime getTimeOffset();
                void setTimeOffset(GstClockTime offset);

                void pushEvent(GstEvent *event);

            private:
                std::recursive_mutex _callbacksGuard;
                std::string _name;

                std::function<bool(GstEvent *, bool &discard)> _eventCallback;
                guint                                          _eventProbeId;

                std::unordered_map<guint, BlockCallback> _blockCallbacks;
                std::unordered_map<guint, ProbeCallback> _probeCallbacks;
        };

        class GhostPad : public Pad {
            public:
                GhostPad(const GstPadPtr &ptr);

                void setTarget(PadPtr target);
                GstPadPtr getTarget();

            public:
                static GhostPadPtr create(GstPadDirection direction, const std::string &name = std::string());
                static GhostPadPtr create(GstPadPtr pad, const std::string &name = std::string());
        };
    }
}

#endif /* __UTPL_PIPELINE_PAD_H__ */
