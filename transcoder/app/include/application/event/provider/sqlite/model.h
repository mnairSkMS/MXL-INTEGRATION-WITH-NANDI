#ifndef __APPLICATION_EVENT_PROVIDER_SQLITE_MODEL_H__
#define __APPLICATION_EVENT_PROVIDER_SQLITE_MODEL_H__

#include <nlohmann/json.hpp>

#include "utpl/common/time.h"

namespace app {
    class EventModel {
        public:
            enum class Type {
                UNKNOWN,

                IMAGE_OVERLAY,
                SQUEEZE,
                SLATE,
                SCTE35_TIMESIGNAL,
                SCTE35_SPLICEINSERT,
                SCTE35_RETURNTONETWORK
            };

        public:
            EventModel() {
                this->_id           = -1;
                this->_type         = Type::UNKNOWN;
                this->_time         = 0;
                this->_creationTime = 0;
                this->_readTime     = 0;
            }

            int64_t getId() const {
                return this->_id;
            }

            void setId(int64_t id) {
                this->_id = id;
            }

            Type getType() const {
                return this->_type;
            }

            void setType(Type type) {
                this->_type = type;
            }

            const nlohmann::json &getDetails() const {
                return this->_details;
            }

            void setDetails(const nlohmann::json &json) {
                this->_details = json;
            }

            utpl::Timestamp getTime() const {
                return this->_time;
            }

            void setTime(utpl::Timestamp ts) {
                this->_time = ts;
            }

            utpl::Timestamp getEndTime() const {
                return this->_endTime;
            }

            void setEndTime(utpl::Timestamp ts) {
                this->_endTime = ts;
            }

            utpl::Timestamp getCreationTime() const {
                return this->_creationTime;
            }

            void setCreationTime(utpl::Timestamp ts) {
                this->_creationTime = ts;
            }

            utpl::Timestamp getReadTime() const {
                return this->_readTime;
            }

            void setReadTime(utpl::Timestamp ts) {
                this->_readTime = ts;
            }

        private:
            int64_t         _id;
            Type            _type;
            nlohmann::json  _details;
            utpl::Timestamp _time;
            utpl::Timestamp _creationTime;
            utpl::Timestamp _readTime;
            utpl::Timestamp _endTime;

    };
}

#endif /* __APPLICATION_EVENT_PROVIDER_SQLITE_MODEL_H__ */