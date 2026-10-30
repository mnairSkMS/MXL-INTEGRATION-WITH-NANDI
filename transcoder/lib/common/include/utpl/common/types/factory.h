#ifndef __UTPL_COMMON_TYPES_FACTORY_H__
#define __UTPL_COMMON_TYPES_FACTORY_H__

#include <memory>

#include "utpl/common/utils/class.h"

namespace utpl {
    template <class T, class P>
    class Factory {
        UTPL_CLASS_INTERFACE(Factory) {}

        virtual std::unique_ptr<T> create(const P &parameters) = 0;
    };
}

#endif /* __UTPL_COMMON_TYPES_FACTORY_H__ */