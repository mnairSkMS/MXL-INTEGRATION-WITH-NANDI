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


#define OPT_NAME_HELP        "help"
#define OPT_NAME_LICENSE_KEY "license-key"
#define OPT_NAME_PUBLIC_KEY  "public-key"
#define OPT_NAME_INPUT_FILE  "input-file"

#define DEF_OUT std::cerr

struct CmdlineParameters {
    std::string licenseKeyFilePath;
    std::string publicKeyFilePath;
    std::string inputFilePath;
};

using namespace utpl;

bool _isValidFile(std::string filePath) {
    return !filePath.empty() && std::filesystem::exists(filePath) && !std::filesystem::is_directory(filePath);
}

int main(int argc, char *argv[]) {
    auto ret = utpl::Error::NO_ERROR;

    {
        CmdlineParameters params;
        auto &crypto = utpl::crypto::getCryptoManager();

        std::unique_ptr<utpl::crypto::PublicKey> publicKey;
        std::unique_ptr<utpl::crypto::Key> encKey;
        int ttl = 0;

        {
            auto *publicKeyHex = getenv(UTPL_LICENSE_ENV_KEY_PUBLIC_VAR_NAME);

            if (publicKeyHex != nullptr) {
                publicKey = crypto.loadPublicKey(utpl::utils::fromHex(publicKeyHex));
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

            options.add_options()(OPT_NAME_HELP ",h",
                "Produce help message."
            );

            options.add_options()(OPT_NAME_LICENSE_KEY ",l", po::value<std::string>(&params.licenseKeyFilePath), 
                "Path to the license encryption key file. It can also be provided as a HEX string in the " UTPL_LICENSE_ENV_KEY_ENCRYPTION_VAR_NAME " environment variable."
            );

            options.add_options()(OPT_NAME_PUBLIC_KEY ",p", po::value<std::string>(&params.publicKeyFilePath), 
                "Path to the public key file. It can also be provided as a HEX string in the " UTPL_LICENSE_ENV_KEY_PUBLIC_VAR_NAME " environment variable."
            );

            options.add_options()(OPT_NAME_INPUT_FILE ",i", po::value<std::string>(&params.inputFilePath), 
                "Input license file path."
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

                do
                {
                    if (argc == 1 || vm.count(OPT_NAME_HELP) > 0) {
                        DEF_OUT << options << std::endl;
                        break;
                    }

                    if (!publicKey && !_isValidFile(params.publicKeyFilePath))
                    {
                        DEF_OUT << "Empty public key path provided, or it points to a non-existing file, or the target is a directory." << std::endl;
                        break;
                    }
                    

                    if (!encKey && !_isValidFile(params.licenseKeyFilePath))
                    {
                        DEF_OUT << "Empty encryption key path provided, or it points to a non-existing file, or the target is a directory." << std::endl;
                        break;
                    }


                    if (!_isValidFile(params.inputFilePath))
                    {
                        DEF_OUT << "Empty input file path provided, or it points to a non-existing file, or the target is a directory." << std::endl;
                        break;
                    }
                
                    validated = true;
                } while (0);

                if (!validated)
                {
                    return (int) utpl::Error::INVALID_ARGUMENT;
                }
                
            } catch (po::error const& e) {
                DEF_OUT << "Invalid options provided: " << e.what() << std::endl;
                
                return (int) utpl::Error::INVALID_ARGUMENT;
            }
        }

        try {
            license::LicenseFile licenseFile;

            if (! encKey) {
                encKey = crypto.loadKey(params.licenseKeyFilePath);
            }

            if (! publicKey) {
                publicKey = crypto.loadPublicKey(params.publicKeyFilePath);
            }

            std::string signatureStatus = "Not signed";    

            {
                {
                    std::unique_ptr<license::LicenseFileSerializer> fileSerializer = std::make_unique<license::JsonLicenseFileSerializer>();
                    
                    std::ifstream licenseStream(params.inputFilePath);
                 
                    licenseFile = fileSerializer->deserialize(licenseStream);
                }
                
                auto signature = licenseFile.getSignature();
                if (! signature.empty()) {                
                    utpl::license::LicenseSignature s;
                    
                    signatureStatus = s.verify(licenseFile.getData(), licenseFile.getSignature(), *publicKey) ? "Valid" : "Invalid";
                }
            }

            static const size_t labelLength = 20;

            try {                
                auto serializer = std::make_unique<license::JsonLicenseSerializer>();
                auto license = serializer->deserialize(licenseFile.getData(), *encKey);
                
                auto &model = license.getModel();
                auto &meta = license.getMetadata();

                DEF_OUT << std::left << std::setw(labelLength) << "Id:"            << model.getId()                                             << std::endl;
                DEF_OUT << std::left << std::setw(labelLength) << "Name:"          << model.getName()                                           << std::endl;
                DEF_OUT << std::left << std::setw(labelLength) << "Start date:"    << Time::toUTCString(model.getValidSince())                  << std::endl;
                DEF_OUT << std::left << std::setw(labelLength) << "Expiry date:"   << Time::toUTCString(model.getValidSince() + model.getTtl()) << std::endl;
                DEF_OUT << std::left << std::setw(labelLength) << "Creation date:" << Time::toUTCString(meta.getCreationTime())                 << std::endl;
                DEF_OUT << std::left << std::setw(labelLength) << "Max sessions:"  << model.getMaxSessions()                                    << std::endl;
                DEF_OUT << std::left << std::setw(labelLength) << "Offline:"       << std::boolalpha << model.isOffline()                       << std::endl;
            } catch (const std::exception &e) {
                ERR(("Unable to decrypt license payload: {}", e.what()));

                DEF_OUT << "Unable to decrypt the license payload data - the encryption key is incorrect or the license is damaged." << std::endl;
            }

            DEF_OUT << std::left << std::setw(labelLength) << "Signature:" << signatureStatus << std::endl;

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