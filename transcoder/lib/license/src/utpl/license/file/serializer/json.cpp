#include <nlohmann/json.hpp>

#include "utpl/common/types/exception.h"
#include "utpl/common/utils.h"

#include "utpl/license/config.h"
#include "utpl/license/exception.h"
#include "utpl/license/file/serializer/json.h"

using namespace utpl;

static const std::string header = "TRANSCODER LICENSE";
static const std::string footer = "TRANSCODER LICENSE";

std::vector<uint8_t> license::JsonLicenseFileSerializer::serialize(const LicenseFile &file) {
    std::vector<uint8_t> ret;

    try {
        std::string jsonPayload;

        {
            nlohmann::json json;
            
            json["enc"] = utils::toBase64(file.getData());
            json["sig"] = utils::toBase64(file.getSignature());

            jsonPayload = json.dump();
        }

        auto pemString = utils::pemFormat(
            std::vector<uint8_t>(jsonPayload.begin(), jsonPayload.end()), header, footer
        );

        ret = std::vector<uint8_t>(pemString.begin(), pemString.end());

    } catch (nlohmann::json::exception &ex) {
        throw license::GenericLicenseException("Unable to create json license file");

    } catch (std::exception &ex) {
        throw license::GenericLicenseException("Unable to create json license file");
    }

    return ret;
}

license::LicenseFile license::JsonLicenseFileSerializer::deserialize(const std::vector<uint8_t> &data) {
    LicenseFile license;

    try {
        auto decodedData = utils::pemParse(std::string(reinterpret_cast<const char *>(data.data()), data.size()));
    
        auto json = nlohmann::json::parse(decodedData.begin(), decodedData.end());
        {
            auto enc = json["enc"].get<std::string>();
            auto sig = json["sig"].get<std::string>();

            license.setData(utils::fromBase64(enc));
            license.setSignature(utils::fromBase64(sig));
        }

    } catch (nlohmann::json::exception &ex) {
        throw utpl::InvalidArgumentException("Unable to deserialize license file '" + std::string(ex.what()) + "'");
    }

    return license;
}