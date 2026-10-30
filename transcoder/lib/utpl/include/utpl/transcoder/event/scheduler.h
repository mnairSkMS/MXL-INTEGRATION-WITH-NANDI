#ifndef __UTPL_TRANSCODER_EVENT_SCHEDULER_H__
#define __UTPL_TRANSCODER_EVENT_SCHEDULER_H__

#include <memory>
#include <unordered_map>

#include "utpl/common/utils/class.h"

#include "utpl/transcoder/channel.h"
#include "utpl/transcoder/event/provider.h"


namespace utpl {
    namespace transcoder {
        class SqueezeParameters {
            public:
                SqueezeParameters() {
                    this->_srcWindow = ORIGINAL_WINDOW_NORM;
                    this->_dstWindow = ORIGINAL_WINDOW_NORM;
                }

                const utpl::NormRect &getSrcWindow() const {
                    return this->_srcWindow;
                }

                const utpl::NormRect &getDstWindow() const {
                    return this->_dstWindow;
                }

                void setSrcWindow(const utpl::NormRect &w) {
                    this->_srcWindow = w;
                }

                void setDstWindow(const utpl::NormRect &w) {
                    this->_dstWindow = w;
                }

            private:
                utpl::NormRect _srcWindow;
                utpl::NormRect _dstWindow;
        };

        class PredefinedSqueezes {
            public:
                PredefinedSqueezes() {

                }

                const std::unordered_map<std::string, SqueezeParameters> &getSqueezes() const {
                    return this->_squeezes;
                }

                const SqueezeParameters *getSqueezeParametersByName(const std::string &name) const {
                    auto it = this->_squeezes.find(name);
                    if (it == this->_squeezes.end()) {
                        return nullptr;
                    }

                    return &it->second;
                }

                void addSqueeze(const std::string &name, const SqueezeParameters &params) {
                    this->_squeezes[name] = params;
                }

            private:
                std::unordered_map<std::string, SqueezeParameters> _squeezes;
        };

        class EventScheduler {
            private:
                class ScheduledEvent {
                    public:
                        enum State {
                            INIT,
                            STARTING,
                            STARTED,
                            STOPPING,
                            HANDLED
                        };

                    public:
                        ScheduledEvent(EventProvider *provider, Channel *channel, Channel::TimedCallbackSource src, bool asyncStart, bool asyncStop);
                        virtual ~ScheduledEvent();

                        ScheduledEvent(ScheduledEvent &&other);
                        ScheduledEvent& operator=(ScheduledEvent&&);

                        void prepare();
                        bool isHandled() const;
                        bool isInProgress() const;
                        bool isStopping() const;

                        virtual void update(Event *event);
                        virtual bool cancel();

                        Channel       *getChannel();
                        const Channel *getChannel() const;
                        
                        EventProvider       *getProvider();
                        const EventProvider *getProvider() const;

                    public:
                        virtual Event *getEvent() = 0;

                        virtual void handleStart() = 0;
                        virtual void handleStop() = 0;

                    protected:
                        void setState(State state);

                        // The channel's running time now, read from the compositor's programme
                        // layer, or GST_CLOCK_TIME_NONE when the channel has no video compositor.
                        GstClockTime getCurrentRunningTime();

                        // As-run trace. One FIRED line when an event takes effect on air and one
                        // ENDED line when it stops, in a fixed "key=value" format keyed by the
                        // event ID and the REST type name, so the two can be joined with
                        // GET /scheduled_events. MISSED reports an event that never went on air.
                        // Static so they are safe to call from pad probes that outlive `this`.
                        static const char *typeName(const Event &event);
                        static void logFired(const Event &event, utpl::Timestamp at, GstClockTime runningTime, const std::string &detail = std::string());
                        static void logEnded(const Event &event, utpl::Timestamp at, GstClockTime runningTime, const std::string &detail = std::string());
                        static void logMissed(const Event &event, const std::string &reason);

                    private:
                        bool _onEventStart();
                        bool _onEventStop();

                    private:
                        EventProvider *provider;
                        std::string    error;
                        Channel       *channel;

                        State state;
                        bool asyncStart;
                        bool asyncStop;

                        utpl::pipeline::TimedCallbackId startCallbackId;
                        utpl::pipeline::TimedCallbackId stopCallbackId;
                        Channel::TimedCallbackSource timedSource;

                        mutable std::recursive_mutex mutex;
                };

                class ScheduledSqueezeEvent;
                class ScheduledOverlayEvent;
                class ScheduledSpliceInsertEvent;
                class ScheduledTimeSignalEvent;
                class ScheduledReturnToNetworkEvent;
                class ScheduledSlateEvent;

            public:
                EventScheduler();
                ~EventScheduler();

                void addProvider(EventProvider *provider);
                void removeProvider(EventProvider *provider);

                void start(const PredefinedSqueezes &squeezes, const ScteParameters &scteParams, Channel *channel);
                void stop();

            private:
                static bool _onCheckForNewEvent(EventScheduler *self);

            private:
                std::vector<EventProvider *> _providers;
                Channel *_channel;

                guint _onCheckForNewEventSigId;

                std::vector<std::unique_ptr<ScheduledEvent>> _events;
                PredefinedSqueezes _squeezes;
                ScteParameters     _scte;
        };
    }
}

#endif /* __UTPL_TRANSCODER_EVENT_SCHEDULER_H__ */
