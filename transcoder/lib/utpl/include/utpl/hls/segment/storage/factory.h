#ifndef __UTPL_HLS_SEGMENT_STORAGE_FACTORY_H__
#define __UTPL_HLS_SEGMENT_STORAGE_FACTORY_H__

#include <memory>
#include <vector>

#include "utpl/common/utils/class.h"

#include "utpl/hls/segment/storage.h"

namespace utpl {
    namespace hls {
        class SegmentStorageFactory {
            UTPL_CLASS_INTERFACE(SegmentStorageFactory) {}
            
            public:
                virtual std::unique_ptr<SegmentStorage> create(const std::string &name, const std::string &mimeType) = 0;
        };
    }
}

#endif /* __UTPL_HLS_SEGMENT_STORAGE_FACTORY_H__ */