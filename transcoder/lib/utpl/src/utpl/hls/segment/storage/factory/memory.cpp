#include "utpl/common/debug.h"

#include "utpl/hls/segment/storage/factory/memory.h"

using namespace utpl;


class SegmentMemoryStorage : public hls::SegmentStorage {
    public:
        SegmentMemoryStorage(const std::string &name, const std::string &mimeType) : _name(name), _mimeType(mimeType) {
        }

        size_t read(void *buffer, off_t offset, size_t size) override {
            if (offset < this->_data.size()) {
                size_t toRead = size;

                toRead = std::min(toRead, this->_data.size() - offset);

                memcpy(buffer, this->_data.data() + offset, toRead);

                return toRead;
            }

            return 0;
        }

        size_t write(void *buffer, size_t size) override {
            this->_data.insert(this->_data.end(), (uint8_t *)buffer, ((uint8_t *)buffer) + size);

            return size;
        }

        size_t length() {
            return this->_data.size();
        }

        void dispose() override {
            DBG(("Disposing memory of segment '{}' storage object", this->_name));

            this->_data.clear();
        }

        std::string getMimeType() override {
            return this->_mimeType;
        }

    private:
        std::vector<uint8_t> _data;
        std::string _name;
        std::string _mimeType;
};

hls::SegmentMemoryStorageFactory::SegmentMemoryStorageFactory() {
}

std::unique_ptr<hls::SegmentStorage> hls::SegmentMemoryStorageFactory::create(const std::string &name, const std::string &mimeType) {
    return std::make_unique<SegmentMemoryStorage>(name, mimeType);
}
