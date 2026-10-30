
#include <iostream>
#include <regex>
#include <stdexcept>
#include <cpr/cpr.h>
#include <pugixml.hpp>

#include "utpl/common/utils.h"
#include "utpl/common/types/exception.h"
#include "utpl/common/debug.h"

#include "utpl/esam/client.h"


utpl::esam::Client::Client() {
    this->_session.SetHeader(cpr::Header{
        {"Content-Type", "application/xml"},
        {"Accept", "application/xml"},
        {"charset", "utf-8"}
    });
}


utpl::esam::Client::Client(const std::string &endpoint) {
    this->setEndpoint(endpoint);
}


utpl::esam::Client::~Client() {}


void utpl::esam::Client::setEndpoint(const std::string &endpoint) {
    this->_session.SetUrl(cpr::Url{endpoint});
}


void utpl::esam::Client::setTimeout(utpl::Timestamp timeout) {
    std::chrono::milliseconds duration(timeout);
    this->_session.SetTimeout(cpr::Timeout{duration});
}


void utpl::esam::Client::setAcquisitionPointId(const std::string &id) {
    this->_acquisitionPointId = id;
}

void utpl::esam::Client::signalEvent(const SignalParameters& signalParams, EventResponse& response) {
    std::string scte35Base64 = utils::toBase64(signalParams.sectionData);
    SignalProcessingEvent spe(this->_acquisitionPointId, scte35Base64, signalParams.acquisitionTime, signalParams.signalTimestamp, signalParams.pts);

    auto httpResponse = this->_sendSignalProcessingEvent(spe);
    if (httpResponse.status_code == 0) {
        throw utpl::Exception("Failed to send signal processing event to POIS endpoint, error: " + httpResponse.error.message);
    }

    if (httpResponse.status_code >= 400) {
        throw utpl::Exception("Failed to send signal processing event to POIS endpoint, status code: " + std::to_string(httpResponse.status_code));
    }

    {
        auto notification = SignalProcessingNotification::fromXml(httpResponse.text);
        // Only first signal is supported ( no batch processing )
        if (notification.responseSignals.empty()) {
            throw utpl::Exception("No response signals in notification");
        }

        auto &responseSignal = notification.responseSignals[0];

        TRACE(("Got response action: {}", responseSignal.action));

        // Verify the response signal
        {
            if (notification.statusCode.code != SUCCESS) {
                throw utpl::Exception("Error status code : " + std::to_string(notification.statusCode.code) + ", message: " + notification.statusCode.note);
            }

            auto acquisitionPointIdentity = responseSignal.acquisitionPointIdentity.empty() ? notification.acquisitionPointIdentity : responseSignal.acquisitionPointIdentity;
            if (acquisitionPointIdentity.empty() || acquisitionPointIdentity != spe.acquiredSignal.acquisitionPointIdentity) {
                throw utpl::Exception("AcquisitionPointIdentity mismatch, expected: '" + spe.acquiredSignal.acquisitionPointIdentity + "', got: '" + acquisitionPointIdentity + "'");
            }

            if (responseSignal.acquisitionSignalID != spe.acquiredSignal.acquisitionSignalID) {
                throw utpl::Exception("AcquisitionSignalID mismatch, expected: '" + spe.acquiredSignal.acquisitionSignalID + "', got: '" + responseSignal.acquisitionSignalID + "'");
            }
        }

        // Parse action
        {
            auto &action = responseSignal.action;
            if (action == "noop") {
                response.mode = Operation::NOOP;

            } else if (action == "delete") {
                response.mode = Operation::DELETE;

            } else if (action == "replace") {
                response.mode = Operation::REPLACE;

            } else if (action == "create") {
                response.mode = Operation::CREATE;

            } else if (action == "private") {
                response.mode = Operation::PRIVATE;

            } else {
                throw utpl::InvalidArgumentException("Unknown action in notification");
            }
        }

        // Parse scte35 data
        if (! responseSignal.scte35.empty()) {
            response.sectionData = utils::fromBase64(notification.responseSignals[0].scte35);
        }

        // Parse conditioning info
        for (const auto& info : notification.conditioningInfos) {
            if (info.acquisitionSignalIDRef.empty() || info.acquisitionSignalIDRef == responseSignal.acquisitionSignalID) {
                // duration and startOffset format is ISO 8601 duration e.g. "PT30S"/
                try {
                    response.conditioningInfo.duration = this->_parseISO8601Duration(info.duration);
                    response.conditioningInfo.startOffset = this->_parseISO8601Duration(info.startOffset);

                } catch (const std::invalid_argument& e) {
                    throw utpl::InvalidArgumentException("Failed to parse conditioning info: " + std::string(e.what()));
                }
            }
        }
    }
}


std::future<void> utpl::esam::Client::signalEventAsync(const SignalParameters& signalParams, std::function<void(EventResponse &response, bool)> callback) {
    return std::async(std::launch::async, [this, signalParams, callback]() {
        EventResponse response;
        bool success = false;

        try {
            this->signalEvent(signalParams, response);
            success = true;

        } catch (const std::exception& e) {
            success = false;
            ERR(("Failed to signal event to POIS server: {}", e.what()));
        }

        callback(response, success);
    });
}


cpr::Response utpl::esam::Client::_sendSignalProcessingEvent(const SignalProcessingEvent &event) {
    auto payload = event.toXmlString();

    LOG(("Sending signal processing event: {}", payload));
    _session.SetBody(cpr::Body{payload});

    auto response = _session.Post();

    return response;
}


utpl::Timestamp utpl::esam::Client::_parseISO8601Duration(const std::string& iso8601Duration) const {
    std::regex pattern(R"(P(?:(\d+)Y)?(?:(\d+)M)?(?:(\d+)W)?(?:(\d+)D)?(?:T(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?)?)");
    std::smatch matches;

    if (!std::regex_match(iso8601Duration, matches, pattern)) {
        throw std::invalid_argument("Invalid ISO-8601 duration format");
    }

    uint64_t total_milliseconds = 0;
    if (matches[1].matched) total_milliseconds += std::stoull(matches[1].str()) * 31536000000ULL; // years to ms
    if (matches[2].matched) total_milliseconds += std::stoull(matches[2].str()) * 2592000000ULL;  // months to ms
    if (matches[3].matched) total_milliseconds += std::stoull(matches[3].str()) * 604800000ULL;   // weeks to ms
    if (matches[4].matched) total_milliseconds += std::stoull(matches[4].str()) * 86400000ULL;    // days to ms
    if (matches[5].matched) total_milliseconds += std::stoull(matches[5].str()) * 3600000ULL;     // hours to ms
    if (matches[6].matched) total_milliseconds += std::stoull(matches[6].str()) * 60000ULL;       // minutes to ms
    if (matches[7].matched) total_milliseconds += std::stoull(matches[7].str()) * 1000ULL;        // seconds to ms

    return total_milliseconds;
}


void utpl::esam::Client::_dump(const utpl::esam::SignalProcessingNotification &notification) const {
    TRACE(("SignalProcessingNotification:"));

    TRACE(("  Status Code: {}, detail code: {}, msg: {}", notification.statusCode.code, notification.statusCode.detailCode, notification.statusCode.note));

    for (const auto& signal : notification.responseSignals) {
        TRACE(("  ResponseSignal"));
        TRACE(("    acquisitionPointIdentity: {}", signal.acquisitionPointIdentity));
        TRACE(("    acquisitionSignalID: {}", signal.acquisitionSignalID));
        TRACE(("    acquisitionTime: {}", signal.acquisitionTime));
        TRACE(("    utcPoint: {}", signal.utcPoint));
        TRACE(("    scte35: {}", signal.scte35));
        TRACE(("    Action: {}", signal.action));
    }
    for (const auto& info : notification.conditioningInfos) {
        TRACE(("  ConditioningInfo:" ));
        TRACE(("    Duration: ", info.duration ));
        TRACE(("    Acquisition Signal ID Ref: ", info.acquisitionSignalIDRef ));
        TRACE(("    Start Offset: ", info.startOffset ));
    }
}
