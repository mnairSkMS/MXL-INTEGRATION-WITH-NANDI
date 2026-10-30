#include <sstream>

#include "utpl/common/utils/m3u8.h"
#include "utpl/common/utils.h"
#include "utpl/common/debug.h"

using namespace utpl;

#define ENDL "\n"

std::string utils::M3U8::serialize() const {
    std::string ret;

    for (auto &entry : this->_entries) {
        {
            auto tagEntry = std::dynamic_pointer_cast<TagEntry>(entry);
            if (tagEntry) {
                auto &attrs = tagEntry->getAttributes();

                ret += ("#" + tagEntry->getTag());

                if (! attrs.empty()) {
                    ret += ":";

                    for (int i = 0; i < attrs.size(); i++) {
                        auto &attr = attrs[i];

                        if (! attr.getName().empty()) {
                            ret += attr.getName();
                            ret += "=";                            
                        }

                        if (std::dynamic_pointer_cast<AttributeValueInteger>(attr.getValue())) {
                            ret += std::to_string(
                                std::reinterpret_pointer_cast<AttributeValueInteger>(attr.getValue())->getValue()
                            );

                        } else if (std::dynamic_pointer_cast<AttributeValueFloat>(attr.getValue())) {
                            const auto &floatValue = std::reinterpret_pointer_cast<AttributeValueFloat>(attr.getValue());

                            {
                                std::ostringstream out;

                                out.precision(3);
                                out << std::fixed << floatValue->getValue();

                                ret += out.str();
                            }

                        } else if (std::dynamic_pointer_cast<AttributeValueString>(attr.getValue())) {
                            // TODO: escape apostrophies
                            ret += ("\"" + std::reinterpret_pointer_cast<AttributeValueString>(attr.getValue())->getValue() + "\"");

                        } else if (std::dynamic_pointer_cast<AttributeValueEnum>(attr.getValue())) {

                            ret += std::reinterpret_pointer_cast<AttributeValueEnum>(attr.getValue())->getValue();

                        } else if (std::dynamic_pointer_cast<AttributeValueRational>(attr.getValue())) {
                            auto rational = std::reinterpret_pointer_cast<AttributeValueRational>(attr.getValue());

                            {
                                std::ostringstream out;

                                out.precision(3);
                                out << std::fixed << rational->getNumerator();
                                out << "/";
                                out << rational->getDenominator();

                                ret += out.str();
                            }
                        } else if(std::dynamic_pointer_cast<AttributeValueDateTime>(attr.getValue())) {
                            ret += std::reinterpret_pointer_cast<AttributeValueString>(attr.getValue())->getValue();
                        }

                        if (i != attrs.size() - 1) {
                            ret += ",";
                        }
                    }
                }
                
                ret += ENDL;
                continue;
            }
        }

        {
            auto uriEntry = std::dynamic_pointer_cast<UriEntry>(entry);
            if (uriEntry) {
                ret += uriEntry->getUri();

                ret += ENDL;
                continue;
            }
        }

        ERR(("Have not supported entry type!"));
    }


    return ret;
}

std::unique_ptr<utils::M3U8> utils::M3U8::parse(std::istream &stream) {
    auto m3u8 = std::make_unique<M3U8>();

    {
        std::string line;

        while (utils::getline(stream, line)) {
            if (line.empty()) {
                continue;
            }

            if (line[0] == '#') {
                line = line.substr(1);

                size_t colonPos = line.find(':');
                std::string tagName;
                std::string attrStr;

                if (colonPos != std::string::npos) {
                    tagName = line.substr(0, colonPos);
                    attrStr = line.substr(colonPos + 1);

                } else {
                    tagName = line;
                }

                auto tagEntry = m3u8->addTagEntry(tagName);

                if (! attrStr.empty()) {
                    size_t pos = 0;
                    
                    while (pos < attrStr.length()) {
                        size_t commaPos = attrStr.find(',', pos);
                        std::string attr = attrStr.substr(pos, commaPos - pos);

                        size_t equalPos = attr.find('=');
                        std::string name;
                        std::string value;

                        if (equalPos != std::string::npos) {
                            name = attr.substr(0, equalPos);
                            value = attr.substr(equalPos + 1);

                        } else if (!attr.empty()) {
                            value = attr;
                        }

                        if (value.empty()) {
                            tagEntry->addAttribute(name, std::make_shared<AttributeValueEnum>(name));

                        } else if (value[0] == '"' && value[value.length()-1] == '"') {
                            tagEntry->addAttribute(
                                name,
                                std::make_shared<AttributeValueString>(value.substr(1, value.length()-2))
                            );

                        } else if (value.find('/') != std::string::npos) {
                            // A '/' or a '.' only suggests a rational or a float - it is also what
                            // a path or a file name looks like. std::stof throws on those, so each
                            // conversion falls back to a plain enum value the way the integer
                            // branch below already does. Without this an "#EXTINF:-1,clip.mp4"
                            // title, which is what any real rundown carries, aborts the parse.
                            try {
                                size_t slashPos = value.find('/');
                                float num = std::stof(value.substr(0, slashPos));
                                float den = std::stof(value.substr(slashPos + 1));
                                tagEntry->addAttribute(name, std::make_shared<AttributeValueRational>(num, den));

                            } catch (const std::logic_error &e) {
                                tagEntry->addAttribute(name, std::make_shared<AttributeValueEnum>(value));
                            }

                        } else if (value.find('.') != std::string::npos) {
                            try {
                                tagEntry->addAttribute(
                                    name,
                                    std::make_shared<AttributeValueFloat>(std::stof(value))
                                );

                            } catch (const std::logic_error &e) {
                                tagEntry->addAttribute(name, std::make_shared<AttributeValueEnum>(value));
                            }

                        } else {
                            try {
                                tagEntry->addAttribute(name, std::make_shared<AttributeValueInteger>(std::stoi(value)));

                            } catch (const std::logic_error &e) {
                                // logic_error covers both invalid_argument and out_of_range.
                                tagEntry->addAttribute(name, std::make_shared<AttributeValueEnum>(value));
                            }
                        }

                        if (commaPos == std::string::npos) {
                            break;
                        }
                        pos = commaPos + 1;
                    }
                }
                
            } else {
                m3u8->addUriEntry(line);
            }
        }
    }

    return m3u8;
}