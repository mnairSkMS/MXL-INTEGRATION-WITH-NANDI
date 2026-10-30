#ifndef __APPLICATION_EVENT_PROVIDER_SQLITE_H__
#define __APPLICATION_EVENT_PROVIDER_SQLITE_H__

#include "utpl/transcoder/event/provider.h"
#include "application/event/provider/sqlite/model.h"

namespace app {
    class SqliteEventProvider : public utpl::transcoder::EventProvider {
        public:
            SqliteEventProvider(const std::string &dbPath);

        public:
            std::vector<std::unique_ptr<utpl::transcoder::Event>> getNextEvents(utpl::Timestamp notBefore, utpl::Timestamp notAfter) override;
            void markProcessed(const utpl::transcoder::Event &event, const std::string &errorMessage) override;
            bool isValid(const utpl::transcoder::Event &event) override;

            void start() override;
            void stop() override;

        private:
            void _handleSqliteException(const std::exception &ex);
            
            std::unique_ptr<utpl::transcoder::EventOverlay>         _parseOverlay(const EventModel &model);
            std::unique_ptr<utpl::transcoder::EventSqueeze>         _parseSqueeze(const EventModel &model);
            std::unique_ptr<utpl::transcoder::EventSlate>           _parseSlate(const EventModel &model);
            std::unique_ptr<utpl::transcoder::EventTimeSignal>      _parseTimeSignal(const EventModel &model);
            std::unique_ptr<utpl::transcoder::EventSpliceInsert>    _parseSpliceInsert(const EventModel &model);
            std::unique_ptr<utpl::transcoder::EventReturnToNetwork> _parseReturnToNetwork(const EventModel &model);

        private:
            std::string _dbPath;
    };
}

#endif /* __APPLICATION_EVENT_PROVIDER_SQLITE_H__ */