#ifndef __UTPL_COMMON_UTILS_WRAPPER_H__
#define __UTPL_COMMON_UTILS_WRAPPER_H__

#include <string>

#include "utpl/common/utils/gstreamer.h"
#include "utpl/common/utils/class.h"


namespace utpl {
    namespace pipeline {
        template <class PtrClass>
        class GstWrapper {
            UTPL_CLASS_ROF(GstWrapper, {});

            public:
                GstWrapper(const PtrClass &ptr) : _ptr(ptr) {
                }

                const PtrClass *operator->() const {
                    return &_ptr;
                }

                PtrClass *operator->() {
                    return &_ptr;
                }

                operator bool() const {
				    return *this->_ptr;
			    }

                const PtrClass &operator*() const {
                    return this->_ptr;
                }

                PtrClass &operator*() {
                    return this->_ptr;
                }

                virtual const std::string &getName() const = 0;

            private:
                PtrClass _ptr;
        };
    }
}

#endif /* __UTPL_COMMON_UTILS_WRAPPER_H__ */