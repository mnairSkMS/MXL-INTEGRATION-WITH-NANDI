#ifndef __UTPL_TRANSCODER_INPUT_DASH_EMSG_H__
#define __UTPL_TRANSCODER_INPUT_DASH_EMSG_H__

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace utpl {
    namespace transcoder {
        namespace dash {
            /**
             * An ISO/IEC 23009-1 Event Message box ('emsg').
             *
             * For version 0, presentationTime holds presentation_time_delta, relative to the
             * earliest presentation time of the segment carrying the box. For version 1 it is
             * the absolute presentation_time on the media timeline.
             */
            struct EmsgBox {
                uint8_t              version          = 0;
                std::string          schemeIdUri;
                std::string          value;
                uint32_t             timescale        = 0;
                uint64_t             presentationTime = 0;
                uint32_t             eventDuration    = 0;
                uint32_t             id               = 0;
                std::vector<uint8_t> messageData;
            };

            /**
             * Parses the body of an 'emsg' box: everything after its size/type header, starting
             * at the FullBox version byte. Returns false for an unknown version or a truncated box.
             */
            bool parseEmsg(const uint8_t *body, std::size_t size, EmsgBox &out);

            /**
             * True for the SCTE 214-1 binary SCTE-35 schemes ("urn:scte:scte35:2013:bin" and its
             * later revisions), whose message_data is a complete splice_info_section.
             */
            bool isScte35Scheme(const std::string &schemeIdUri);

            /**
             * Finds 'emsg' boxes in an ISO BMFF byte stream delivered in arbitrary chunks.
             *
             * Only top-level boxes are walked, which is where DASH places 'emsg' (before 'moof').
             * Box payloads other than 'emsg' are skipped without being buffered, so a large
             * 'mdat' costs nothing. reset() must be called at each new download (init segment or
             * media segment), because a chunk boundary there is also a box boundary and an aborted
             * download would otherwise leave the scanner mid-box.
             */
            class EmsgScanner {
                public:
                    // An 'emsg' larger than this is skipped rather than buffered.
                    static constexpr std::size_t MAX_EMSG_SIZE = 64 * 1024;

                public:
                    void reset();

                    void push(const uint8_t *data, std::size_t size, std::vector<EmsgBox> &out);

                private:
                    std::vector<uint8_t> _header;

                    uint64_t _skipRemaining = 0;

                    bool                 _inEmsg        = false;
                    uint64_t             _emsgRemaining = 0;
                    std::vector<uint8_t> _emsgBody;

                    // A box of size 0 runs to the end of the stream, and a malformed size leaves no
                    // way to find the next header: ignore everything until the next reset().
                    bool _lost = false;
            };
        }
    }
}

#endif /* __UTPL_TRANSCODER_INPUT_DASH_EMSG_H__ */
