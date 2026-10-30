#include <nlohmann/json.hpp>

#include "utpl/common/debug.h"
#include "utpl/common/types/exception.h"
#include "utpl/common/utils.h"

#include "utpl/crypto/manager.h"

#include "utpl/license/config.h"
#include "utpl/license/exception.h"
#include "utpl/license/serializer/json.h"

using namespace utpl;

#define NODE_DATA "data"

#define KEY_ID           "id"
#define KEY_NAME         "name"
#define KEY_VALID_SINCE  "valid_since"
#define KEY_TTL          "ttl"
#define KEY_MAX_SESSIONS "max_sessions"
#define KEY_OFFLINE      "offline"

#define NODE_META      "meta"
#define KEY_CREATED_AT "created_at"

std::vector<uint8_t> license::JsonLicenseSerializer::serialize(const License &license, const crypto::Key &key) {
    std::vector<uint8_t> ret;

    {
        std::string jsonString;

        try {
            nlohmann::json json;

            {
                auto &model = license.getModel();
                auto &meta = license.getMetadata();

                json[NODE_DATA] = {
                    { KEY_ID,            model.getId()          },
                    { KEY_NAME,          model.getName()        },
                    { KEY_VALID_SINCE,   model.getValidSince()  },
                    { KEY_TTL,           model.getTtl()         },
                    { KEY_MAX_SESSIONS,  model.getMaxSessions() },
                    { KEY_OFFLINE,       model.isOffline()      }
                };

                json[NODE_META] = {
                    { KEY_CREATED_AT, meta.getCreationTime() }
                };
            }

            jsonString = json.dump();

        } catch (nlohmann::json::exception &ex) {
            throw license::GenericLicenseException("Unable to create json license data '" + std::string(ex.what()) + "'");
        }    

        {
            auto cipher = crypto::getCryptoManager().newCipher(
                UTPL_LICENSE_KEY_SECTION_CIPHER_ALGORITHM,
                UTPL_LICENSE_KEY_SECTION_CIPHER_MODE
            );

            cipher->init(true, key);
            auto encryptedData = cipher->final(jsonString.data(), jsonString.size());
            auto iv = cipher->getIv();
            
            auto base64 = utils::toBase64(encryptedData) + "." + utils::toBase64(iv);

            ret = std::vector<uint8_t>(base64.begin(), base64.end());
        }
    }

    return ret;
}

license::License license::JsonLicenseSerializer::deserialize(const std::vector<uint8_t> &data, const crypto::Key &key) {
    License license;

    std::vector<uint8_t> decryptedPayload;
    
    {
        auto stringData = std::string(data.begin(), data.end());
        auto parts = utils::split(stringData, '.');
        if (parts.size() != 2) {
            throw utpl::Exception("Invalid license data");
        }

        {
            auto ciphertext = utils::fromBase64(parts[0]);
            auto iv = utils::fromBase64(parts[1]);
            
            {
                crypto::CipherIvParameters params(iv);
            
                auto cipher = utpl::crypto::getCryptoManager().newCipher(
                    UTPL_LICENSE_KEY_SECTION_CIPHER_ALGORITHM,
                    UTPL_LICENSE_KEY_SECTION_CIPHER_MODE
                );

                cipher->init(false, key, &params);
                decryptedPayload = cipher->final(ciphertext);            
            }
        }
    }

    try {
        auto payloadJson = nlohmann::json::parse(decryptedPayload.begin(), decryptedPayload.end());
        
        {
            auto &model = license.getModel();
            auto &data = payloadJson[NODE_DATA];

            model.setId        (data[KEY_ID].get<std::string>());
            model.setName      (data[KEY_NAME].get<std::string>());
            model.setValidSince(data[KEY_VALID_SINCE].get<utpl::Timestamp>());
            model.setTtl       (data[KEY_TTL].get<utpl::Timestamp>());

            if (data.contains(KEY_MAX_SESSIONS)) {
                model.setMaxSessions(data[KEY_MAX_SESSIONS].get<size_t>());
            }

            if (data.contains(KEY_OFFLINE)) {
                model.setOffline(data[KEY_OFFLINE]);
                
            } else {
                // backward compatibility
                model.setOffline(true);
            }
        }

        {
            auto& meta = license.getMetadata();

            meta.setCreationTime(payloadJson[NODE_META][KEY_CREATED_AT].get<utpl::Timestamp>());
        }
    } catch (nlohmann::json::exception &ex) {
        throw license::GenericLicenseException("Unable to deserialize license data'" + std::string(ex.what()) + "'");
    }

    return license;
}