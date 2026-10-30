#include "utpl/common/debug.h"
#include "utpl/common/types/exception.h"

#include "utpl/playlist/parser.h"
#include "utpl/transcoder/input/single.h"

#include <boost/url/url.hpp>
#include <boost/url/parse.hpp>

using namespace utpl;

namespace {
    const std::string TAG_EXTM3U = "EXTM3U";
    const std::string TAG_EXTINF = "EXTINF";
}

std::vector<transcoder::InputParameters> playlist::Parser::parse(std::istream &stream) {
    std::vector<transcoder::InputParameters> inputs;

    auto m3u8 = this->_parseM3U8(stream);
    if (! m3u8) {
        throw utpl::InvalidArgumentException("Invalid M3U8 playlist");
    }

    {
        const auto &entries = m3u8->getEntries();

        for (size_t i = 1; i < entries.size(); i += 2) {
            if (i + 1 >= entries.size()) {
                break;
            }
        
            auto tagEntry = std::dynamic_pointer_cast<utils::M3U8::TagEntry>(entries[i]);
            auto uriEntry = std::dynamic_pointer_cast<utils::M3U8::UriEntry>(entries[i + 1]);

            if (!tagEntry || !uriEntry) {
                continue;
            }

            if (tagEntry->getTag() == TAG_EXTINF) {
                auto input = this->_createInputFromEntry(*tagEntry, *uriEntry);
                inputs.push_back(input);
            }
        }
    }

    return inputs;
}

std::unique_ptr<utils::M3U8> playlist::Parser::_parseM3U8(std::istream &stream) {
    auto m3u8 = utils::M3U8::parse(stream);
    if (!m3u8) {
        ERR(("Failed to parse M3U8 playlist"));

        return nullptr;
    }

    // Verify first tag is EXTM3U
    const auto &entries = m3u8->getEntries();
    if (entries.empty()) {
        ERR(("M3U8 playlist is empty"));

        return nullptr;
    }

    auto firstTag = std::dynamic_pointer_cast<utils::M3U8::TagEntry>(entries[0]);
    if (!firstTag || firstTag->getTag() != TAG_EXTM3U) {
        ERR(("First tag is not EXTM3U"));

        return nullptr;
    }

    return m3u8;
}

transcoder::InputParameters playlist::Parser::_createInputFromEntry(
    const utils::M3U8::TagEntry &tagEntry,
    const utils::M3U8::UriEntry &uriEntry
) {
    transcoder::InputParameters params;

    params.addUri(uriEntry.getUri());

    auto url = boost::urls::parse_uri(uriEntry.getUri());
    std::unique_ptr<utpl::transcoder::InputTransportParameters> base;

    if (! url) {
        throw utpl::InvalidArgumentException("Invalid URL: " + uriEntry.getUri());
    }

    if (url->scheme() == "http" || url->scheme() == "https") {
        auto hlsParams = std::make_unique<utpl::transcoder::HlsInputTransportParameters>();
        utpl::Credentials creds;
        bool valid = false;

        for (const auto &attr : tagEntry.getAttributes()) {
            if (attr.getName() == "scte35Source") {
                auto value = std::dynamic_pointer_cast<utils::M3U8::AttributeValueEnum>(attr.getValue());

                if (value) {
                    transcoder::ScteInputParameters scteParams;

                    scteParams.setSource(value->getValue() == "manifest" ? transcoder::ScteSource::MANIFEST : transcoder::ScteSource::SEGMENTS);
                    hlsParams->setScteParameters(scteParams);

                    valid = true;
                }

            } else if (attr.getName() == "bandwidth") {
                auto value = std::dynamic_pointer_cast<utils::M3U8::AttributeValueInteger>(attr.getValue());

                if (value) {
                    hlsParams->setBandwith(value->getValue());
                    valid = true;
                }

            } else {
                if (attr.getName() == "user") {
                    auto value = std::dynamic_pointer_cast<utils::M3U8::AttributeValueString>(attr.getValue());
                    
                    if (value) {
                        creds.setUsername(value->getValue());
                    }

                } else if (attr.getName() == "password") {
                    auto value = std::dynamic_pointer_cast<utils::M3U8::AttributeValueString>(attr.getValue());
                    
                    if (value) {
                        creds.setPassword(value->getValue());
                    }
                }

                if (! creds.getPassword().empty() && ! creds.getUsername().empty()) {
                    hlsParams->setCredentials(creds);

                    valid = true;
                }
            }
        }

        if (valid) {
            base = std::move(hlsParams); params.addTransportParameters(base);
        }

    } else if (url->scheme() == "srt") {
        auto srtParams = std::make_unique<transcoder::SrtInputTransportParameters>();
        bool valid = false;

        for (const auto &attr : tagEntry.getAttributes()) {
            if (attr.getName() == "mode") {
                auto value = std::dynamic_pointer_cast<utils::M3U8::AttributeValueEnum>(attr.getValue());
                
                if (value) {
                    srtParams->setMode(value->getValue() == "caller" ? 
                        transcoder::SrtProtocolMode::CALLER : transcoder::SrtProtocolMode::LISTENER);

                    valid = true;
                }

            } else if (attr.getName() == "latency") {
                auto value = std::dynamic_pointer_cast<utils::M3U8::AttributeValueInteger>(attr.getValue());
                
                if (value) {
                    srtParams->setLatency(value->getValue());

                    valid = true;
                }

            } else if (attr.getName() == "bufferSize") {
                auto value = std::dynamic_pointer_cast<utils::M3U8::AttributeValueInteger>(attr.getValue());
                
                if (value) {
                    srtParams->setBufferSize(value->getValue());

                    valid = true;
                }
            }
        }

        if (valid) {
            base = std::move(srtParams); params.addTransportParameters(base);
        }

    } else if (url->scheme() == "udp") {
        auto udpParams = std::make_unique<transcoder::UdpInputTransportParameters>();
        bool valid = false;

        if (valid) {
            base = std::move(udpParams); params.addTransportParameters(base);
        }

    } else if (url->scheme() == "file") {
        auto fileParams = std::make_unique<transcoder::FileInputTransportParameters>();
        bool valid = false;

        if (valid) {
            base = std::move(fileParams); params.addTransportParameters(base);
        }
    }

    return params;
} 