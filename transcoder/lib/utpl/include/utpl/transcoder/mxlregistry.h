#ifndef __UTPL_TRANSCODER_MXLREGISTRY_H__
#define __UTPL_TRANSCODER_MXLREGISTRY_H__

#include <string>

namespace utpl {
    namespace transcoder {
        // What the engine may ask of the MXL domain about its flows, using only MXL v1.1.0 and the files of the
        // domain (there is no registry process): is there a writer on a flow, and remove flows nobody writes.
        //
        // libmxl is loaded when it is first needed (dlopen), not linked, so an engine built or run without the MXL
        // SDK still works: every call then answers "unknown" and does nothing.
        //
        // Both calls take an advisory lock on the flow's data file for an instant (MXL's own way of telling a live
        // writer from a dead one), so a writer that is being created at that very moment can fail to take its own
        // lock. The window is a few microseconds; it is why a clean-up runs once when the engine starts and not on a timer.
        class MxlRegistry {
            public:
                enum class Writer {
                    UNKNOWN, // libmxl is not available, the domain cannot be opened, or the flow id is not a UUID
                    NONE,    // the flow does not exist, or exists with no writer holding it
                    ACTIVE   // another process (or this one) holds the flow as its writer
                };

                // True when libmxl could be loaded.
                static bool isAvailable();

                static Writer getWriter(const std::string &domain, const std::string &flowId);

                // Removes the flows of the domain that no writer holds (left behind by a writer that crashed or
                // was killed). True when the clean-up ran; false when it could not.
                static bool collectGarbage(const std::string &domain);
        };
    }
}

#endif /* __UTPL_TRANSCODER_MXLREGISTRY_H__ */
