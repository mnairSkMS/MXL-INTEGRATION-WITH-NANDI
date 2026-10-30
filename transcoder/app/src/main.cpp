#include <iostream>
#include <filesystem>
#include <fstream>

#include <boost/program_options.hpp>
#include <boost/dll.hpp>
#include <nlohmann/json.hpp>

#include "utpl.h"
#include "utpl/common/debug.h"
#include "utpl/common/types/exception.h"

#include "utpl/license/exception.h"
#include "utpl/license/elf/keyLoader.h"
#include "utpl/license/file/serializer/json.h"
#include "utpl/license/serializer/json.h"
#include "utpl/license/signature.h"
#include "utpl/license/utils.h"
#include "utpl/license.h"

#include "application.h"
#include "application/logger.h"

namespace po = boost::program_options;

#define DEF_OUT std::cerr

#define OPT_NAME_HELP    "help"
#define OPT_NAME_CONFIG  "config"
#define OPT_NAME_DB      "db"
#define OPT_NAME_LICENSE "license"
#define OPT_NAME_LICENSE_INFO "license-info"

struct CmdlineParameters {
    std::string configFilePath;
    std::string dbFilePath;
    std::string licensePath;
    bool        licenseInfo = false;
};

// Exit codes of --license-info. The management API keys off the JSON status rather than these,
// but they let a shell script tell a usable license from an unusable one without parsing.
enum class LicenseInfoExit : int {
    VALID    = 0,
    UNUSABLE = 1,   // Authentic, but expired or not yet valid.
    INVALID  = 2,   // Unreadable, corrupted, or not signed with this binary's key.
};

// Decode and authenticate a license with the keys embedded in this binary. Throws on any file
// that is not a genuine license; time validity is left to the caller.
static utpl::license::License loadLicense(const std::string &licensePath, utpl::license::ElfKeyLoader &loader) {
    utpl::license::LicenseFile licenseFile;

    {
        std::unique_ptr<utpl::license::LicenseFileSerializer> serializer = std::make_unique<utpl::license::JsonLicenseFileSerializer>();

        std::ifstream licenseStream(licensePath);

        licenseFile = serializer->deserialize(licenseStream);
    }

    {
        utpl::license::LicenseSignature validator;

        if (! validator.verify(licenseFile.getData(), licenseFile.getSignature(), loader.getPublicKey())) {
            throw utpl::license::GenericLicenseException("License is not valid");
        }
    }

    auto serializer = std::make_unique<utpl::license::JsonLicenseSerializer>();

    return serializer->deserialize(licenseFile.getData(), loader.getEncryptionKey());
}

// --license-info: report on a license file as one JSON object on stdout, without a config and
// without starting anything. This is how the management API inspects a license - the keys that
// decrypt it exist only inside this binary, so nothing else can.
static int printLicenseInfo(const std::string &licensePath) {
    nlohmann::json report;
    LicenseInfoExit code = LicenseInfoExit::INVALID;

    try {
        if (! std::filesystem::is_regular_file(licensePath)) {
            throw utpl::license::GenericLicenseException("No license file at " + licensePath);
        }

        utpl::license::ElfKeyLoader loader;

        loader.load(boost::dll::program_location().string());

        const auto license = loadLicense(licensePath, loader);
        const auto &model  = license.getModel();
        const auto now     = utpl::Time::getUTCTimestamp();

        if (utpl::license::LicenseUtils::isExpired(license, now)) {
            report["status"] = "expired";
            code = LicenseInfoExit::UNUSABLE;
        } else if (utpl::license::LicenseUtils::isNotYetValid(license, now)) {
            report["status"] = "notYetValid";
            code = LicenseInfoExit::UNUSABLE;
        } else {
            report["status"] = "valid";
            code = LicenseInfoExit::VALID;
        }

        report["id"]          = model.getId();
        report["name"]        = model.getName();
        report["validSince"]  = model.getValidSince();
        report["validUntil"]  = model.getValidSince() + model.getTtl();
        report["ttl"]         = model.getTtl();
        report["offline"]     = model.isOffline();
        report["maxSessions"] = model.getMaxSessions();
        report["createdAt"]   = license.getMetadata().getCreationTime();
        report["checkedAt"]   = now;

    } catch (const std::exception &ex) {
        report = nlohmann::json::object();
        report["status"] = "invalid";
        report["error"]  = ex.what();
        code = LicenseInfoExit::INVALID;
    }

    std::cout << report.dump() << std::endl;

    return static_cast<int>(code);
}

int main(int argc, char *argv[]) {
    utpl::Error ret = utpl::Error::NO_ERROR;
    
    do {
        CmdlineParameters params;

        utpl::initialize();

        {
            namespace po = boost::program_options;

            po::options_description options("Allowed options");

            options.add_options()
                (OPT_NAME_HELP ",h", "Produce help message.");

            options.add_options()
                (OPT_NAME_CONFIG ",c", po::value<std::string>(&params.configFilePath), "Path to configuration file.");

            options.add_options()
                (OPT_NAME_DB ",b", po::value<std::string>(&params.dbFilePath), "Path to SQLite database file.");

            options.add_options()
                (OPT_NAME_LICENSE ",l", po::value<std::string>(&params.licensePath), "Path to license file.");

            options.add_options()
                (OPT_NAME_LICENSE_INFO, po::bool_switch(&params.licenseInfo), "Print the license given by -l as JSON and exit. Needs no configuration.");

            {
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

                    if (params.licenseInfo) {
                        if (params.licensePath.empty()) {
                            DEF_OUT << "--" OPT_NAME_LICENSE_INFO " needs a license path given with -l." << std::endl;
                            break;
                        }

                        return printLicenseInfo(params.licensePath);
                    }

                    if (params.configFilePath.empty() || ! std::filesystem::exists(params.configFilePath) || std::filesystem::is_directory(params.configFilePath)) {
                        DEF_OUT << "Empty configuration path provided, or it points to a non-existing file, or the target is a directory." << std::endl;
                        break;
                    }

                    if (params.licensePath.empty() || ! std::filesystem::exists(params.licensePath) || std::filesystem::is_directory(params.licensePath)) {
                        DEF_OUT << "Empty license path provided, or it points to a non-existing file, or the target is a directory." << std::endl;
                        break;
                    }

                    validated = true;
                } while (0);

                if (! validated) {
                    ret = utpl::Error::INVALID_ARGUMENT;
                    break;
                }
            }
        }

        try {
            Application app;

            {
                auto &licenseLogger = utpl::Logger::getLogger(APP_LOGGER_NAME_LICENSE);

                utpl::license::License license;

                utpl::license::ElfKeyLoader loader;

                loader.load(boost::dll::program_location().string());

                try {
                    license = loadLicense(params.licensePath, loader);
                } catch (const utpl::license::GenericLicenseException &) {
                    throw;
                } catch (const std::exception &e) {
                    licenseLogger.error("Unable to load the license file {}. The license is either invalid or corrupted.", params.licensePath);

                    throw;
                }

                const auto licenseModel = license.getModel();

                // Preliminary validation
                // TODO: make sure if it has sense for online licenses
                {
                    auto currentTs = utpl::Time::getUTCTimestamp();
                    auto startTs   = licenseModel.getValidSince();
                    auto endTs     = startTs + licenseModel.getTtl();

                    std::string title;

                    if (utpl::license::LicenseUtils::isExpired(license, currentTs)) {
                        title = "The license has expired on " + utpl::Time::toUTCString(endTs) + " UTC. Exiting the application.";

                    } else if (utpl::license::LicenseUtils::isNotYetValid(license, currentTs)) {
                        title = "The license is not yet valid (starts on " + utpl::Time::toUTCString(startTs) + " UTC). Exiting the application.";
                    }

                    if (! title.empty()) {
                        licenseLogger.error(title);

                        throw utpl::license::GenericLicenseException(title);
                    }
                }
                
                licenseLogger.always("Have a correct license '{}', valid until {}, {}offline capable", 
                    licenseModel.getName(), utpl::Time::toUTCString(licenseModel.getValidSince() + licenseModel.getTtl()), licenseModel.isOffline() ? "" : "not "
                );

                app.configure(params.configFilePath, params.dbFilePath, params.licensePath, license, loader.getPublicKey());
            }

            app.start();
            {           
                app.loop();
            }
            app.stop();

            ret = app.getError();

        } catch (const utpl::license::GenericLicenseException &ex) {
            ERR(("Cought license related exception: {}", ex.getMessage()));
            ret = utpl::Error::LICENSE_ERROR;
        
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