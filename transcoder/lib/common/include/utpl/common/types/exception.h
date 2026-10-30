#ifndef __UTPL_COMMON_TYPES_EXCEPTION_H__
#define __UTPL_COMMON_TYPES_EXCEPTION_H__

#include <stdexcept>
#include <string>

#include "utpl/common/types.h"
#include "utpl/common/utils/class.h"

#define UTPL_DECLARE_EXCEPTION(className, errorCode)      \
    class className: public utpl::Exception {             \
        public:                                           \
            explicit className(const std::string &msg) :  \
                utpl::Exception(errorCode, #className ": " + msg) {} \
    };                             

namespace utpl {
    class Exception : public std::runtime_error {
        UTPL_CLASS_ROF(Exception, {});

        public:
            Exception(Error code, const std::string &message) : runtime_error("Exception: code: " + std::to_string((int) code) + ", message: " + message), _code(code) {
            }

            explicit Exception(const std::string &message) : Exception(Error::UNKNOWN_ERROR, message) {
            }

            Error getCode() const {
                return this->_code;
            }

            const char *getMessage() const {
                return this->what();
            }

        private:
            Error _code;
    };

    UTPL_DECLARE_EXCEPTION(NotImplementedException,  Error::NOT_IMPLEMENTED);
    UTPL_DECLARE_EXCEPTION(NotSupportedException,    Error::NOT_SUPPORTED);
    UTPL_DECLARE_EXCEPTION(InvalidArgumentException, Error::INVALID_ARGUMENT);
    UTPL_DECLARE_EXCEPTION(NoFreeMemoryException,    Error::NO_FREE_MEMORY);
    UTPL_DECLARE_EXCEPTION(InvalidStateException,    Error::INVALID_STATE);
}

#endif /* __UTPL_COMMON_TYPES_EXCEPTION_H__ */