#include <dlfcn.h>

#include <mutex>

#include "utpl/common/debug.h"

#include "utpl/transcoder/mxlregistry.h"

// The types come from the MXL header when the build has it, so the compiler checks every call below
// against the real prototypes. Without the SDK the registry is a stub.
#if defined(__has_include)
#  if __has_include(<mxl/mxl.h>) && __has_include(<mxl/flow.h>)
#    include <mxl/mxl.h>
#    include <mxl/flow.h>
#    define UTPL_MXL_REGISTRY 1
#  endif
#endif

using namespace utpl;

#ifdef UTPL_MXL_REGISTRY

namespace {
    struct Api {
        mxlInstance (*createInstance)(const char *, const char *)                 = nullptr;
        mxlStatus   (*destroyInstance)(mxlInstance)                               = nullptr;
        mxlStatus   (*isFlowActive)(mxlInstance, const char *, bool *)            = nullptr;
        mxlStatus   (*garbageCollectFlows)(mxlInstance)                           = nullptr;
    };

    // Loaded once. The library is never unloaded: the GStreamer plugin has it open too.
    const Api *api() {
        static std::once_flag once;
        static Api            loaded;
        static bool           ok = false;

        std::call_once(once, [] {
            void *handle = dlopen("libmxl.so.1", RTLD_NOW | RTLD_GLOBAL);

            if (! handle) {
                handle = dlopen("libmxl.so", RTLD_NOW | RTLD_GLOBAL);
            }

            if (! handle) {
                DBG(("MXL registry: libmxl is not available ({})", dlerror()));
                return;
            }

            loaded.createInstance       = reinterpret_cast<decltype(loaded.createInstance)>(dlsym(handle, "mxlCreateInstance"));
            loaded.destroyInstance      = reinterpret_cast<decltype(loaded.destroyInstance)>(dlsym(handle, "mxlDestroyInstance"));
            loaded.isFlowActive         = reinterpret_cast<decltype(loaded.isFlowActive)>(dlsym(handle, "mxlIsFlowActive"));
            loaded.garbageCollectFlows  = reinterpret_cast<decltype(loaded.garbageCollectFlows)>(dlsym(handle, "mxlGarbageCollectFlows"));

            ok = loaded.createInstance && loaded.destroyInstance && loaded.isFlowActive && loaded.garbageCollectFlows;

            if (! ok) {
                WARN(("MXL registry: libmxl lacks a function the registry needs; flow checks are off"));
            }
        });

        return ok ? &loaded : nullptr;
    }

    // An instance for the length of one call, so that nothing is held between calls.
    class Instance {
        public:
            Instance(const Api &api, const std::string &domain) : _api(api) {
                this->_handle = api.createInstance(domain.c_str(), "");
            }

            ~Instance() {
                if (this->_handle) {
                    this->_api.destroyInstance(this->_handle);
                }
            }

            Instance(const Instance &) = delete;
            Instance &operator=(const Instance &) = delete;

            mxlInstance get() const {
                return this->_handle;
            }

        private:
            const Api  &_api;
            mxlInstance _handle = nullptr;
    };
}

bool transcoder::MxlRegistry::isAvailable() {
    return api() != nullptr;
}

transcoder::MxlRegistry::Writer transcoder::MxlRegistry::getWriter(const std::string &domain, const std::string &flowId) {
    const Api *a = api();

    if (! a || domain.empty() || flowId.empty()) {
        return Writer::UNKNOWN;
    }

    Instance instance(*a, domain);

    if (! instance.get()) {
        return Writer::UNKNOWN;
    }

    bool active = false;

    switch (a->isFlowActive(instance.get(), flowId.c_str(), &active)) {
        case MXL_STATUS_OK:
            return active ? Writer::ACTIVE : Writer::NONE;

        case MXL_ERR_FLOW_NOT_FOUND:
            return Writer::NONE;

        default:
            return Writer::UNKNOWN;
    }
}

bool transcoder::MxlRegistry::collectGarbage(const std::string &domain) {
    const Api *a = api();

    if (! a || domain.empty()) {
        return false;
    }

    Instance instance(*a, domain);

    return instance.get() && a->garbageCollectFlows(instance.get()) == MXL_STATUS_OK;
}

#else

bool transcoder::MxlRegistry::isAvailable() {
    return false;
}

transcoder::MxlRegistry::Writer transcoder::MxlRegistry::getWriter(const std::string &, const std::string &) {
    return Writer::UNKNOWN;
}

bool transcoder::MxlRegistry::collectGarbage(const std::string &) {
    return false;
}

#endif
