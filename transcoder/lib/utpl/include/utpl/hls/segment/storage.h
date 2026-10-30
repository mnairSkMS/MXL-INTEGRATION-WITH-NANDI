#ifndef __UTPL_HLS_SEGMENT_STORAGE_H__
#define __UTPL_HLS_SEGMENT_STORAGE_H__

#include <vector>
#include <string>
#include <set>

#include <gst/gst.h>

#include "utpl/common/utils/class.h"

namespace utpl {
    namespace hls {
        class SegmentStorage {
            UTPL_CLASS_INTERFACE(SegmentStorage) {}
            
            public:
                virtual size_t read(void *buffer, off_t offset, size_t size) = 0;
                virtual size_t write(void *buffer, size_t size) = 0;
                virtual size_t length() = 0;

                virtual std::string getMimeType() = 0;

                virtual void dispose() = 0;

                virtual size_t write(GstBuffer *buffer) {
                    size_t ret = 0;

                    if (buffer) {
                        GstBufferMapInfo info;
                        
                        if (gst_buffer_map(buffer, &info, GST_MAP_READ)) {
                            ret += this->write(info.data, info.size);

                            gst_buffer_unmap(buffer, &info);
                        }
                    }

                    return ret;
                }

                virtual size_t write(GstBufferList *bufferList, const std::set<int> &ignoredPositions = {}) {
                    size_t ret = 0;

                    if (bufferList) {
                        for (int i = 0; i < gst_buffer_list_length(bufferList); i++) {
                            if (ignoredPositions.find(i) != ignoredPositions.end()) {
                                continue;
                            }

                            ret += this->write(gst_buffer_list_get(bufferList, i));
                        }
                    }

                    return ret;
                }
        };
    }
}

#endif /* __UTPL_HLS_SEGMENT_STORAGE_H__ */