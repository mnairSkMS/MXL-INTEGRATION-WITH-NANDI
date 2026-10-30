#include <iostream>
#include <filesystem>

#include <boost/program_options.hpp>

#include "utpl/common/types/exception.h"
#include "utpl/common/debug.h"
#include "utpl/common/logger.h"
#include "utpl/common/utils.h"

#include "utpl/crypto/manager.h"

#include "utpl/license/config.h"
#include "utpl/license/elf/keyStorer.h"
#include "utpl/license/exception.h"

using namespace utpl;

#define DEF_OUT std::cerr

#define OPT_NAME_HELP        "help"
#define OPT_NAME_PUBLIC_KEY  "public-key"
#define OPT_NAME_LICENSE_KEY "license-key"
#define OPT_NAME_FILE        "elf-path"

struct CmdlineParameters {
    std::string keyPathPublic;
    std::string keyPathLicense;
    std::string elfPath;
};

int main(int argc, char *argv[]) {
    utpl::Error ret = utpl::Error::NO_ERROR;

    do {
        CmdlineParameters params;

        std::vector<uint8_t> publicKey;
        std::vector<uint8_t> encKey;

        {
            auto *publicKeyHex = getenv(UTPL_LICENSE_ENV_KEY_PUBLIC_VAR_NAME);

            if (publicKeyHex != nullptr) {
                publicKey = utpl::utils::fromHex(publicKeyHex);
            }
        }

        {
            auto *licenseKeyHex = getenv(UTPL_LICENSE_ENV_KEY_ENCRYPTION_VAR_NAME);

            if (licenseKeyHex != nullptr) {
                encKey = utpl::utils::fromHex(licenseKeyHex);
            }
        }

        {
            namespace po = boost::program_options;

            po::options_description options("Allowed options");

            options.add_options()
                (OPT_NAME_HELP ",h", "Produce help message.");

            options.add_options()
                (OPT_NAME_PUBLIC_KEY ",p", po::value<std::string>(&params.keyPathPublic), "Path to the public key file. It can also be provided as a HEX string in the " UTPL_LICENSE_ENV_KEY_PUBLIC_VAR_NAME " environment variable.");

            options.add_options()
                (OPT_NAME_LICENSE_KEY ",l", po::value<std::string>(&params.keyPathLicense), "Path to the license encryption key file. It can also be provided as a HEX string in the " UTPL_LICENSE_ENV_KEY_ENCRYPTION_VAR_NAME " environment variable.");

            options.add_options()
                (OPT_NAME_FILE ",i", po::value<std::string>(&params.elfPath), "Path to ELF file.");

            try {
                po::variables_map vm;

                {
                    po::positional_options_description pos_opts;

                    po::store(
                        po::command_line_parser(argc, argv).options(options).positional(pos_opts).run(), vm
                    );
                }

                po::notify(vm);

                bool validated = false;

                do {
                    if (argc == 1 || vm.count(OPT_NAME_HELP) > 0) {
                        DEF_OUT << options << std::endl;
                        break;
                    }

                    if (publicKey.empty() && (params.keyPathPublic.empty() || ! std::filesystem::exists(params.keyPathPublic) || std::filesystem::is_directory(params.keyPathPublic))) {
                        DEF_OUT << "Empty public key path provided, or it points to a non-existing file, or the target is a directory." << std::endl;
                        break;
                    }

                    if (encKey.empty() && (params.keyPathLicense.empty() || ! std::filesystem::exists(params.keyPathLicense) || std::filesystem::is_directory(params.keyPathLicense))) {
                        DEF_OUT << "Empty license encryption key path provided, or it points to a non-existing file, or the target is a directory." << std::endl;
                        break;
                    }

                    if (params.elfPath.empty() || ! std::filesystem::exists(params.elfPath) || std::filesystem::is_directory(params.elfPath)) {
                        DEF_OUT << "Empty ELF path provided, or it points to a non-existing file, or the target is a directory." << std::endl;
                        break;
                    }

                    validated = true;
                } while (0);

                if (! validated) {
                    ret = utpl::Error::INVALID_ARGUMENT;
                    break;
                }
            }  catch (po::error const& e) {
                DEF_OUT << "Invalid options provided: " << e.what() << std::endl;
                
                ret = utpl::Error::INVALID_ARGUMENT;
                break;
            }
        }

        try {
            auto &crypto = utpl::crypto::getCryptoManager();

            license::ElfKeyStorer keyStorer;

            if (publicKey.empty()) {
                publicKey = crypto.loadPublicKey(params.keyPathPublic)->raw();
            }

            if (encKey.empty()) {
                encKey = crypto.loadKey(params.keyPathLicense)->raw();
            }

            keyStorer.setPublicKey(publicKey);
            keyStorer.setEncryptionKey(encKey);
            keyStorer.store(params.elfPath);

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
    } while (0);

    return (int) ret;
}