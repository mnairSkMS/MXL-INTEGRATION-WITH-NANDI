#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include <boost/url/url.hpp>
#include <boost/url/parse.hpp>
#include <boost/algorithm/string.hpp>

#include <cpr/cpr.h>

#include "utpl/common/debug.h"
#include "utpl/common/utils.h"

#include "utpl/crypto/manager.h"
#include "utpl/crypto/signature.h"

#include "utpl/license/utils.h"

#include "application/license/manager.h"
#include "application/logger.h"

using namespace utpl;

namespace urls = boost::urls;

static Logger &logger = Logger::getLogger(APP_LOGGER_NAME_LICENSE);

#define LICENSE_SERVER_TIMEOUT_MS 3000
#define LICENSE_SERVER_ATTEMPTS   3
#define LICENSE_SERVER_TOKEN_TTL  60

static std::string _getUrl(const std::string &base, const std::string &endpoint) {
    urls::url parsedUrl = urls::parse_uri(base).value();

    {
        std::filesystem::path mergedPath = std::filesystem::path(parsedUrl.path());

        mergedPath /= endpoint;

        parsedUrl.set_path(mergedPath.string());
    }

    return parsedUrl.buffer();
}


app::LicenseManager::LicenseStatus app::LicenseManager::_processTokenResponse(cpr::Response &response) {
    LicenseStatus ret = LicenseStatus::VALID;

    {
        auto contentTypeIt = response.header.find("content-type");
        
        logger.debug("Received a successful response from the license server, verifying the response.");

        if (contentTypeIt == response.header.end()) {
            logger.error("The license server responds with success, but the response content type is unknown.");

            ret = LicenseStatus::INVALID;

        } else if (! boost::istarts_with(contentTypeIt->second, "application/json")) {
            logger.error("The license server responds with success, but the response content type is not JSON ({})", contentTypeIt->second);

            ret = LicenseStatus::INVALID;

        } else if (response.text.empty()) {
            logger.error("The license server responds with success, but the response is empty.", contentTypeIt->second);
            
            ret = LicenseStatus::INVALID;

        } else {
            auto responseJson = nlohmann::json::parse(response.text);

            if (! responseJson.contains("token")) {
                logger.error("The license server responded with no JWT token");

                ret = LicenseStatus::INVALID;

            } else {
                auto rawJwt      = responseJson["token"].get<std::string>();
                auto splittedJwt = utils::split(rawJwt, '.');

                if (splittedJwt.size() != 3) {
                    logger.error("The license server responded with invalid JWT token ({})", rawJwt);

                    ret = LicenseStatus::INVALID;
                    
                } else {
                    // Verify JWT algorithm
                    {
                        auto headerJson = nlohmann::json::parse(utils::fromBase64(splittedJwt[0]));

                        if (! headerJson.contains("alg")) {
                            logger.error("The license server responded with invalid JWT token - missing 'alg'");

                            ret = LicenseStatus::INVALID;

                        } else {
                            auto alg = headerJson["alg"].get<std::string>();

                            if (alg != "EdDSA") {
                                logger.error("The license server responded with not supported JWT token - 'alg' == '{}'", alg);

                                ret = LicenseStatus::INVALID;
                            }
                        }
                    }

                    // Verify JWT signature
                    if (ret == LicenseStatus::VALID) {
                        auto &manager  = crypto::getCryptoManager();
                        auto signature = manager.newSignature(crypto::SignatureAlgorithm::ED25519);
                        auto publicKey = manager.loadPublicKey(this->licensePublicKey);

                        signature->initVerify(*publicKey);
                        signature->update(splittedJwt[0] + "." + splittedJwt[1]);
                        
                        if (! signature->verify(utils::fromBase64(splittedJwt[2]))) {
                            logger.error("The license server responded with JWT token containing invalid signature");

                            ret = LicenseStatus::INVALID;
                        }
                    }

                    // Extract session id
                    if (ret == LicenseStatus::VALID) {
                        auto payloadJson = nlohmann::json::parse(utils::fromBase64(splittedJwt[1]));
                        
                        if (! payloadJson.contains("session_id")) {
                            logger.error("The license server responded with invalid JWT token - missing 'session_id'");

                            ret = LicenseStatus::INVALID;

                        } else {
                            this->sessionToken = payloadJson["session_id"].get<std::string>();
                        }

                        // Remember time when the token should be refreshed
                        if (ret == LicenseStatus::VALID) {
                            utpl::Timestamp serverTime     = 0;
                            utpl::Timestamp expirationTime = 0;

                            // Parse server time
                            {
                                auto headerIt = response.header.find("date");

                                if (headerIt == response.header.end()) {
                                    logger.warning("The license server does not provide the 'Date:' HTTP header. Using system time as a fallback.");

                                    serverTime = utpl::Time::getUTCTimestamp();

                                } else {
                                    serverTime = curl_getdate(headerIt->second.c_str(), nullptr);
                                    if (serverTime == -1) {
                                        logger.warning("The server reponded with Date header that cannot be parsed ({}). Using system time as a fallback.", headerIt->second);

                                        serverTime = utpl::Time::getUTCTimestamp();
                                    }
                                }
                            }

                            // Extract expiration time
                            {
                                if (! payloadJson.contains("exp")) {
                                    logger.warning("Session token does not specify expiration time, using {}s as a fallback", LICENSE_SERVER_TOKEN_TTL);

                                    expirationTime = serverTime + LICENSE_SERVER_TOKEN_TTL;

                                } else {
                                    expirationTime = payloadJson["exp"].get<utpl::Timestamp>();
                                }
                            }

                            // Calculate refresh timestamp
                            this->sessionTokenCheckTimestamp  = utpl::Time::getMonotonicTimestamp() + (expirationTime - serverTime) * 1000;
                            this->sessionTokenCheckTimestamp -= LICENSE_SERVER_TIMEOUT_MS; 
                        }
                    }
                }
            }
        }
    }

    return ret;
}

bool app::LicenseManager::_onCheckLicense(LicenseManager *self) {
    bool verifyExpiration = true;
    
    LicenseStatus newStatus = LicenseStatus::VALID;

    if (! self->license.getModel().isOffline()) {
        verifyExpiration = false;

        if (self->sessionToken.empty()) {
            logger.log("Opening session on license server (attempt {}/{})", self->httpAttempts + 1, LICENSE_SERVER_ATTEMPTS);

            try {
                self->httpSession->SetUrl(cpr::Url(_getUrl(self->licenseServer, "create_session")));
                self->httpSession->SetTimeout(std::chrono::milliseconds(LICENSE_SERVER_TIMEOUT_MS));
                self->httpSession->SetAcceptEncoding(cpr::AcceptEncoding{});
                self->httpSession->SetBody(self->licenseData);
                self->httpSession->SetHeader(cpr::Header({
                    { "Content-Type", "application/json" }
                }));


                auto response = self->httpSession->Post();

                switch (response.status_code) {
                    case 0:
                        {
                            logger.error("The license server is not responding. Code: {}, Message: {}", (int) response.error.code, response.error.message);

                            if (response.error.code != cpr::ErrorCode::OPERATION_TIMEDOUT) {
                                newStatus = LicenseStatus::INVALID;

                            } else {
                                self->httpAttempts++;

                                newStatus = self->licenseStatus;
                            }
                        }
                        break;

                    case 400:
                        {
                            logger.error("Your license is invalid or expired, or the number of allowed concurrent sessions has been reached.");

                            newStatus = LicenseStatus::INVALID;
                        }
                        break;

                    case 200:
                        {
                            self->httpAttempts = 0;

                            newStatus = self->_processTokenResponse(response);
                            if (newStatus == LicenseStatus::VALID) {
                                logger.log("The license server successfully validated the license and started a new session.");
                            }
                        } 
                        break;

                    default:
                        {
                            logger.error("Unexpected error occurred while creating new license session, HTTP status code: {}, errorCode: {}, errorMessage: {}", 
                                response.status_code, (int) response.error.code, response.error.message
                            );

                            newStatus = LicenseStatus::INVALID;
                        }
                        break;
                } 
            } catch (const std::exception &ex) {
                logger.warning("Cought unexpected exception: {}", ex.what());

                newStatus = LicenseStatus::INVALID;
            }

        } else {
            auto currentTime = utpl::Time::getMonotonicTimestamp();

            if (currentTime >= self->sessionTokenCheckTimestamp) {
                logger.debug("Refreshing the current session ID on the license server.");

                try {
                    self->httpSession->SetUrl(cpr::Url(_getUrl(self->licenseServer, "heartbeat/" + self->sessionToken)));
                    self->httpSession->SetTimeout(std::chrono::milliseconds(LICENSE_SERVER_TIMEOUT_MS));
                    self->httpSession->SetAcceptEncoding(cpr::AcceptEncoding{});
                    self->httpSession->SetBody(cpr::Body());
                    self->httpSession->SetHeader(cpr::Header({
                        { "Content-Type", "application/json" }
                    }));


                    auto response = self->httpSession->Post();

                    switch (response.status_code) {
                        case 0:
                            {
                                logger.error("The license server is not responding to hearbeat endpoint. Code: {}, Message: {}", (int) response.error.code, response.error.message);

                                if (response.error.code != cpr::ErrorCode::OPERATION_TIMEDOUT) {
                                    newStatus = LicenseStatus::INVALID;

                                } else {
                                    self->httpAttempts++;

                                    newStatus = self->licenseStatus;
                                }
                            }
                            break;

                        case 404:
                            {
                                logger.warning("Current session token is not valid - it probably get expired.");

                                // Open a new session
                                self->sessionToken.clear();
                            }
                            break;

                        case 200:
                            {
                                self->httpAttempts = 0;

                                newStatus = self->_processTokenResponse(response);
                                if (newStatus == LicenseStatus::VALID) {
                                    logger.log("The license server successfully refreshed the current session.");
                                }
                            } 
                            break;

                        default:
                            {
                                logger.error("Unexpected error occurred while creating new license session, HTTP status code: {}, errorCode: {}, errorMessage: {}", 
                                    response.status_code, (int) response.error.code, response.error.message
                                );

                                newStatus = LicenseStatus::INVALID;
                            }
                            break;
                    } 
                } catch (const std::exception &ex) {
                    logger.warning("Cought unexpected exception: {}", ex.what());

                    newStatus = LicenseStatus::INVALID;
                }
            }
        }

        if (self->httpAttempts == LICENSE_SERVER_ATTEMPTS) {
            logger.error("The number of allowed connection attempts to the license server exceeds the maximum limit of {}.", LICENSE_SERVER_ATTEMPTS);

            newStatus = LicenseStatus::INVALID;
        }
    }

    if ((newStatus == LicenseStatus::VALID) && verifyExpiration) {
        auto currentTs = Time::getUTCTimestamp();

        if (verifyExpiration) {
            if (license::LicenseUtils::isValid(self->license, currentTs)) {
                TRACE(("License is valid"));

            } else {
                if (license::LicenseUtils::isExpired(self->license, currentTs)) {
                    logger.warning("License has expired. Requesting transcoder to stop.");

                } else {
                    logger.warning("The license is not yet valid. The system clock may have been tampered with.");
                }

                newStatus = LicenseStatus::INVALID;
            }
        }
    }

    if (newStatus != self->licenseStatus) {
        self->licenseStatus = newStatus;

        if (self->callback) {
            self->callback(self->licenseStatus);
        }
    }

    return newStatus == LicenseStatus::VALID || newStatus == LicenseStatus::UNKNOWN;
}

app::LicenseManager::LicenseManager() {
    this->timerId                    = 0;
    this->license                    = license;
    this->sessionTokenCheckTimestamp = 0;
    this->licenseStatus              = LicenseStatus::UNKNOWN;
    this->httpAttempts               = 0;
}

void app::LicenseManager::configure(const utpl::license::License &license, const std::string &licensePath, const std::string &licenseServerUri, const utpl::crypto::PublicKey &licensePublicKey) {
    this->license                    = license;
    this->licensePath                = licensePath;
    this->licenseServer              = licenseServerUri;
    this->licensePublicKey           = licensePublicKey.raw();
    this->licenseStatus              = LicenseStatus::UNKNOWN;
    this->sessionTokenCheckTimestamp = 0;
    this->httpAttempts               = 0;

    if (! this->license.getModel().isOffline()) {
        this->httpSession = std::make_unique<cpr::Session>();
    }

    logger.debug("LicenseManager was configured for license '{}'", this->license.getModel().getName());

    {
        std::ifstream t(licensePath);

        std::stringstream buffer;

        buffer << t.rdbuf();

        {
            auto decoded = utpl::utils::pemParse(buffer.str());

            this->licenseData.assign(reinterpret_cast<const char *>(decoded.data()), decoded.size());
        }
    }
}

void app::LicenseManager::start(LicenseExpiredCallback callback) {
    this->callback                   = callback;
    this->sessionTokenCheckTimestamp = 0;
    this->licenseStatus              = LicenseStatus::UNKNOWN;
    this->httpAttempts               = 0;

    this->sessionToken.clear();

    this->timerId = g_timeout_add_seconds(1, G_SOURCE_FUNC(_onCheckLicense), this);

    logger.log("LicenseManager has been started");
}

void app::LicenseManager::stop() {
    if (this->timerId) {
        g_source_remove(this->timerId);

        this->timerId = 0;

        logger.log("LicenseManager has been stopped");
    }

    this->callback = nullptr;
}