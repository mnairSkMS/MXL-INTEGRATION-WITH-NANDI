#ifndef __UTPL_LICENSE_EXCEPTION_H__
#define __UTPL_LICENSE_EXCEPTION_H__

#include "utpl/common/types/exception.h"

namespace utpl {
    namespace license {
        class GenericLicenseException : public Exception {
            public:
                explicit GenericLicenseException(const std::string &msg) :
                    utpl::Exception(Error::INTERRUPTED, "GenericLicenseException: " + msg) {}
        };
    }
}

#endif /* __UTPL_LICENSE_EXCEPTION_H__ */