#ifndef __APPLICATION_EVENT_PROVIDER_DUMMY_H__
#define __APPLICATION_EVENT_PROVIDER_DUMMY_H__

#include "utpl/transcoder/event/provider.h"

namespace app {
    class DummyEventProvider : public utpl::transcoder::EventProvider {
        private:
            enum EventType {
                FIRST,

                SQUEEZE_LOW = FIRST,
                SQUEEZE_L_BAND,

                OVERLAY_STATIC,

                LAST,
            };

        public:
            DummyEventProvider();

        public:
            std::vector<std::unique_ptr<utpl::transcoder::Event>> getNextEvents(utpl::Timestamp notBefore, utpl::Timestamp notAfter) override;

            void markProcessed(const utpl::transcoder::Event &event, const std::string &errorMessage) override;

            bool isValid(const utpl::transcoder::Event &event) override;

            void start() override;
            void stop() override;

        private:
            void nextEvent();

        private:
            utpl::Timestamp _lastEventTimestamp;
            
            int             _eventId;
            EventType       _eventType;
            bool            _eventReady;
            utpl::Timestamp _eventTimestamp;
    };
}

#endif /* __APPLICATION_EVENT_PROVIDER_DUMMY_H__ */