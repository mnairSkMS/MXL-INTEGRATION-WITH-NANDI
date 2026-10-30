#ifndef __UTPL_HLS_SEGMENT_STORAGE_FACTORY_MEMORY_H__
#define __UTPL_HLS_SEGMENT_STORAGE_FACTORY_MEMORY_H__

#include <memory>
#include <vector>

#include "utpl/common/utils/class.h"

#include "utpl/hls/segment/storage/factory.h"

namespace utpl {
    namespace hls {
        class SegmentMemoryStorageFactory : public SegmentStorageFactory {
            public:
                SegmentMemoryStorageFactory();
                
                std::unique_ptr<SegmentStorage> create(const std::string &name, const std::string &mimeType) override;
        };
    }
}

#endif /* __UTPL_HLS_SEGMENT_STORAGE_FACTORY_MEMORY_H__ */