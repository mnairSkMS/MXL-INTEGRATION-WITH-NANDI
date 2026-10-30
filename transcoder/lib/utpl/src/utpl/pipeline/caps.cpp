#include <unordered_map>

#include "utpl/common/utils.h"
#include "utpl/common/debug.h"
#include "utpl/common/types/exception.h"

#include "utpl/pipeline/caps.h"

using namespace utpl;


static const std::unordered_map<std::string, Codec> _codecMap = {
    { "x-raw",  Codec::ES   },
    { "x-h264", Codec::H264 },
    { "x-h265", Codec::H265 },
    { "x-ac3",  Codec::AC3  },
    { "x-eac3", Codec::EAC3 }
};

pipeline::Caps::Caps(const GstCapsPtr &ptr) : GstWrapper(ptr) {

}

const std::string &pipeline::Caps::getName() const {
    auto *caps = (*this)->get();

    if (! caps) {
        this->_name.clear();

    } else {
        auto *str = gst_caps_get_structure(caps, 0);
        if (str) {
            this->_name = gst_structure_get_name(str);
        }
    }

    return this->_name;
}

std::string pipeline::Caps::toString() const {
    std::string ret;

    {
        gchar *str = gst_caps_to_string((*this)->get());

        if (str) {
            ret = str;

            g_free(str);
        }
    }

    return ret;
}

MediaType pipeline::Caps::getMediaType() const {
    auto arr = utils::split(this->getName(), '/');

    if (! arr.empty()) {
        if (arr[0] == "audio") {
            return MediaType::AUDIO;
        }

        if (arr[0] == "video") {
            return MediaType::VIDEO;
        }
    }

    if (
        this->getCodec() == Codec::SUB_DVB ||
        this->getCodec() == Codec::SUB_TTX
    ) {
        return MediaType::CAPTION;
    }

    return MediaType::UNKNOWN;
}

Container pipeline::Caps::getContainer() const {
    auto arr = utils::split(this->getName(), '/');

    if (arr.size() == 2) {
        if (arr[1] == "mpegts") {
            return Container::MPEGTS;
        }

        if (arr[1] == "x-raw") {
            return Container::ES;
        }
    }

    return Container::UNKNOWN;
}

Codec pipeline::Caps::getCodec() const {
    auto arr = utils::split(this->getName(), '/');

    if (arr.size() == 2) {
        auto it = _codecMap.find(arr[1]);
        if (it != _codecMap.end()) {
            return it->second;

        } else if (arr[1] == "mpeg") {
            auto *str = gst_caps_get_structure((*this)->get(), 0);
            if (str) {
                int version;

                if (gst_structure_get_int(str, "mpegversion", &version)) {
                    switch (version) {
                        case 1:
                            {
                                int layer;

                                if (gst_structure_get_int(str, "layer", &layer)) {
                                    if (layer == 1) {
                                        return Codec::MP1;

                                    } else if (layer == 2) {
                                        return Codec::MP2;

                                    } else if (layer == 3) {
                                        return Codec::MP3;

                                    } else {
                                        ERR(("Unknown MPEG-1 layer number '{}'", layer));
                                    }

                                } else {
                                    return Codec::MP1;
                                }
                            }
                            break;

                        case 2: 
                            return Codec::MP2;

                        case 4: 
                            return Codec::AAC;
                    }
                }
            }

        } else if (arr[0] == "subpicture") {
            if (arr[1] == "x-dvb") {
                return Codec::SUB_DVB;
            }

        } else if (arr[0] == "application") {
            if (arr[1] == "x-teletext") {
                return Codec::SUB_TTX;
            }
        }
    }

    return Codec::UNKNOWN;
}

void pipeline::Caps::setMediaFormat(MediaType media, Container container, Codec codec) {
    std::string mediaStr;
    std::string dataStr;

    switch (media) {
        case MediaType::AUDIO: mediaStr = "audio"; break;
        case MediaType::VIDEO: mediaStr = "video"; break;
        case MediaType::CAPTION:
            if (codec == Codec::SUB_DVB) {
                mediaStr = "subpicture";

            } else if (codec == Codec::SUB_TTX) {
                mediaStr = "application";
            }
            break;

        default:
            ERR(("Unknown media type! '{}'", (int)media));
    }

    if (! mediaStr.empty()) {
        switch (codec) {
            case Codec::AC3:     dataStr = "x-ac3";      break;
            case Codec::EAC3:    dataStr = "x-eac3";     break;
            case Codec::ES:      dataStr = "x-raw";      break;
            case Codec::H264:    dataStr = "x-h264";     break;
            case Codec::H265:    dataStr = "x-h265";     break;
            case Codec::SUB_DVB: dataStr = "x-dvb";      break;
            case Codec::SUB_TTX: dataStr = "x-teletext"; break;

            case Codec::AAC:
            case Codec::MP1:
            case Codec::MP2:
            case Codec::MP3: 
                dataStr = "mpeg"; 
                break;

            default:
                ERR(("Unknown Codec! '{}'", (int) codec));
        }

        if (! dataStr.empty()) {
            auto *str = gst_caps_get_structure((*this)->get(), 0);        

            auto name = mediaStr + "/" + dataStr;

            gst_structure_set_name(str, name.c_str());

            switch (codec) {
                case Codec::MP1:
                case Codec::MP2:
                    if (media == MediaType::AUDIO) {
                        this->setInt("mpegversion", 1);

                        if (codec == Codec::MP1) {
                            this->setInt("layer", 1);
                        } else {
                            this->setInt("layer", 2);
                        }

                    } else {
                        if (codec == Codec::MP1) {
                            this->setInt("mpegversion", 1);
                        } else {
                            this->setInt("mpegversion", 2);
                        }
                    }
                    break;

                case Codec::MP3:
                    {
                        this->setInt("mpegversion", 1);
                        this->setInt("layer", 3);
                    }
                    break;

                case Codec::AAC:
                    {
                        this->setInt("mpegversion", 4);
                    }
                    break;
            }
        }
    }
}

int pipeline::Caps::getInt(const std::string &name, int defaultValue) const {
    auto *caps = (*this)->get();

    if (caps) {
        auto *str = gst_caps_get_structure(caps, 0);
        if (str) {
            gint value;

            if (gst_structure_get_int(str, name.c_str(), &value)) {
                return value;
            }
        }
    }

    return defaultValue;
}

std::string pipeline::Caps::getString(const std::string &name, const std::string &defaultValue) const {
    auto *caps = (*this)->get();

    if (caps) {
        auto *str = gst_caps_get_structure(caps, 0);
        if (str) {
            const auto *s = gst_structure_get_string(str, name.c_str());
            if (s) {
                return s;
            }
        }
    }

    return defaultValue;
}

std::pair<int, int> pipeline::Caps::getFraction(const std::string &name, const std::pair<int, int> &defaultValue) const {
    auto *caps = (*this)->get();

    if (caps) {
        auto *str = gst_caps_get_structure(caps, 0);
        if (str) {
            gint numerator, denominator;

            if (gst_structure_get_fraction(str, name.c_str(), &numerator, &denominator)) {
                return { numerator, denominator };
            }
        }
    }

    return defaultValue;
}

void pipeline::Caps::setInt(const std::string &name, int value) {
    auto *caps = (*this)->get();

    if (! caps) {
        WARN(("Internal Caps object is null!"));

    } else {
        auto *str = gst_caps_get_structure(caps, 0);
        if (str) {
            GValue val = G_VALUE_INIT;

            g_value_init(&val, G_TYPE_INT);
            g_value_set_int(&val, value);

            gst_structure_set_value(str, name.c_str(), &val);

            g_value_unset(&val);
        }
    }
}

void pipeline::Caps::setString(const std::string &name, const std::string &value) {
    auto *caps = (*this)->get();

    if (! caps) {
        WARN(("Internal Caps object is null!"));
        
    } else {
        auto *str = gst_caps_get_structure(caps, 0);
        if (str) {
            GValue val = G_VALUE_INIT;

            g_value_init(&val, G_TYPE_STRING);
            g_value_set_string(&val, value.c_str());

            gst_structure_set_value(str, name.c_str(), &val);

            g_value_unset(&val);
        }
    }
}

void pipeline::Caps::setFraction(const std::string &name, const std::pair<int, int> &value) {
    auto *caps = (*this)->get();

    if (! caps) {
        WARN(("Internal Caps object is null!"));

    } else {
        auto *str = gst_caps_get_structure(caps, 0);
        if (str) {
            GValue val = G_VALUE_INIT;

            g_value_init(&val, GST_TYPE_FRACTION);
            gst_value_set_fraction(&val, value.first, value.second);

            gst_structure_set_value(str, name.c_str(), &val);

            g_value_unset(&val);
        }
    }
}

void pipeline::Caps::setValue(const std::string &name, GValue *value) {
    auto *caps = (*this)->get();

    if (! caps) {
        WARN(("Internal Caps object is null!"));

    } else {
        auto *str = gst_caps_get_structure(caps, 0);
        if (str) {
            gst_structure_set_value(str, name.c_str(), value);
        }
    }
}

int pipeline::Caps::getHeight(int defaultValue) const {
    return this->getInt("height", defaultValue);
}

int pipeline::Caps::getWidth(int defaultValue) const {
    return this->getInt("width", defaultValue);
}

Fraction pipeline::Caps::getFrameRate(const Fraction &defaultValue) const {
    auto f = this->getFraction("framerate", { 
        defaultValue.getNumerator(), 
        defaultValue.getDenominator() 
    });
    
    return Fraction(f.first, f.second);
}

Interlace pipeline::Caps::getInterlace(Interlace defaultValue) const {
    auto str = this->getString("interlace-mode");

    if (str == "progressive") {
        return Interlace::PROGRESSIVE;

    } else if (str == "interleaved") {
        return Interlace::INTERLEAVED;
    }

    return defaultValue;
}

PixelFormat pipeline::Caps::getFormat(PixelFormat defaultValue) const {
    auto str = this->getString("format");

    if (! str.empty()) {
        return fromGst(gst_video_format_from_string(str.c_str()));
    }

    return defaultValue;
}

void pipeline::Caps::setHeight(int value) {
    this->setInt("height", value);
}

void pipeline::Caps::setWidth(int value) {
    this->setInt("width", value);
}

void pipeline::Caps::setFrameRate(const Fraction &value) {
    this->setFraction("framerate", { 
        value.getNumerator(), 
        value.getDenominator() 
    });
}

void pipeline::Caps::setInterlace(Interlace interlace) {
    switch (interlace) {
        case Interlace::INTERLEAVED: this->setString("interlace-mode", "interleaved"); break;
        case Interlace::PROGRESSIVE: this->setString("interlace-mode", "progressive"); break;
        default:
            ERR(("Not supported Interlace value'{}'", (int) interlace));
    }
}

void pipeline::Caps::setFormat(PixelFormat format) {
    this->setString("format", gst_video_format_to_string(toGst(format)));
}

int pipeline::Caps::getRate(int defaultValue) const {
    return this->getInt("rate", defaultValue);
}

int pipeline::Caps::getChannels(int defaultValue) const {
    return this->getInt("channels", defaultValue);
}

void pipeline::Caps::setRate(int value) {
    this->setInt("rate", value);
}

void pipeline::Caps::setChannels(int value) {
    this->setInt("channels", value);
}

void pipeline::Caps::clear() {
    while (gst_caps_get_size((*this)->get())) {
        gst_caps_remove_structure((*this)->get(), 0);
    }
}

pipeline::CapsPtr pipeline::Caps::fromString(const std::string &str) {
    if (str.empty()) {
        throw utpl::InvalidArgumentException("Caps string is empty");
    }

    auto ptr = pipeline::GstCapsPtr::wrap(gst_caps_from_string(str.c_str()));
    if (! ptr) {
        throw utpl::InvalidArgumentException("Error parsing caps string! (" + str + ")");
    }

    return std::make_shared<pipeline::Caps>(ptr);
}

pipeline::CapsPtr pipeline::Caps::create() {
    auto caps = pipeline::GstCapsPtr::wrap(
        gst_caps_new_empty()
    );

    gst_caps_append_structure(
        *caps,
        gst_structure_new_empty("ANY")
    );

    return std::make_shared<pipeline::Caps>(caps);
}