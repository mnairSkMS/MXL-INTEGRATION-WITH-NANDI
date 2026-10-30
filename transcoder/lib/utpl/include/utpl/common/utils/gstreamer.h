#ifndef __UTPL_COMMON_UTILS_GSTREAMER_H__
#define __UTPL_COMMON_UTILS_GSTREAMER_H__

#include <string>
#include <cstdint>

#include <gst/gst.h>


// taken from gst-plugins-bad/gst/mpegtsdemux/gstmpegdefs.h
#define GST_UTILS_CLOCK_BASE 9LL
#define GST_UTILS_GSTTIME_TO_MPEGTIME(time) \
    (((time) > 0 ? (gint64) 1 : (gint64) -1) * \
    (gint64) gst_util_uint64_scale (ABS(time), GST_UTILS_CLOCK_BASE, GST_MSECOND/10))

#define GST_UTILS_MPEGTIME_TO_GSTTIME(t) ((t) * (uint64_t)100000 / GST_UTILS_CLOCK_BASE)


#define UTPL_GST_PTR_GEN_DEF(GstType, refMethod, unrefMethod) \
    class GstType ## Ptr { \
        public: \
            static const GstType ## Ptr NULLPTR; \
        private: \
    \
            GstType ## Ptr(GstType *ptr, bool takeOwnership) : GstType ## Ptr() { \
                this->_ptr = ptr; \
                if (takeOwnership) { \
                    this->ref(); \
                } else { \
                    this->acquire(); \
                } \
            } \
    \
        public: \
            static GstType ## Ptr take(GstType *ptr) { \
                return GstType ## Ptr(ptr, true); \
            } \
    \
            static GstType ## Ptr wrap(GstType *ptr) { \
                return GstType ## Ptr(ptr, false); \
            } \
    \
            GstType ## Ptr() { \
                this->_ptr      = nullptr; \
                this->_refCount = 0; \
            } \
    \
    \
            GstType ## Ptr(const GstType ## Ptr &other) : GstType ## Ptr(other._ptr, true) { \
            } \
    \
            GstType ## Ptr &operator=(GstType ## Ptr &other) { \
                this->unref(); \
                this->_ptr = other._ptr; \
                this->_refCount = 0; \
                this->ref(); \
    \
                return *this; \
            } \
            GstType ## Ptr &operator=(const GstType ## Ptr &other) { \
                this->unref(); \
                this->_ptr = other._ptr; \
                this->_refCount = 0; \
                this->ref(); \
    \
                return *this; \
            } \
    \
            virtual ~GstType ## Ptr() { \
                this->unref(); \
            } \
    \
            void ref() { \
                if (this->acquire()) { \
                    refMethod(this->_ptr); \
                } \
            } \
    \
            void unref() { \
                if (this->release()) { \
                    if (this->_refCount == 0) { \
                        unrefMethod(this->_ptr); \
                        this->_ptr = nullptr; \
                    } \
                } \
            } \
    \
            bool acquire() { \
                if (this->_ptr) { \
                    this->_refCount++; \
                    return true; \
                } \
    \
                return false; \
            } \
    \
            bool release() { \
                if (this->_ptr && this->_refCount > 0) { \
                    this->_refCount--; \
                    return true; \
                } \
    \
                return false; \
            } \
    \
            GstType *get() const { \
                return this->_ptr; \
            } \
    \
            GstType *operator*() const { \
                return this->_ptr; \
            } \
    \
            bool operator==(const GstType ## Ptr &other) { \
                return this->_ptr == other._ptr; \
            } \
    \
            operator bool() const { \
				return this->_ptr != nullptr; \
			} \
    \
        private: \
            GstType *_ptr; \
            int      _refCount; \
    }
    

    #define UTPL_GST_PTR_GEN_IMPL(GstType) \
        const GstType ## Ptr GstType ## Ptr::NULLPTR = GstType ## Ptr::wrap(nullptr);


namespace utpl {
    namespace pipeline {
        static const char *elementName(GstElement *el) {
            return GST_ELEMENT_NAME(el);
        }

        static  const char *padName(GstPad *pad) {
            return GST_PAD_NAME(pad);
        }

        static const char *capsName(GstCaps *caps) {
            return nullptr;
        }

        class ScteRunningTimeMap {
            public:
                ScteRunningTimeMap(const GstStructure *structure) {
                    if (structure) {
                        this->_str = gst_structure_copy(structure);

                    } else {
                        this->_str = gst_structure_new_empty("running-time-map");
                    }

                    this->_allocated = true;
                }

                ScteRunningTimeMap(GstStructure *structure) {
                    this->_str = structure;

                    if (! this->_str) {
                        this->_str       = gst_structure_new_empty("running-time-map");
                        this->_allocated = true;

                    } else {
                        this->_allocated = false;
                    }
                }

                ~ScteRunningTimeMap() {
                    if (this->_allocated) {
                        gst_structure_free(this->_str);
                    }
                }

                GstStructure *get() {
                    return this->_str;
                }

                GstStructure *release() {
                    auto *ret = this->_str;

                    this->_str       = nullptr;
                    this->_allocated = false;

                    return ret;
                }

                GstClockTime getTimeSignalTime() const {
                    return this->_getTime(_timeSignalName());
                }

                GstClockTime getSpliceEventTime(uint32_t eventId) const {
                    return this->_getTime(_spliceEventIdToName(eventId));
                }

                void setSpliceEventTime(uint32_t eventId, GstClockTime ts) {
                    auto fieldName = _spliceEventIdToName(eventId);

                    gst_structure_set(this->_str, fieldName.c_str(), G_TYPE_UINT64, ts, nullptr);
                }

                void setTimeSignalTime(GstClockTime ts) {
                    gst_structure_set(this->_str, _timeSignalName().c_str(), G_TYPE_UINT64, ts, nullptr);
                }

                uint64_t getPtsOffset() const {
                    uint64_t ret;

                    if (! gst_structure_get_uint64(this->_str, "mpeg-pts-offset", &ret)) {
                        ret = 0;
                    }

                    return ret;
                }

            private:
                GstClockTime _getTime(const std::string &name) const {
                    GstClockTime ret = GST_CLOCK_TIME_NONE;

                    gst_structure_get_uint64(this->_str, name.c_str(), &ret);

                    return ret;
                }

                static std::string _spliceEventIdToName(uint32_t id) {
                    return "event-" + std::to_string(id) + "-splice-time";
                }

                static std::string _timeSignalName() {
                    return "splice-time";
                }
                
            private:
                GstStructure *_str;
                bool          _allocated;
        };

        UTPL_GST_PTR_GEN_DEF(GstElement,  gst_object_ref, gst_object_unref);
        UTPL_GST_PTR_GEN_DEF(GstBin,      gst_object_ref, gst_object_unref);
        UTPL_GST_PTR_GEN_DEF(GstPad,      gst_object_ref, gst_object_unref);
        UTPL_GST_PTR_GEN_DEF(GstGhostPad, gst_object_ref, gst_object_unref);
        UTPL_GST_PTR_GEN_DEF(GstCaps,     gst_caps_ref,   gst_caps_unref);
    }
}

#endif /* __UTPL_COMMON_UTILS_GSTREAMER_H__ */
