#ifndef __UTPL_LICENSE_FILE_H__
#define __UTPL_LICENSE_FILE_H__

#include <vector>
#include <cstdint>

namespace utpl {
    namespace license {
        class LicenseFile {
            public:
                const std::vector<uint8_t> &getSignature() const {
                    return this->_signature;
                }

                const std::vector<uint8_t> &getData() const {
                    return this->_data;
                }

                void setData(const std::vector<uint8_t> &data) {
                    this->_data = data;
                }

                void setSignature(const std::vector<uint8_t> &signature) {
                    this->_signature = signature;
                }
                
            private:
                std::vector<uint8_t> _signature;
                std::vector<uint8_t> _data;
        };
    }
}

#endif /* __UTPL_LICENSE_FILE_H__ */