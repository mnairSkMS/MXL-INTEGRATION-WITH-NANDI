#ifndef __UTPL_TRANSCODER_EVENT_PROVIDER_H__
#define __UTPL_TRANSCODER_EVENT_PROVIDER_H__

#include <memory>

#include "utpl/common/utils/class.h"

#include "utpl/transcoder/event.h"

namespace utpl {
    namespace transcoder {
        class EventProvider {
            UTPL_CLASS_INTERFACE(EventProvider) {};

            public:
                virtual std::vector<std::unique_ptr<Event>> getNextEvents(utpl::Timestamp notBefore, utpl::Timestamp notAfter) = 0;

                virtual void markProcessed(const Event &event, const std::string &errorMessage) = 0;

                virtual bool isValid(const Event &event) = 0;

                virtual void start() = 0;
                virtual void stop() = 0;
        };
    }
}

#endif /* __UTPL_TRANSCODER_EVENT_PROVIDER_H__ */