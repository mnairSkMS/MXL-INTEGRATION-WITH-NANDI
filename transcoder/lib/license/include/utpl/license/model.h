#ifndef __UTPL_LICENSE_MODEL_H__
#define __UTPL_LICENSE_MODEL_H__

#include <string>

#include "utpl/license/config.h"

namespace utpl {
    namespace license {
        class LicenseModel {
            public:
                LicenseModel() {
                    this->_ttl         = 0;
                    this->_validSince  = UTPL_LICENSE_EPOCH_START;
                    this->_maxSessions = 1;
                    this->_offline     = false;
                }

                const std::string &getId() const {
                    return this->_id;
                }

                void setId(const std::string &id) {
                    this->_id = id;
                }

                const std::string &getName() const {
                    return this->_name;
                }

                void setName(const std::string &name) {
                    this->_name = name;
                }

                Timestamp getValidSince() const {
                    return this->_validSince;
                }

                void setValidSince(Timestamp validSince) {
                    this->_validSince = validSince;
                }

                Timestamp getTtl() const {
                    return this->_ttl;
                }

                void setTtl(Timestamp ttl) {
                    this->_ttl = ttl;
                }
                
                bool isOffline() const {
                    return this->_offline;
                }

                void setOffline(bool offline) {
                    this->_offline = offline;
                }

                size_t getMaxSessions() const {
                    return this->_maxSessions;
                }

                void setMaxSessions(size_t maxSessions) {
                    this->_maxSessions = maxSessions;
                }

            private:
                std::string _id;
                std::string _name;
                bool        _offline;
                size_t      _maxSessions;
                
                Timestamp _validSince;
                Timestamp _ttl;
        };
    }
}

#endif /* __UTPL_LICENSE_MODEL_H__ */