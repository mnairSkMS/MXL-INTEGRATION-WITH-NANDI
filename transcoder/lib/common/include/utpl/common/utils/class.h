#ifndef __UTPL_COMMON_UTILS_CLASS_H__
#define __UTPL_COMMON_UTILS_CLASS_H__

#include <mutex>
#include <cstdlib>

#define UTPL_CLASS_INTERFACE(className)                   \
    public:                                               \
        className() = default;                            \
        className(className&&) = default;                 \
        className(const className&) = default;            \
        className& operator=(className&&) = default;      \
        className& operator=(const className&) = default; \
        virtual ~className()

#define UTPL_CLASS_NO_DEFAULT_CONSTRUCTORS(className) \
    private:                                          \
        className() = delete;                         \
        className(className&&) = delete;              \
        className(const className&) = delete

#define UTPL_CLASS_NOCOPY(className)                \
    private:                                        \
        className(className&&) = delete;            \
        className(const className&) = delete;       \
        className& operator=(className&&) = delete; \
        className& operator=(const className&) = delete

#define UTPL_CLASS_NOBUILD(className)               \
        className() = delete;                       \

#define UTPL_CLASS_NOBUILD_NOCOPY(className)        \
    private:                                        \
        className() = delete;                       \
        className(className&&) = delete;            \
        className(const className&) = delete;       \
        className& operator=(className&&) = delete; \
        className& operator=(const className&) = delete

// Rule of five
#define UTPL_CLASS_ROF(className, dtor_attributes)        \
    public:                                               \
        /** @cond nodoxygen */                            \
        className(className&&) = default;                 \
        className(const className&) = default;            \
        className& operator=(className&&) = default;      \
        className& operator=(const className&) = default; \
        virtual ~className() dtor_attributes              \
        /** @endcond */


#define UTPL_CLASS_DECLARE_SINGLETON(className)                         \
    UTPL_CLASS_NOCOPY(className);                                       \
    public:                                                             \
        /** Get the instance of the singleton of this class. */         \
        /** @return The instance of the singleton of this class. */     \
        static className& getInstance() {                               \
            if (_instance == nullptr) {                                 \
                std::call_once(_onceFlag, []() {                        \
                    _instanceInit();                                    \
                });                                                     \
            }                                                           \
            return *_instance;                                          \
        }                                                               \
    private:                                                            \
        className(); /* default constructor */                          \
        static className* volatile _instance;                           \
        static std::once_flag _onceFlag;                                \
        static void _instanceInit();                                    \
        static void _instanceCleanup()

#define UTPL_CLASS_IMPLEMENT_SINGLETON(className)           \
    void className::_instanceInit()                         \
    {                                                       \
        _instance = new className;                          \
        std::atexit(className::_instanceCleanup);           \
    }                                                       \
    void className::_instanceCleanup()                      \
    {                                                       \
        if (_instance != nullptr) {                         \
            delete _instance;                               \
            _instance = nullptr;                            \
        }                                                   \
    }                                                       \
    className* volatile className::_instance = nullptr;     \
    std::once_flag className::_onceFlag {}




#endif /* __UTPL_COMMON_UTILS_CLASS_H__ */