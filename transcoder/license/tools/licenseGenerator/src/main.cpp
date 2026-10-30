#include <stdlib.h>
#include <assert.h>
#include <unordered_map>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <ctime>
#include <filesystem>

#include <boost/program_options.hpp>

#include "utpl/common/types/exception.h"

#include "utpl/common/logger.h"
#include "utpl/common/debug.h"
#include "utpl/common/time.h"
#include "utpl/common/utils.h"

#include "utpl/crypto/manager.h"

#include "utpl/license.h"
#include "utpl/license/config.h"
#include "utpl/license/exception.h"
#include "utpl/license/signature.h"
#include "utpl/license/serializer/json.h"
#include "utpl/license/file/serializer/json.h"


#define OPT_NAME_HELP         "help"
#define OPT_NAME_LICENSE_KEY  "license-key"
#define OPT_NAME_PRIVATE_KEY  "private-key"
#define OPT_NAME_START        "start"
#define OPT_NAME_TTL_DAYS     "ttl-days"
#define OPT_NAME_TTL_SEC      "ttl-seconds"
#define OPT_NAME_ID           "id"
#define OPT_NAME_OUT          "output"
#define OPT_NAME_NAME         "name"
#define OPT_NAME_MAX_SESSIONS "max-sessions"
#define OPT_NAME_OFFLINE      "offline"

#define DEF_OUT std::cerr

using namespace utpl;

struct CmdlineParameters {
    std::string licenseKeyFilePath;
    std::string startDate;
    Timestamp   ttl;
    std::string id;
    std::string name;
    std::string outPath;
    bool        offline;
    size_t      maxSessions;

    CmdlineParameters() {
        outPath = "transcoder.lsr"; // License Signing Request
        ttl     = 0;
    }
};

bool _isValidFile(std::string filePath) {
    return !filePath.empty() && std::filesystem::exists(filePath) && !std::filesystem::is_directory(filePath);
}

int main(int argc, char *argv[]) {
    auto ret = utpl::Error::NO_ERROR;

    {
        CmdlineParameters params;

        std::unique_ptr<utpl::crypto::PrivateKey> privateKey;
        std::unique_ptr<utpl::crypto::Key> encKey;

        auto &crypto = utpl::crypto::getCryptoManager();

        {
            auto *privateKeyHex = getenv(UTPL_LICENSE_ENV_KEY_PRIVATE_VAR_NAME);

            if (privateKeyHex != nullptr) {
                privateKey = crypto.loadPrivateKey(utpl::utils::fromHex(privateKeyHex));
            }
        }

        {
            auto *licenseKeyHex = getenv(UTPL_LICENSE_ENV_KEY_ENCRYPTION_VAR_NAME);

            if (licenseKeyHex != nullptr) {
                encKey = crypto.loadKey(utpl::utils::fromHex(licenseKeyHex));
            }
        }

        {
            namespace po = boost::program_options;

            po::options_description options("Allowed options");

            utpl::license::LicenseModel defaultModel;

            options.add_options()(OPT_NAME_HELP  ",h",
                "Produce help message."
            );

            options.add_options()(OPT_NAME_LICENSE_KEY  ",l", po::value<std::string>(&params.licenseKeyFilePath), 
                "Path to the license encryption key file. It can also be provided as a HEX string in the " UTPL_LICENSE_ENV_KEY_ENCRYPTION_VAR_NAME " environment variable."
            );

            options.add_options()(OPT_NAME_START ",s", po::value<std::string>(&params.startDate),
                "License issue date. Value should be in ISO-8601 format, e.g. 2025-01-01, 2025-02-05T14:50:00."
            );

            options.add_options()(OPT_NAME_TTL_DAYS ",t", po::value<int64_t>(),
                "The time after which the license will expire in days. Cannot be used with the -T option."
            );

            options.add_options()(OPT_NAME_TTL_SEC ",T", po::value<int64_t>(),
                "The time after which the license will expire in seconds. Cannot be used with the -t option."
            );

            options.add_options()(OPT_NAME_ID    ",i", po::value<std::string>(&params.id), 
                "License ID."
            );

            options.add_options()(OPT_NAME_NAME  ",n", po::value<std::string>(&params.name),
                "License name."
            );

            options.add_options()(OPT_NAME_OUT   ",o", po::value<std::string>(&params.outPath),
                "License output path (default: transcoder.lsr)."
            );

            options.add_options()(OPT_NAME_MAX_SESSIONS ",c", po::value<size_t>(&params.maxSessions)->default_value(defaultModel.getMaxSessions()),
                "Maximum concurrent sessions allowed (for online licenses)."
            );

            options.add_options()(OPT_NAME_OFFLINE ",O", po::value<bool>(&params.offline)->default_value(defaultModel.isOffline()),
                "Enable offline use of the license."
            );

            try {
                po::variables_map vm;

                {
                    po::positional_options_description pos_opts;

                    po::store(
                        po::command_line_parser(argc, argv).options(options).positional(pos_opts).run(), vm);
                }

                po::notify(vm);

                bool validated = false;

                do {
                    if (argc == 1 || vm.count(OPT_NAME_HELP) > 0) {
                        DEF_OUT << options << std::endl;
                        break;
                    }

                    if (! encKey && ! _isValidFile(params.licenseKeyFilePath)) {
                        DEF_OUT << "Empty encryption key path provided, or it points to a non-existing file, or the target is a directory." << std::endl;
                        break;
                    }

                    if (vm.count(OPT_NAME_TTL_DAYS) > 0) {
                        auto days = vm[OPT_NAME_TTL_DAYS].as<int64_t>();

                        if (vm.count(OPT_NAME_TTL_SEC) > 0) {
                            DEF_OUT << "You can define the TTL time either in seconds or in days." << std::endl;
                            break;
                        }

                        if (days < 0 || days > UTPL_LICENSE_TTL_MAX) {
                            DEF_OUT << "Invalid expiration time value provided." << std::endl;
                            break;
                        }

                        params.ttl = std::chrono::duration_cast<std::chrono::milliseconds>(Time::days(days)).count();

                    } else if (vm.count(OPT_NAME_TTL_SEC) > 0) {
                        auto seconds = vm[OPT_NAME_TTL_SEC].as<int64_t>();

                        if (seconds < 0 || seconds > (UTPL_LICENSE_TTL_MAX * (24LL * 60 * 60))) {
                            DEF_OUT << "Invalid expiration time value provided." << std::endl;
                            break;
                        }

                        params.ttl = std::chrono::duration_cast<std::chrono::milliseconds>(Time::seconds(seconds)).count();
                    }

                    if (params.ttl <= 0) {
                        DEF_OUT << "Expiration time can't be 0" << std::endl;
                        break;
                    }

                    validated = true;
                } while (0);

                if (! validated) {
                    return (int) utpl::Error::INVALID_ARGUMENT;
                }

            } catch (po::error const& e) {
                DEF_OUT << "Invalid options provided: " << e.what() << std::endl;
                
                return (int) utpl::Error::INVALID_ARGUMENT;
            }
        }

        try {
            utpl::license::License license;

            {
                auto &model = license.getModel();

                if (params.id.empty()) {
                    model.setId(utils::generateGUID());

                } else {
                    model.setId(params.id);
                }

                if (params.name.empty()) {
                    model.setName("License " + model.getId());

                } else {
                    model.setName(params.name);
                }

                if (params.startDate.empty()) {
                    model.setValidSince(UTPL_LICENSE_EPOCH_START);

                } else {
                    model.setValidSince(Time::fromIso8601(params.startDate));
                }
                
                model.setTtl(params.ttl);
                model.setOffline(params.offline);
                model.setMaxSessions(params.maxSessions);
            }

            {
                auto &meta = license.getMetadata();
                
                meta.setCreationTime(Time::getUTCTimestamp());
            }

            if (! encKey) {
                encKey = crypto.loadKey(params.licenseKeyFilePath);
            }

            if (encKey->size() == 0) {
                throw utpl::Exception("Empty encryption key provided");
            }

            {
                license::LicenseFile licenseFile;

                try {
                    auto licenseSerializer = std::make_unique<license::JsonLicenseSerializer>();
                
                    licenseFile.setData(
                        licenseSerializer->serialize(license, *encKey)
                    );
                } catch (std::exception &ex) {
                    throw utpl::Exception("Unable to create license data.");
                }

                {
                    std::vector<uint8_t> serialized;

                    try {
                        auto licenseFileSerializer = std::make_unique<license::JsonLicenseFileSerializer>();

                        serialized = licenseFileSerializer->serialize(licenseFile);
                    } catch (std::exception &ex) {
                        throw utpl::Exception("Unable to create license file.");
                    }
                    
                    {
                        std::ofstream outStream(params.outPath, std::ios::binary | std::ios::trunc);

                        if (! outStream.is_open()) {
                            throw utpl::Exception("Unable to open file " + params.outPath + " - " + std::string(strerror(errno)));
                        }

                        outStream.write(reinterpret_cast<const char *>(serialized.data()), serialized.size());
                        outStream.close();
                    }
                }
            }
            
            DEF_OUT << "License successfully generated: " << params.outPath << std::endl;

        } catch (const utpl::license::GenericLicenseException &ex) {
            ERR(("Cought license related exception: {}", ex.getMessage()));
        
        } catch (const utpl::Exception &ex) {
            ERR(("Cought Exception: {}", ex.getMessage()));
            ret = ex.getCode();

        } catch (const std::exception &ex) {
            ERR(("Cought a raw exception! ({})", ex.what()));
            ret = utpl::Error::UNKNOWN_ERROR;

        } catch (...) {
            ERR(("Cought an unknown exception!"));
            ret = utpl::Error::UNKNOWN_ERROR;
        }
    }

    return (int) ret;
}