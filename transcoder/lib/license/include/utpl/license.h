#ifndef __UTPL_LICENSE_H__
#define __UTPL_LICENSE_H__

#include "utpl/license/model.h"
#include "utpl/license/metadata.h"

namespace utpl {
    namespace license {
        class License {
            public:
                LicenseModel     &getModel();
                LicenseMetadata  &getMetadata();

                const LicenseModel     &getModel() const;
                const LicenseMetadata  &getMetadata() const;
                
            private:
                LicenseModel     _model;
                LicenseMetadata  _metadata;
        };
    }
}

#endif /* __UTPL_LICENSE_H__ */