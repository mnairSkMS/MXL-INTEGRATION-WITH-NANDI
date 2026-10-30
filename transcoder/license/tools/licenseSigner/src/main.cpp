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
#define OPT_NAME_PRIVATE_KEY "private-key"
#define OPT_NAME_INPUT_FILE  "input-file"
#define OPT_NAME_OUT         "output-file"

#define DEF_OUT std::cerr

struct CmdlineParameters {
    std::string inputFilePath;
    std::string privateKeyFilePath;
    std::string outPath;

    CmdlineParameters() {
        outPath = "transcoder.lic";
    }
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

        std::unique_ptr<utpl::crypto::PrivateKey> privateKey;

        {
            auto *privateKeyHex = getenv(UTPL_LICENSE_ENV_KEY_PRIVATE_VAR_NAME);

            if (privateKeyHex != nullptr) {
                privateKey = crypto.loadPrivateKey(utpl::utils::fromHex(privateKeyHex));
            }
        }

        {
            namespace po = boost::program_options;

            po::options_description options("Allowed options");

            options.add_options()(OPT_NAME_HELP  ",h",
                "Produce help message."
            );

            options.add_options()(OPT_NAME_PRIVATE_KEY  ",p", po::value<std::string>(&params.privateKeyFilePath), 
                "Path to the private key file. It can also be provided as a HEX string in the " UTPL_LICENSE_ENV_KEY_PRIVATE_VAR_NAME " environment variable."
            );

            options.add_options()(OPT_NAME_INPUT_FILE ",i", po::value<std::string>(&params.inputFilePath), 
                "Input license file path."
            );

            options.add_options()(OPT_NAME_OUT   ",o", po::value<std::string>(&params.outPath),
                "License output path (transcoder.lic)."
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

                    if (!privateKey && !_isValidFile(params.privateKeyFilePath))
                    {
                        DEF_OUT << "Empty private key path provided, or it points to a non-existing file, or the target is a directory." << std::endl;
                        break;
                    }

                    if (!_isValidFile(params.inputFilePath))
                    {
                        DEF_OUT << "Empty license file path provided, or it points to a non-existing file, or the target is a directory." << std::endl;
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
            if (! privateKey) {
                privateKey = crypto.loadPrivateKey(params.privateKeyFilePath);
            }

            {
                license::LicenseFile licenseFile;

                {
                    auto fileSerializer = std::make_unique<license::JsonLicenseFileSerializer>();
                    
                    std::ifstream licenseStream(params.inputFilePath);
                    
                    licenseFile = fileSerializer->LicenseFileSerializer::deserialize(licenseStream);
                }

                {
                    license::LicenseSignature s;
    
                    licenseFile.setSignature(
                        s.sign(licenseFile.getData(), *privateKey)
                    );
                }

                {
                    std::vector<uint8_t> serialized;

                    {
                        auto fileSerializer = std::make_unique<license::JsonLicenseFileSerializer>();

                        serialized = fileSerializer->serialize(licenseFile);
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
            
            DEF_OUT << "License successfully signed: " << params.outPath << std::endl;

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