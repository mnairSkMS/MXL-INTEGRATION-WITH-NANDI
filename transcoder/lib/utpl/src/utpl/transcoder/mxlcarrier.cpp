#include <algorithm>
#include <cstdio>

#include "utpl/transcoder/mxlcarrier.h"

using namespace utpl::transcoder::mxl_carrier;

namespace {
    // The sink's own header of a grain: Length(16), ANC_Count(8), F(2), reserved(22).
    constexpr size_t SINK_HEADER_BYTES = 6;

    // One ANC packet in the sink's grain: C, line, offset, S, stream (32 bits); DID, SDID, data count (30 bits); the user data
    // words and the checksum (10 bits each); then padding to a 32-bit word.
    size_t packetBytesInGrain(size_t udwCount) {
        const size_t bits = 32 + 30 + 10 * udwCount + 10;

        return ((bits + 31) / 32) * 4;
    }

    size_t packetCount(size_t sectionBytes) {
        return std::max<size_t>(1, (sectionBytes + FRAGMENT_BYTES - 1) / FRAGMENT_BYTES);
    }

    // b8 is the even parity of b0..b7, b9 is the opposite of b8 (ST 291).
    uint16_t withParity(uint8_t value) {
        const uint16_t b8 = __builtin_popcount(value) & 1;

        return (uint16_t) (value | (b8 << 8) | ((b8 ? 0 : 1) << 9));
    }

    bool parityOk(uint16_t word) {
        const uint16_t b8 = (word >> 8) & 1;
        const uint16_t b9 = (word >> 9) & 1;

        return b8 == (uint16_t) (__builtin_popcount(word & 0xFF) & 1) && b9 == (uint16_t) (b8 ? 0 : 1);
    }

    struct BitWriter {
        std::vector<uint8_t> out;
        int                  fill = 0;
        uint8_t              current = 0;

        void put(uint32_t value, int bits) {
            for (int i = bits - 1; i >= 0; i--) {
                current = (uint8_t) ((current << 1) | ((value >> i) & 1));

                if (++fill == 8) {
                    out.push_back(current);
                    current = 0;
                    fill    = 0;
                }
            }
        }

        void padWithOnes() {
            while (fill) {
                put(1, 1);
            }
        }
    };

    struct BitReader {
        const uint8_t *data;
        size_t         size;
        size_t         pos = 0;

        BitReader(const uint8_t *d, size_t n) : data(d), size(n) {}

        bool has(size_t bits) const {
            return pos + bits <= size * 8;
        }

        uint32_t get(int bits) {
            uint32_t value = 0;

            for (int i = 0; i < bits; i++) {
                const size_t q = pos++;

                value = (value << 1) | ((data[q >> 3] >> (7 - (q & 7))) & 1);
            }

            return value;
        }

        void alignToByte() {
            pos = (pos + 7) & ~(size_t) 7;
        }
    };

    void packet(std::vector<uint8_t> &out, int32_t countdown, size_t index, size_t total, size_t sectionBytes, const uint8_t *fragment, size_t fragmentBytes) {
        std::vector<uint8_t> udw8;

        udw8.reserve(HEADER_BYTES + fragmentBytes);
        udw8.push_back('N');
        udw8.push_back('S');
        udw8.push_back(VERSION);
        udw8.push_back((uint8_t) total);
        udw8.push_back((uint8_t) index);

        for (int i = 0; i < 4; i++) {
            udw8.push_back((uint8_t) (((uint32_t) countdown >> (24 - 8 * i)) & 0xFF));
        }

        udw8.push_back((uint8_t) (sectionBytes >> 8));
        udw8.push_back((uint8_t) (sectionBytes & 0xFF));
        udw8.insert(udw8.end(), fragment, fragment + fragmentBytes);

        const uint16_t did = withParity(DID), sdid = withParity(SDID), count = withParity((uint8_t) udw8.size());

        uint32_t sum = (did & 0x1FF) + (sdid & 0x1FF) + (count & 0x1FF);

        std::vector<uint16_t> words;

        words.reserve(udw8.size());

        for (uint8_t byte : udw8) {
            const uint16_t word = withParity(byte);

            words.push_back(word);
            sum += word & 0x1FF;
        }

        sum &= 0x1FF;

        const uint16_t checksum = (uint16_t) (sum | (((sum >> 8) & 1) ? 0 : 0x200));

        BitWriter w;

        w.put(0, 6);    // reserved
        w.put(0, 1);    // c_not_y_channel_flag: the luma channel
        w.put(9, 11);   // line number: 9, outside the picture, as ancillary data is placed
        w.put(0, 12);   // horizontal offset
        w.put(did, 10);
        w.put(sdid, 10);
        w.put(count, 10);

        for (uint16_t word : words) {
            w.put(word, 10);
        }

        w.put(checksum, 10);
        w.padWithOnes();

        out.insert(out.end(), w.out.begin(), w.out.end());
    }

    void setError(std::string *error, const char *message, size_t at) {
        if (error) {
            char buffer[160];

            std::snprintf(buffer, sizeof(buffer), "%s (at byte %zu)", message, at);
            *error = buffer;
        }
    }
}

size_t utpl::transcoder::mxl_carrier::sizeInGrain(size_t sectionBytes) {
    const size_t total = packetCount(sectionBytes);

    size_t bytes = 0;
    size_t left  = sectionBytes;

    for (size_t i = 0; i < total; i++) {
        const size_t fragment = std::min(left, FRAGMENT_BYTES);

        bytes += packetBytesInGrain(HEADER_BYTES + fragment);
        left  -= fragment;
    }

    return bytes;
}

bool utpl::transcoder::mxl_carrier::fitsInGrain(const std::vector<size_t> &sectionBytes) {
    size_t bytes   = SINK_HEADER_BYTES;
    size_t packets = 0;

    for (size_t size : sectionBytes) {
        bytes   += sizeInGrain(size);
        packets += packetCount(size);
    }

    return packets <= 255 && bytes + GRAIN_MARGIN <= MAX_GRAIN_BYTES;
}

size_t utpl::transcoder::mxl_carrier::maxSectionBytes() {
    size_t low = 0, high = MAX_GRAIN_BYTES;

    while (low < high) {
        const size_t middle = (low + high + 1) / 2;

        if (fitsInGrain({ middle })) {
            low = middle;

        } else {
            high = middle - 1;
        }
    }

    return low;
}

std::vector<uint8_t> utpl::transcoder::mxl_carrier::pack(const std::vector<Cue> &cues) {
    std::vector<uint8_t> out;

    for (const auto &cue : cues) {
        if (cue.section.empty() || cue.section.size() > 0xFFFF) {
            continue;
        }

        const size_t total = packetCount(cue.section.size());

        if (total > 255) {
            continue;
        }

        for (size_t i = 0; i < total; i++) {
            const size_t offset = i * FRAGMENT_BYTES;

            packet(out, cue.countdown, i, total, cue.section.size(), cue.section.data() + offset, std::min(FRAGMENT_BYTES, cue.section.size() - offset));
        }
    }

    return out;
}

bool utpl::transcoder::mxl_carrier::unpack(const uint8_t *data, size_t size, std::vector<Cue> &cues, std::string *error) {
    BitReader r(data, size);

    // The cue being put together from its fragments.
    Cue    current;
    size_t expectedTotal = 0, nextIndex = 0, expectedLength = 0;

    while (r.has(6 + 1 + 11 + 12 + 30 + 10)) {
        const size_t start = r.pos / 8;

        r.get(6);
        r.get(1);
        r.get(11);
        r.get(12);

        const uint16_t did = (uint16_t) r.get(10), sdid = (uint16_t) r.get(10), count = (uint16_t) r.get(10);

        if (! parityOk(did) || ! parityOk(sdid) || ! parityOk(count)) {
            setError(error, "the ANC packet header has a wrong parity bit", start);

            return false;
        }

        const size_t udwCount = count & 0xFF;

        if (! r.has(10 * udwCount + 10)) {
            setError(error, "the ANC packet is cut short", start);

            return false;
        }

        uint32_t sum = (did & 0x1FF) + (sdid & 0x1FF) + (count & 0x1FF);

        std::vector<uint8_t> udw;

        udw.reserve(udwCount);

        for (size_t i = 0; i < udwCount; i++) {
            const uint16_t word = (uint16_t) r.get(10);

            if (! parityOk(word)) {
                setError(error, "a user data word has a wrong parity bit", start);

                return false;
            }

            sum += word & 0x1FF;
            udw.push_back((uint8_t) (word & 0xFF));
        }

        const uint16_t checksum = (uint16_t) r.get(10);

        sum &= 0x1FF;

        if (checksum != (uint16_t) (sum | (((sum >> 8) & 1) ? 0 : 0x200))) {
            setError(error, "the ANC packet has a wrong checksum", start);

            return false;
        }

        r.alignToByte();

        if ((did & 0xFF) != DID || (sdid & 0xFF) != SDID) {
            continue;   // somebody else's ancillary data
        }

        if (udw.size() < HEADER_BYTES || udw[0] != 'N' || udw[1] != 'S' || udw[2] != VERSION) {
            setError(error, "a cue packet has an unknown header", start);

            return false;
        }

        const size_t total = udw[3], index = udw[4];
        const int32_t countdown = (int32_t) (((uint32_t) udw[5] << 24) | ((uint32_t) udw[6] << 16) | ((uint32_t) udw[7] << 8) | udw[8]);
        const size_t length = ((size_t) udw[9] << 8) | udw[10];

        if (index == 0) {
            if (nextIndex != expectedTotal) {
                setError(error, "a cue started before the one before it was complete", start);

                return false;
            }

            current.section.clear();
            current.countdown = countdown;
            expectedTotal     = total;
            expectedLength    = length;
            nextIndex         = 0;

        } else if (index != nextIndex || total != expectedTotal || countdown != current.countdown) {
            setError(error, "a cue fragment is out of order", start);

            return false;
        }

        current.section.insert(current.section.end(), udw.begin() + HEADER_BYTES, udw.end());
        nextIndex = index + 1;

        if (nextIndex == expectedTotal) {
            if (current.section.size() != expectedLength) {
                setError(error, "a cue has a different length than it declares", start);

                return false;
            }

            cues.push_back(current);

            current.section.clear();
            expectedTotal = nextIndex = 0;
        }
    }

    if (nextIndex != expectedTotal) {
        setError(error, "the buffer ends inside a cue", size);

        return false;
    }

    return true;
}

void CueQueue::setRate(int num, int den) {
    this->_period = (num > 0 && den > 0) ? (1000000000ULL * (uint64_t) den + (uint64_t) num - 1) / (uint64_t) num : 0;
}

CueQueue::Add CueQueue::add(const uint8_t *section, size_t size, uint64_t spliceRunningTime, uint64_t addedAt) {
    if (! section || size == 0) {
        return Add::EMPTY;
    }

    if (! fitsInGrain({ size })) {
        return Add::TOO_LARGE;
    }

    if (this->_pending.size() >= MAX_PENDING) {
        return Add::QUEUE_FULL;
    }

    this->_pending.push_back({ std::vector<uint8_t>(section, section + size), spliceRunningTime, -1, addedAt });

    return Add::ACCEPTED;
}

std::vector<Cue> CueQueue::forFrame(uint64_t frameRunningTime) {
    std::vector<Cue>   out;
    std::vector<size_t> sizes;

    if (this->_period == 0) {
        return out;   // the frame rate is not known yet: cues wait
    }

    for (auto it = this->_pending.begin(); it != this->_pending.end();) {
        int32_t countdown = 0;

        if (it->framesLeft < 0) {
            // Not carried by any frame yet. If the frames only come now, long after the cue, it is stale.
            const bool oldCue    = it->addedAt != NONE && frameRunningTime > it->addedAt && frameRunningTime - it->addedAt > STALE_NS;
            const bool pastSplice = it->splice != NONE && frameRunningTime > it->splice && frameRunningTime - it->splice > STALE_NS;

            if (oldCue || pastSplice) {
                this->_stale++;
                it = this->_pending.erase(it);

                continue;
            }

            if (it->splice == NONE || (int64_t) (it->splice - frameRunningTime) <= (int64_t) (this->_period / 2)) {
                it->framesLeft = IMMEDIATE_FRAMES;

            } else {
                countdown = (int32_t) (((it->splice - frameRunningTime) + this->_period / 2) / this->_period);
            }
        }

        sizes.push_back(it->section.size());

        if (! fitsInGrain(sizes)) {
            sizes.pop_back();
            this->_skipped++;
            ++it;

            continue;
        }

        out.push_back({ it->section, countdown });

        if (it->framesLeft >= 0 && --it->framesLeft == 0) {
            it = this->_pending.erase(it);

        } else {
            ++it;
        }
    }

    return out;
}

bool Deduper::isNew(uint64_t time, const std::vector<uint8_t> &section) {
    // Forget what has not been seen for longer than the window.
    for (auto it = this->_seen.begin(); it != this->_seen.end();) {
        if (time > it->second && time - it->second > this->_window) {
            it = this->_seen.erase(it);

        } else {
            ++it;
        }
    }

    const std::string key(section.begin(), section.end());
    auto found = this->_seen.find(key);

    if (found != this->_seen.end()) {
        found->second = time;

        return false;
    }

    this->_seen[key] = time;

    return true;
}
