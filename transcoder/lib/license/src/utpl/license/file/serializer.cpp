#include "utpl/license/file/serializer.h"

using namespace utpl;

license::LicenseFile license::LicenseFileSerializer::deserialize(std::istream &stream) {
    stream.seekg(0, std::ios::end);
                    
    size_t size = stream.tellg();
    
    std::vector<uint8_t> buffer(size, 0);
    
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(buffer.data()), size); 

    return this->deserialize(buffer);
}
