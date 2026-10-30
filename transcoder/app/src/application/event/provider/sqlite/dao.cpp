#include "utpl/common/debug.h"

#include "application/event/provider/sqlite/dao.h"


#define TABLE_NAME_EVENTS "events"

#define TABLE_EVENTS_COLUMN_NAME_ID            "id"
#define TABLE_EVENTS_COLUMN_NAME_TYPE          "type"
#define TABLE_EVENTS_COLUMN_NAME_TIME          "time"
#define TABLE_EVENTS_COLUMN_NAME_END_TIME      "end_time"
#define TABLE_EVENTS_COLUMN_NAME_DETAILS       "details"
#define TABLE_EVENTS_COLUMN_NAME_CREATION_TIME "creation_time"
#define TABLE_EVENTS_COLUMN_NAME_READ_TIME     "read_time"

#define EVENT_TYPE_STR_OVERLAY         "image_overlay"
#define EVENT_TYPE_STR_SQUEEZE         "squeeze"
#define EVENT_TYPE_STR_SLATE           "slate"
#define EVENT_TYPE_STR_TIMESIGNAL      "scte35_timesignal"
#define EVENT_TYPE_STR_SPLICEINSERT    "scte35_spliceinsert"
#define EVENT_TYPE_STR_RETURNTONETWORK "scte35_returntonetwork"


static app::EventModel::Type fromString(const std::string &val) {
    if (val == EVENT_TYPE_STR_OVERLAY) {
        return app::EventModel::Type::IMAGE_OVERLAY;

    } else if (val == EVENT_TYPE_STR_SQUEEZE) {
        return app::EventModel::Type::SQUEEZE;
        
    } else if (val == EVENT_TYPE_STR_SLATE) {
        return app::EventModel::Type::SLATE;

    } else if (val == EVENT_TYPE_STR_TIMESIGNAL) {
        return app::EventModel::Type::SCTE35_TIMESIGNAL;

    } else if (val == EVENT_TYPE_STR_SPLICEINSERT) {
        return app::EventModel::Type::SCTE35_SPLICEINSERT;

    } else if (val == EVENT_TYPE_STR_RETURNTONETWORK) {
        return app::EventModel::Type::SCTE35_RETURNTONETWORK;
    }

    return app::EventModel::Type::UNKNOWN;
}


static std::string toString(app::EventModel::Type type) {
    switch (type) {  
        case app::EventModel::Type::IMAGE_OVERLAY:          return EVENT_TYPE_STR_OVERLAY;
        case app::EventModel::Type::SQUEEZE:                return EVENT_TYPE_STR_SQUEEZE;
        case app::EventModel::Type::SLATE:                  return EVENT_TYPE_STR_SLATE;
        case app::EventModel::Type::SCTE35_TIMESIGNAL:      return EVENT_TYPE_STR_TIMESIGNAL;
        case app::EventModel::Type::SCTE35_SPLICEINSERT:    return EVENT_TYPE_STR_SPLICEINSERT;
        case app::EventModel::Type::SCTE35_RETURNTONETWORK: return EVENT_TYPE_STR_RETURNTONETWORK;
        case app::EventModel::Type::UNKNOWN:                return "unknown";
    }

    ERR(("Not supported event type {}", (int) type));

    return "unknown";
}


static void _dbToModel(SQLite::Statement &statement, app::EventModel &model) {
    model.setId(
        statement.getColumn(TABLE_EVENTS_COLUMN_NAME_ID).getInt64()
    );

    model.setType(
        fromString(statement.getColumn(TABLE_EVENTS_COLUMN_NAME_TYPE).getString())
    );

    model.setDetails(
        nlohmann::json::parse(
            statement.getColumn(TABLE_EVENTS_COLUMN_NAME_DETAILS).getString()
        )
    );

    model.setTime(
        utpl::Time::fromSqliteDateTime(
            statement.getColumn(TABLE_EVENTS_COLUMN_NAME_TIME).getString()
        )
    );

    model.setEndTime(
        utpl::Time::fromSqliteDateTime(
            statement.getColumn(TABLE_EVENTS_COLUMN_NAME_TIME).getString()
        )
    );

    model.setCreationTime(
        utpl::Time::fromSqliteDateTime(
            statement.getColumn(TABLE_EVENTS_COLUMN_NAME_CREATION_TIME).getString()
        )
    );

    model.setReadTime(
        utpl::Time::fromSqliteDateTime(
            statement.getColumn(TABLE_EVENTS_COLUMN_NAME_READ_TIME).getString()
        )
    );
}

app::EventsDao::EventsDao(SQLite::Database &db) : _db(db) {
}

bool app::EventsDao::getById(int64_t id, EventModel &model) {
    bool ret = true;

    try {
        SQLite::Statement query(this->_db, "SELECT * FROM " TABLE_NAME_EVENTS " WHERE " TABLE_EVENTS_COLUMN_NAME_ID "=?");

        query.bind(1, id);

        if (query.executeStep()) {
            _dbToModel(query, model);

        } else {
            ret = false;
        }

    } catch (const std::exception &ex) {
        this->_handleException(ex);

        ret = false;
    }

    return ret;
}

bool app::EventsDao::getInTimeRange(std::vector<EventModel> &events, utpl::Timestamp notBefore, utpl::Timestamp notAfter) {
    bool ret = true;

    try {
        SQLite::Statement query(this->_db, "SELECT * FROM " TABLE_NAME_EVENTS " WHERE datetime(time) >= datetime(?) AND datetime(time) <= datetime(?)");

        events.clear();

        query.bind(1, utpl::Time::toSqliteDateTime(notBefore));
        query.bind(2, utpl::Time::toSqliteDateTime(notAfter));

        while (query.executeStep()) {
            EventModel item;

            _dbToModel(query, item);

            events.push_back(item);
        }
    } catch (const std::exception &ex) {
        this->_handleException(ex);

        ret = false;
    }

    return ret;
}

bool app::EventsDao::getAll(std::vector<EventModel> &events) {
    bool ret = true;

    try {
        SQLite::Statement stmt(this->_db, "SELECT * from " TABLE_NAME_EVENTS);

        events.clear();

        while (stmt.executeStep()) {
            EventModel item;

            _dbToModel(stmt, item);

            events.push_back(item);
        }
        
    } catch (const std::exception &ex) {
        this->_handleException(ex);

        ret = false;
    }

    return ret;
}


bool app::EventsDao::update(const EventModel &model) {
    bool ret = true;

    try {
        SQLite::Statement stmt(this->_db, "UPDATE " TABLE_NAME_EVENTS " SET "
            TABLE_EVENTS_COLUMN_NAME_CREATION_TIME " = ?, "
            TABLE_EVENTS_COLUMN_NAME_DETAILS " = ?, "
            TABLE_EVENTS_COLUMN_NAME_READ_TIME " = ?, "
            TABLE_EVENTS_COLUMN_NAME_TIME " = ?, "
            TABLE_EVENTS_COLUMN_NAME_END_TIME " = ?, "
            TABLE_EVENTS_COLUMN_NAME_TYPE " = ?"
            " WHERE " TABLE_EVENTS_COLUMN_NAME_ID " = ?"
        );

        stmt.bind(1, utpl::Time::toSqliteDateTime(model.getCreationTime()));
        stmt.bind(2, nlohmann::to_string(model.getDetails()));
        stmt.bind(3, utpl::Time::toSqliteDateTime(model.getReadTime()));
        stmt.bind(4, utpl::Time::toSqliteDateTime(model.getTime()));
        stmt.bind(5, utpl::Time::toSqliteDateTime(model.getEndTime()));
        stmt.bind(6, toString(model.getType()));
        stmt.bind(7, model.getId());

        if (stmt.exec() != 1) {
            ERR(("Cannot update event model in the database!"));

            ret = false;
        }

    } catch (const std::exception &ex) {
        this->_handleException(ex);

        ret = false;
    }

    return ret;
}


bool app::EventsDao::updateReadTime(const EventModel &model) {
    bool ret = true;

    try {
        SQLite::Statement stmt(this->_db, "UPDATE " TABLE_NAME_EVENTS " SET "
            TABLE_EVENTS_COLUMN_NAME_READ_TIME " = ?"
            " WHERE " TABLE_EVENTS_COLUMN_NAME_ID " = ?"
        );

        stmt.bind(1, utpl::Time::toSqliteDateTime(model.getReadTime()));
        stmt.bind(2, model.getId());

        if (stmt.exec() != 1) {
            ERR(("Cannot update event model in the database!"));

            ret = false;
        }

    } catch (const std::exception &ex) {
        this->_handleException(ex);

        ret = false;
    }

    return ret;
}


void app::EventsDao::_handleException(const std::exception &ex) {
    ERR(("Cought SQLite exception! '{}'", ex.what()));
}