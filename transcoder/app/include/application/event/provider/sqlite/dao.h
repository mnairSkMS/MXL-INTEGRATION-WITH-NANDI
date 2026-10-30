#ifndef __APPLICATION_EVENT_PROVIDER_SQLITE_DAO_H__
#define __APPLICATION_EVENT_PROVIDER_SQLITE_DAO_H__

#include <SQLiteCpp/SQLiteCpp.h>

#include "application/event/provider/sqlite/model.h"

namespace app {
    class EventsDao {
        public:
            EventsDao(SQLite::Database &db);

            bool getInTimeRange(std::vector<EventModel> &events, utpl::Timestamp notBefore, utpl::Timestamp notAfter);
            bool getById(int64_t id, EventModel &model);
            bool updateReadTime(const EventModel &model);

            bool getAll(std::vector<EventModel> &events);
            bool update(const EventModel &model);

        private:
            void _handleException(const std::exception &ex);

        private:
            SQLite::Database &_db;
    };
}

#endif /* __APPLICATION_EVENT_PROVIDER_SQLITE_DAO_H__ */