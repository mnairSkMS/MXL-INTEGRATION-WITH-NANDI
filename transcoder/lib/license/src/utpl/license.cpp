#include "utpl/license.h"

using namespace utpl;


license::LicenseModel &license::License::getModel() {
    return this->_model;
}

license::LicenseMetadata &license::License::getMetadata() {
    return this->_metadata;
}

const license::LicenseModel &license::License::getModel() const {
    return this->_model;    
}

const license::LicenseMetadata &license::License::getMetadata() const {
    return this->_metadata;
}
