#include "utpl/common/utils.h"

#include <gtest/gtest.h>

using namespace utpl;

TEST(common_utils, base64) {
    // dd if=/dev/urandom of=data.bin bs=128 count=1
    // cat data.bin | basenc --base64url
    // cat data.bin | xxd -i
    static std::vector<uint8_t> data = {
        0x5c, 0x56, 0x00, 0xd3, 0x5d, 0x81, 0xcd, 0xfb, 0x1f, 0x84, 0x7f, 0xaf,
        0x87, 0x3f, 0xda, 0xc3, 0xa5, 0x41, 0x46, 0x04, 0x44, 0xef, 0x22, 0x37,
        0xd9, 0xc1, 0x86, 0x40, 0x3d, 0x95, 0x33, 0x1f, 0x12, 0x6e, 0xaf, 0xd1,
        0xc7, 0x12, 0x23, 0xe3, 0xde, 0xfe, 0xd4, 0x45, 0x0f, 0x34, 0x04, 0x2b,
        0x0b, 0xed, 0x04, 0x52, 0xa7, 0xe3, 0xa6, 0xe9, 0x71, 0x5c, 0xcf, 0xa3,
        0xf6, 0x44, 0x34, 0x75, 0xd8, 0x8c, 0x1a, 0x7f, 0x6d, 0x98, 0x56, 0xf3,
        0x74, 0xaa, 0x52, 0xf7, 0x94, 0xf0, 0x9d, 0x6a, 0x6d, 0x60, 0xaa, 0xc6,
        0x7d, 0xf2, 0x16, 0xba, 0x70, 0x8b, 0x56, 0x72, 0xe8, 0x4d, 0x47, 0xb5,
        0x60, 0x01, 0xa1, 0xcb, 0xc0, 0x85, 0x56, 0x9c, 0xad, 0x96, 0xdf, 0x24,
        0x34, 0x99, 0x96, 0x86, 0x79, 0xee, 0xb7, 0x6e, 0x9e, 0xa8, 0x54, 0x8f,
        0x00, 0xce, 0x58, 0x97, 0x51, 0x94, 0xd4, 0xc4
    };

    static const std::string base64 = 
        "XFYA012BzfsfhH+vhz/aw6VBRgRE7yI32cGGQD2VMx8Sbq/RxxIj497+1EUPNAQrC+0EUqfjpulx"
        "XM+j9kQ0ddiMGn9tmFbzdKpS95TwnWptYKrGffIWunCLVnLoTUe1YAGhy8CFVpytlt8kNJmWhnnu"
        "t26eqFSPAM5Yl1GU1MQ=";

    static const std::string base64Url = 
        "XFYA012BzfsfhH-vhz_aw6VBRgRE7yI32cGGQD2VMx8Sbq_RxxIj497-1EUPNAQrC-0EUqfjpulx"
        "XM-j9kQ0ddiMGn9tmFbzdKpS95TwnWptYKrGffIWunCLVnLoTUe1YAGhy8CFVpytlt8kNJmWhnnu"
        "t26eqFSPAM5Yl1GU1MQ";

    ASSERT_EQ(utils::toBase64(data.data(), data.size(), false), base64);
    ASSERT_EQ(utils::toBase64(data.data(), data.size(), true),  base64Url);

    ASSERT_EQ(utils::fromBase64(base64),    data);
    ASSERT_EQ(utils::fromBase64(base64Url), data);
}

// fromBase64() must return exactly as many bytes as were encoded, never the padded
// upper bound. Decoders that size the output buffer from the input length and skip the
// final resize hand back 1-2 trailing zero bytes, which corrupts any length-sensitive
// payload (an MPEG-TS section, for example) appended straight from the result.
TEST(common_utils, base64DecodedLengthIsExact) {
    // Lengths 1..3 mod 3 cover the two-pad, one-pad and no-pad encodings.
    for (size_t length = 1; length <= 64; length++) {
        std::vector<uint8_t> data(length);

        for (size_t i = 0; i < length; i++) {
            data[i] = static_cast<uint8_t>(i * 7 + 1);
        }

        const auto encoded = utils::toBase64(data);
        const auto decoded = utils::fromBase64(encoded);

        ASSERT_EQ(decoded.size(), length) << "round-trip of " << length << " bytes";
        ASSERT_EQ(decoded, data)          << "round-trip of " << length << " bytes";
    }
}