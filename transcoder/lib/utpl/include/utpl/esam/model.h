#ifndef __UTPL_ESAM_MODEL_H__
#define __UTPL_ESAM_MODEL_H__

#include <string>
#include <optional>
#include <pugixml.hpp>

#include "utpl/common/time.h"
#include "utpl/common/utils.h"
#include "utpl/common/types/exception.h"

namespace utpl {
    namespace esam {
        enum StatusCodeType {
            SUCCESS,
            ERROR,
            WARNING,
            INFO
        };

        enum DetailCodeType {
            RESERVED,
            GENERAL_ERROR,
            RESOURCE_NOT_FOUND,
            MISSING_PARAMETER
        };

        struct StatusCode {
            int code;
            int detailCode;
            std::string note;

            StatusCode() : code(0), detailCode(0) {}

            void fromXml(const pugi::xml_node& node);
        };

        struct StreamTime {
            std::string timeType;
            std::string timeValue;

            void toXml(pugi::xml_node& parent) const;
        };

        struct ConditioningInfo {
            std::string duration;
            std::string acquisitionSignalIDRef;
            std::string startOffset;

            void fromXml(const pugi::xml_node& node);
        };

        struct AcquiredSignal {
            std::string acquisitionPointIdentity;
            std::string acquisitionSignalID;
            std::string acquisitionTime;
            // UTC Zulu combined date and time encoding for the signal point. This UTC time value typically SHOULD NOT match the
            // acquisitionTime value. The UTC time SHOULD be sourced from an SCTE 35 time_descriptor
            // ([SCTE 35]) if present in the source stream. If no SCTE 35 time_descriptor is present in the source
            //  stream, the SAS SHOULD set its own clock. 
            // Time derived by splice_time in splice_insert with pts_adjustment from splice_info_section applied
            std::string utcPoint;
            std::string scte35;
            std::vector<StreamTime> streamTimes;

            void toXml(pugi::xml_node& parent) const;
            void fromXml(const pugi::xml_node &node);
        };
        
        struct ResponseSignal : public AcquiredSignal {
            std::string action;

            void fromXml(const pugi::xml_node& node);
        };

        struct SignalProcessingEvent {
            AcquiredSignal acquiredSignal;

            SignalProcessingEvent(const std::string& clientID, const std::string &scte35, const utpl::Timestamp &acquisitionTime, const utpl::Timestamp utcPoint, const utpl::Timestamp &pts);

            pugi::xml_document toXml() const;
            std::string toXmlString() const;
        };

        struct SignalProcessingNotification {
            StatusCode statusCode;
            std::string acquisitionPointIdentity;
            std::vector<ResponseSignal> responseSignals;
            std::vector<ConditioningInfo> conditioningInfos;

            static SignalProcessingNotification fromXml(const std::string& xmlContent);
            void parseFromXml(const std::string& xmlContent);
        };
    };
};

#endif // __UTPL_ESAM_MODEL_H__
