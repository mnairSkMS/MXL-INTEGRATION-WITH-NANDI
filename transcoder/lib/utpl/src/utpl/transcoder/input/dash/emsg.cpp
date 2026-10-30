#include "emsg.h"

#include <algorithm>
#include <cstring>

using namespace utpl::transcoder;

namespace {
    constexpr std::size_t BOX_HEADER_SIZE       = 8;
    constexpr std::size_t BOX_LARGE_HEADER_SIZE = 16;

    uint32_t _readU32(const uint8_t *p) {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    }

    uint64_t _readU64(const uint8_t *p) {
        return (uint64_t(_readU32(p)) << 32) | _readU32(p + 4);
    }

    // Reads a NUL-terminated string at pos, advancing pos past the terminator.
    bool _readString(const uint8_t *body, std::size_t size, std::size_t &pos, std::string &out) {
        const auto *begin = body + pos;
        const auto *end   = static_cast<const uint8_t *>(memchr(begin, 0, size - pos));

        if (! end) {
            return false;
        }

        out.assign(reinterpret_cast<const char *>(begin), end - begin);
        pos += (end - begin) + 1;

        return true;
    }

    bool _readFields(const uint8_t *body, std::size_t size, std::size_t &pos, std::size_t count) {
        return size - pos >= count;
    }
}

bool dash::parseEmsg(const uint8_t *body, std::size_t size, EmsgBox &out) {
    std::size_t pos = 4; // version (8) + flags (24)

    if (size < pos) {
        return false;
    }

    out.version = body[0];

    if (out.version == 0) {
        if (! _readString(body, size, pos, out.schemeIdUri) || ! _readString(body, size, pos, out.value)) {
            return false;
        }

        if (! _readFields(body, size, pos, 16)) {
            return false;
        }

        out.timescale        = _readU32(body + pos);
        out.presentationTime = _readU32(body + pos + 4);
        out.eventDuration    = _readU32(body + pos + 8);
        out.id               = _readU32(body + pos + 12);
        pos += 16;

    } else if (out.version == 1) {
        if (! _readFields(body, size, pos, 20)) {
            return false;
        }

        out.timescale        = _readU32(body + pos);
        out.presentationTime = _readU64(body + pos + 4);
        out.eventDuration    = _readU32(body + pos + 12);
        out.id               = _readU32(body + pos + 16);
        pos += 20;

        if (! _readString(body, size, pos, out.schemeIdUri) || ! _readString(body, size, pos, out.value)) {
            return false;
        }

    } else {
        return false;
    }

    out.messageData.assign(body + pos, body + size);

    return true;
}

bool dash::isScte35Scheme(const std::string &schemeIdUri) {
    static const std::string prefix = "urn:scte:scte35:";
    static const std::string suffix = ":bin";

    return
        schemeIdUri.size() > prefix.size() + suffix.size() &&
        schemeIdUri.compare(0, prefix.size(), prefix) == 0 &&
        schemeIdUri.compare(schemeIdUri.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void dash::EmsgScanner::reset() {
    this->_header.clear();
    this->_emsgBody.clear();

    this->_skipRemaining = 0;
    this->_inEmsg        = false;
    this->_emsgRemaining = 0;
    this->_lost          = false;
}

void dash::EmsgScanner::push(const uint8_t *data, std::size_t size, std::vector<EmsgBox> &out) {
    std::size_t pos = 0;

    while (pos < size && ! this->_lost) {
        if (this->_skipRemaining > 0) {
            auto n = std::min<uint64_t>(this->_skipRemaining, size - pos);

            this->_skipRemaining -= n;
            pos += n;

            continue;
        }

        if (this->_inEmsg) {
            auto n = std::min<uint64_t>(this->_emsgRemaining, size - pos);

            this->_emsgBody.insert(this->_emsgBody.end(), data + pos, data + pos + n);
            this->_emsgRemaining -= n;
            pos += n;

            if (this->_emsgRemaining == 0) {
                EmsgBox box;

                if (parseEmsg(this->_emsgBody.data(), this->_emsgBody.size(), box)) {
                    out.push_back(std::move(box));
                }

                this->_emsgBody.clear();
                this->_inEmsg = false;
            }

            continue;
        }

        // Collect a box header; a 64-bit size needs the extended 16-byte form.
        {
            std::size_t wanted = BOX_HEADER_SIZE;

            if (this->_header.size() >= 4 && _readU32(this->_header.data()) == 1) {
                wanted = BOX_LARGE_HEADER_SIZE;
            }

            if (this->_header.size() < wanted) {
                auto n = std::min(wanted - this->_header.size(), size - pos);

                this->_header.insert(this->_header.end(), data + pos, data + pos + n);
                pos += n;

                // The first 4 bytes may have just revealed a large size: re-evaluate.
                continue;
            }
        }

        {
            uint64_t    boxSize    = _readU32(this->_header.data());
            std::size_t headerSize = BOX_HEADER_SIZE;
            bool        isEmsg     = memcmp(this->_header.data() + 4, "emsg", 4) == 0;

            if (boxSize == 1) {
                boxSize    = _readU64(this->_header.data() + 8);
                headerSize = BOX_LARGE_HEADER_SIZE;
            }

            this->_header.clear();

            if (boxSize == 0 || boxSize < headerSize) {
                this->_lost = true;
                break;
            }

            if (isEmsg && boxSize - headerSize <= MAX_EMSG_SIZE) {
                this->_inEmsg        = true;
                this->_emsgRemaining = boxSize - headerSize;
                this->_emsgBody.reserve(this->_emsgRemaining);

                // An empty body is still a (malformed) box; finish it on the next iteration.
                if (this->_emsgRemaining == 0) {
                    this->_inEmsg = false;
                }

            } else {
                this->_skipRemaining = boxSize - headerSize;
            }
        }
    }
}
