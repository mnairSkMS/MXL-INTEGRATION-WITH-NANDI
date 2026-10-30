#ifndef __UTPL_COMMON_UTILS_M3U8_H__
#define __UTPL_COMMON_UTILS_M3U8_H__

#include <memory>
#include <vector>
#include <string>

#include "utpl/common/utils/class.h"

namespace utpl {
    namespace utils {
        class M3U8 {
            public:
                class AttributeValue {
                    UTPL_CLASS_INTERFACE(AttributeValue) {}
                };

                class AttributeValueInteger : public AttributeValue {
                    public:
                        AttributeValueInteger(int value) {
                            this->_value = value;
                        }
                        
                        int getValue() const {
                            return this->_value;
                        }

                    private:
                        int _value;
                };

                class AttributeValueFloat : public AttributeValue {
                    public:
                        AttributeValueFloat(float value) {
                            this->_value = value;
                        }

                        float getValue() const {
                            return this->_value;
                        }

                    private:
                        float _value;
                };

                class AttributeValueString : public AttributeValue {
                    public:
                        AttributeValueString(const std::string &value) : _value(value) {
                        }

                        const std::string &getValue() const {
                            return this->_value;
                        }

                    private:
                        std::string _value;
                };

                class AttributeValueEnum : public AttributeValue {
                    public:
                        AttributeValueEnum(const std::string &value) : _value(value) {
                        }

                        const std::string &getValue() const {
                            return this->_value;
                        }

                    private:
                        std::string _value;
                };

                class AttributeValueRational : public AttributeValue {
                    public:
                        AttributeValueRational(float num, float den) {
                            this->_numerator   = num;
                            this->_denominator = den;
                        }

                        float getNumerator() const {
                            return this->_numerator;
                        }

                        float getDenominator() const {
                            return this->_denominator;
                        }

                    private:
                        float _numerator;
                        float _denominator;
                };

                class AttributeValueDateTime : public AttributeValue {
                    public:
                        AttributeValueDateTime(const std::string value): _value(value) {
                        }

                        const std::string getValue() const {
                            return this->_value;
                        }

                    private:
                        std::string _value;
                };

                class Attribute {
                    public:
                        Attribute(const std::string &name, std::shared_ptr<AttributeValue> value) : _name(name), _value(value) {

                        }
                        
                        const std::string &getName() const {
                            return this->_name;
                        }

                        std::shared_ptr<AttributeValue> getValue() const {
                            return this->_value;
                        }

                    private:
                        std::string _name;
                        std::shared_ptr<AttributeValue> _value;
                };

                class Entry {
                    UTPL_CLASS_INTERFACE(Entry) {}

                    public:
                };

                class TagEntry : public Entry {
                    public:
                        TagEntry(const std::string &tag) : _tagName(tag) {
                        }

                        const std::string &getTag() const {
                            return this->_tagName;
                        }

                        TagEntry &addAttribute(const std::string &name, std::shared_ptr<AttributeValue> value) {
                            this->_attributes.push_back(Attribute(name, value));

                            return *this;
                        }

                        TagEntry &addAttribute(std::shared_ptr<AttributeValue> value) {
                            return this->addAttribute(std::string(), value);
                        }

                        const std::vector<Attribute> &getAttributes() const {
                            return this->_attributes;
                        }

                    private:
                        std::string _tagName;
                        std::vector<Attribute> _attributes;
                };

                class UriEntry : public Entry {
                    public:
                        UriEntry(const std::string &uri) : _uri(uri) {
                        }

                        const std::string &getUri() const {
                            return this->_uri;
                        }

                    private:
                        std::string _uri;
                };

            public:
                M3U8() {
                }

                std::shared_ptr<TagEntry> addTagEntry(const std::string tag) {
                    auto entry = std::make_shared<TagEntry>(tag);

                    this->_entries.push_back(entry);

                    return entry;
                }

                std::shared_ptr<UriEntry> addUriEntry(const std::string uri) {
                    auto entry = std::make_shared<UriEntry>(uri);

                    this->_entries.push_back(entry);

                    return entry;
                }

                const std::vector<std::shared_ptr<Entry>> &getEntries() const {
                    return this->_entries;
                }

            public:
                std::string serialize() const;

                static std::unique_ptr<M3U8> parse(std::istream &stream);

            private:
                std::vector<std::shared_ptr<Entry>> _entries;
        };
    }
}

#endif /* __UTPL_COMMON_UTILS_M3U8_H__ */