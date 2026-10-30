#include <iostream>
#include <optional>
#include <pugixml.hpp>
#include <string>
#include <sstream>

#include "utpl/esam/model.h"

#include "utpl/common/time.h"
#include "utpl/common/types/exception.h"


utpl::esam::SignalProcessingEvent::SignalProcessingEvent(const std::string& clientID, const std::string &scte35, const utpl::Timestamp &acquisitionTime, const utpl::Timestamp utcPoint, const utpl::Timestamp &pts) {
    acquiredSignal.acquisitionPointIdentity = clientID;
    acquiredSignal.acquisitionSignalID = utpl::utils::generateGUID();
    acquiredSignal.acquisitionTime = utpl::Time::toEsamDateTime(acquisitionTime);
    acquiredSignal.utcPoint = utpl::Time::toEsamDateTime(utcPoint); 
    acquiredSignal.scte35 = scte35;
    acquiredSignal.streamTimes.push_back({"PTS", std::to_string(pts)});
};


pugi::xml_document utpl::esam::SignalProcessingEvent::toXml() const {
    pugi::xml_document doc;

    auto root = doc.append_child("SignalProcessingEvent");
    root.append_attribute("xmlns:xsd") = "http://www.w3.org/2001/XMLSchema";
    root.append_attribute("xmlns:core") = "urn:cablelabs:md:xsd:core:3.0";
    root.append_attribute("xmlns:sig") = "urn:cablelabs:md:xsd:signaling:3.0";
    root.append_attribute("xmlns:common") = "urn:cablelabs:iptvservices:esam:xsd:common:1";
    root.append_attribute("xmlns") = "urn:cablelabs:iptvservices:esam:xsd:signal:1";

    acquiredSignal.toXml(root);

    return doc;
}


std::string utpl::esam::SignalProcessingEvent::toXmlString() const {
    pugi::xml_document doc = this->toXml();
    std::ostringstream oss;
    doc.save(oss, "  "); // XML to string stream with formatting
    return oss.str();
}


utpl::esam::SignalProcessingNotification utpl::esam::SignalProcessingNotification::fromXml(const std::string& xmlContent) {
    SignalProcessingNotification notification;
    notification.parseFromXml(xmlContent);
    return notification;
}


void utpl::esam::SignalProcessingNotification::parseFromXml(const std::string& xmlContent) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(xmlContent.c_str());

    if (!result) {
        throw utpl::InvalidArgumentException("Failed to parse XML");
    }

    pugi::xml_node root = doc.child("SignalProcessingNotification");
    if (root.attribute("acquisitionPointIdentity")) {
        acquisitionPointIdentity = root.attribute("acquisitionPointIdentity").value();
    }

    for (pugi::xml_node node : root.children("ResponseSignal")) {
        ResponseSignal signal;
        signal.fromXml(node);
        responseSignals.push_back(signal);
    }

    for (pugi::xml_node node : root.children("ConditioningInfo")) {
        ConditioningInfo conditioningInfo;
        conditioningInfo.fromXml(node);
        conditioningInfos.push_back(conditioningInfo);
    }

    if (root.child("StatusCode")) {
        statusCode.fromXml(root.child("StatusCode"));
    }
}


void utpl::esam::AcquiredSignal::fromXml(const pugi::xml_node &node) {
    if (node.attribute("acquisitionPointIdentity")) {
        acquisitionPointIdentity = node.attribute("acquisitionPointIdentity").value();
    }

    if (node.attribute("acquisitionSignalID")) {
        acquisitionSignalID = node.attribute("acquisitionSignalID").value();
    }

    if (node.attribute("acquisitionTime")) {
        acquisitionTime = node.attribute("acquisitionTime").value();
    }

    if (node.child("sig:UTCPoint")) {
        utcPoint = node.child("sig:UTCPoint").attribute("utcPoint").value();
    }

    scte35 = node.child("sig:BinaryData").text().as_string();

    // TODO: add support for SCTE35PointDescriptor
    if (node.child("sig:SCTE35PointDescriptor")) {
        throw utpl::InvalidArgumentException("SCTE35PointDescriptor is not supported");
    }

    for (pugi::xml_node streamTimeNode : node.child("sig:StreamTimes").children("sig:StreamTime")) {
        StreamTime streamTime;
        streamTime.timeType = streamTimeNode.child("sig:timeType").text().as_string();
        streamTime.timeValue = streamTimeNode.child("sig:timeValue").text().as_string();
        streamTimes.push_back(streamTime);
    }
}


void utpl::esam::AcquiredSignal::toXml(pugi::xml_node &parent) const {
    auto acquiredSignalNode = parent.append_child("AcquiredSignal");
    acquiredSignalNode.append_attribute("acquisitionPointIdentity") = acquisitionPointIdentity.c_str();
    acquiredSignalNode.append_attribute("acquisitionSignalID") = acquisitionSignalID.c_str();
    acquiredSignalNode.append_attribute("acquisitionTime") = acquisitionTime.c_str();

    acquiredSignalNode.append_child("sig:UTCPoint").append_attribute("utcPoint") = utcPoint.c_str();
    auto scte35 = acquiredSignalNode.append_child("sig:BinaryData");
    {
        scte35.append_attribute("signalType") = "SCTE35";
        scte35.text().set(this->scte35.c_str());
    }

    if (!streamTimes.empty()) {
        auto streamTimesNode = acquiredSignalNode.append_child("sig:StreamTimes");

        for (const auto &streamTime : streamTimes) {
            streamTime.toXml(streamTimesNode);
        }
    }
}


void utpl::esam::StatusCode::fromXml(const pugi::xml_node &node) {
    if (!node.attribute("code")) {
        return;
    }

    code = node.attribute("code").as_int();
    if (node.attribute("detailCode")) {
        detailCode = node.attribute("detailCode").as_int();
    }

    if (node.child("core:Note")) {
        note = node.child("core:Note").text().as_string();
    }
}


void utpl::esam::ResponseSignal::fromXml(const pugi::xml_node& node) {
    AcquiredSignal::fromXml(node);
    action = node.attribute("action").value();
}


void utpl::esam::ConditioningInfo::fromXml(const pugi::xml_node &node) {
    duration = node.attribute("duration").value();
    acquisitionSignalIDRef = node.attribute("acquisitionSignalIDRef").value();
    startOffset = node.attribute("startOffset").value();
}


void utpl::esam::StreamTime::toXml(pugi::xml_node &parent) const {
    pugi::xml_node streamTimeNode = parent.append_child("sig:StreamTime");
    streamTimeNode.append_child("sig:timeType").text().set(timeType.c_str());
    streamTimeNode.append_child("sig:timeValue").text().set(timeValue.c_str());
}
