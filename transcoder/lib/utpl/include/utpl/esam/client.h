#ifndef __UTPL_ESAM_CLIENT_H__
#define __UTPL_ESAM_CLIENT_H__

#include <cpr/cpr.h>

#include "utpl/transcoder/types.h"
#include "utpl/esam/model.h"
#include "utpl/esam/types.h"

namespace utpl {
    namespace esam {
        struct SignalParameters {
            std::vector<uint8_t> sectionData;
            utpl::Timestamp acquisitionTime;
            utpl::Timestamp signalTimestamp;
            uint64_t pts;

            SignalParameters() : acquisitionTime(0), signalTimestamp(0), pts(0) {}
        };

        class EventResponse {
            public:
                struct ConditioningInfo {
                    Timestamp duration; // in milliseconds
                    Timestamp startOffset;

                    ConditioningInfo() : duration(0), startOffset(0) {}
                };

            public:
                EventResponse() : mode(Operation::NOOP) {}
                EventResponse(Operation mode) : mode(mode) {}
                virtual ~EventResponse() {}
                
            public:
                Operation mode;
                ConditioningInfo conditioningInfo;
                std::vector<uint8_t> sectionData;
        };

        class Client {
            public:
                Client();
                Client(const std::string& endpoint);
                virtual ~Client();

                void setEndpoint(const std::string& endpoint);
                void setTimeout(utpl::Timestamp timeout);
                void setAcquisitionPointId(const std::string& id);
                void signalEvent(const SignalParameters& signalParams, EventResponse& response);

                std::future<void> signalEventAsync(const SignalParameters& signalParams, std::function<void(EventResponse &response, bool success)> callback);

            private:
                cpr::Response _sendSignalProcessingEvent(const SignalProcessingEvent& event);

                Timestamp _parseISO8601Duration(const std::string& iso8601Duration) const;

                void _dump(const SignalProcessingNotification& notification) const;

            private:
                cpr::Session _session;
                std::string  _acquisitionPointId;
        };
        
    } // namespace esam   
} // namespace utpl

#endif /* __UTPL_ESAM_CLIENT_H__ */
