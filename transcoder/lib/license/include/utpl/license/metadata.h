#ifndef __UTPL_LICENSE_METADATA_H__
#define __UTPL_LICENSE_METADATA_H__

#include "utpl/license/config.h"

namespace utpl {
    namespace license {
        class LicenseMetadata {
            public:
                LicenseMetadata() {
                    this->_creationTime = UTPL_LICENSE_EPOCH_START;
                }

                Timestamp getCreationTime() const {
                    return this->_creationTime;
                }

                void setCreationTime(Timestamp creationTime) {
                    this->_creationTime = creationTime;
                }

            private:
                Timestamp _creationTime;
        };
    }
}

#endif /* __UTPL_LICENSE_METADATA_H__ */