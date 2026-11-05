#include <map>

#include <boost/url/url.hpp>
#include <boost/url/parse.hpp>

#include "utpl/common/types.h"
#include "utpl/common/debug.h"
#include "utpl/common/utils.h"

namespace urls = boost::urls;

using namespace utpl;

static const std::map<MediaType, std::string> mediaTypeMap = {
    { MediaType::UNKNOWN, "unknown" },
    { MediaType::AUDIO,   "audio"   },
    { MediaType::VIDEO,   "video"   },
    { MediaType::CAPTION, "caption" }
};

static const std::string unknownMapping = "unknown-mapping";


const std::string &utpl::toString(MediaType type) {
    auto it = mediaTypeMap.find(type);
    if (it != mediaTypeMap.end()) {
        return it->second;
    }

    return unknownMapping;
}

static const std::map<Container, std::string> containersMap = {
    { Container::ES,     "es"     },
    { Container::FMP4,   "fmp4"   },
    { Container::MPEGTS, "mpegts" }
};

const std::string &utpl::toString(Container container) {
    auto it = containersMap.find(container);
    if (it != containersMap.end()) {
        return it->second;
    }

    return unknownMapping;
}

static const std::map<Codec, std::string> codecsMap = {
    { Codec::AAC,         "AAC" },
    { Codec::AC3,         "AC3" },
    { Codec::EAC3,        "EAC3" },
    { Codec::ES,          "ES" },
    { Codec::H264,        "H264" },
    { Codec::H265,        "H265" },
    { Codec::MP1,         "MP1" },
    { Codec::MP2,         "MP2" },
    { Codec::MP3,         "MP3" },
    { Codec::MXL,         "MXL" },
    { Codec::PASSTHROUGH, "PASSTHROUGH" },
    { Codec::SUB_DVB,     "SUB_DVB" },
    { Codec::SUB_TTX,     "SUB_TTX" },
    { Codec::UNKNOWN,     "UNKNOWN" }
};

const std::string &utpl::toString(Codec codec) {
    auto it = codecsMap.find(codec);
    if (it != codecsMap.end()) {
        return it->second;
    }

    return unknownMapping;
}

struct PixelFormatMapping {
    std::string    name;
    GstVideoFormat target;
};

static const std::map<PixelFormat, PixelFormatMapping> pixelFormatMap = {
    { PixelFormat::UNKNOWN, { "UNKNOWN", GST_VIDEO_FORMAT_UNKNOWN }},

    { PixelFormat::I420, { "I420", GST_VIDEO_FORMAT_I420 }},
    { PixelFormat::YV12, { "YV12", GST_VIDEO_FORMAT_YV12 }},
    { PixelFormat::I422, { "I422", GST_VIDEO_FORMAT_Y42B }},
    { PixelFormat::I444, { "I444", GST_VIDEO_FORMAT_Y444 }},

    { PixelFormat::UYVY, { "UYVY", GST_VIDEO_FORMAT_UYVY }},
    { PixelFormat::YUY2, { "YUY2", GST_VIDEO_FORMAT_YUY2 }},
    { PixelFormat::YVYU, { "YVYU", GST_VIDEO_FORMAT_YVYU }},

    { PixelFormat::NV12, { "NV12", GST_VIDEO_FORMAT_NV12 }},
    { PixelFormat::NV21, { "NV21", GST_VIDEO_FORMAT_NV21 }},

    { PixelFormat::BGRA, { "BGRA", GST_VIDEO_FORMAT_BGRA }}
};

const std::string &utpl::toString(PixelFormat format) {
    auto it = pixelFormatMap.find(format);
    if (it != pixelFormatMap.end()) {
        return it->second.name;
    }

    return unknownMapping;
}

PixelFormat utpl::parsePixelFormat(const std::string &str) {
    auto it = std::find_if(pixelFormatMap.begin(), pixelFormatMap.end(), [&str](const auto &p) {
        if (p.second.name == str) {
            return true;
        }

        return false;
    });

    if (it == pixelFormatMap.end()) {
        return PixelFormat::UNKNOWN;
    }

    return it->first;
}

GstVideoFormat utpl::toGst(PixelFormat format) {
    auto it = pixelFormatMap.find(format);
    if (it != pixelFormatMap.end()) {
        return it->second.target;
    }

    ERR(("Unknown Pixel format mapping! {}", (int) format));

    return GST_VIDEO_FORMAT_UNKNOWN;
}

PixelFormat utpl::fromGst(GstVideoFormat format) {
    auto it = std::find_if(pixelFormatMap.begin(), pixelFormatMap.end(), [format](const auto &p) {
        if (p.second.target == format) {
            return true;
        }

        return false;
    });

    if (it == pixelFormatMap.end()) {
        return PixelFormat::UNKNOWN;
    }

    return it->first;
}

Protocol _parseProtocolSimple(const std::string &url) {
    auto schemeId = boost::urls::string_to_scheme(url);

    switch (schemeId) {
        case urls::scheme::none:
        case urls::scheme::file:
            return utpl::Protocol::FILE;

        case urls::scheme::http:
        case urls::scheme::https:
            return utpl::Protocol::HTTP;
            
        default:
            if (url == "pls") {
                return utpl::Protocol::PLAYLIST;            

            } else if (url == "hls") {
                return utpl::Protocol::HLS;

            } else if (url == "dash") {
                return utpl::Protocol::DASH;

            } else if (url == "srt") {
                return utpl::Protocol::SRT;

            } else if (url == "udp") {
                return utpl::Protocol::UDP;

            } else if (url == "s3") {
                return utpl::Protocol::S3;
            } else if (url == "ndi") {
                return utpl::Protocol::NDI;

            } else if (url == "rtmp" || url == "rtmps") {
                return utpl::Protocol::RTMP;

            } else if (url == "mxl") {
                return utpl::Protocol::MXL;
            }
    }

    return utpl::Protocol::UNKNOWN;
}

Protocol _parseProtocol(boost::urls::url_view url, Protocol &subprotocol) {
    auto parts = utils::split(url.scheme(), '+');

    if (parts.size() > 1) {
        subprotocol = _parseProtocolSimple(utils::toLower(parts[1]));

    } else {
        subprotocol = utpl::Protocol::NONE;
    }

    return _parseProtocolSimple(utils::toLower(parts[0]));
}

Protocol utpl::parseProtocol(const std::string &uri, Protocol &subprotocol) {
    auto url = urls::parse_uri(uri).value();

    return _parseProtocol(url, subprotocol);
}

Protocol utpl::parseProtocolAndPath(const std::string &uri, Protocol &subprotocol, std::string &path) {
    auto url = urls::parse_uri(uri).value();

    path = url.path();

    return _parseProtocol(url, subprotocol);
}