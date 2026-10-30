#ifndef __UTPL_ESAM_TYPES_H__
#define __UTPL_ESAM_TYPES_H__

namespace utpl {
    namespace esam
    {
        enum class Operation {
            NOOP,
            DELETE,
            REPLACE,
            CREATE,
            PRIVATE
        };

    } // namespace esam
    
} // utpl

#endif // __UTPL_ESAM_TYPES_H__