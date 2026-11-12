#ifndef __UTPL_TRANSCODER_MXLCARRIER_H__
#define __UTPL_TRANSCODER_MXLCARRIER_H__

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace utpl {
    namespace transcoder {
        // SCTE-35 sections inside an MXL data flow (Step 5, WP2).
        //
        // An MXL data flow carries SMPTE ST 291 ancillary data packets, one grain per video frame, the grain
        // having the index of the picture it belongs to. A cue is the finished SCTE-35 `splice_info_section`, copied
        // unchanged into private ANC packets (a section is longer than one packet can hold, so it is cut into
        // fragments). The format was proved on MXL v1.1.0 in `doc/mxl/step5-plan-2026-11-11.md`, section 4.6.
        //
        // Three rules from that trial are built into the classes below:
        //  * a grain is written for every video frame, empty when there is no cue (MxlCueQueue gives the cues
        //    of every frame, possibly none): a flow written only when there is a cue lets a late reader be handed a
        //    stale grain from an earlier lap of the ring;
        //  * a cue stays in every grain from the moment it is known until its splice frame, because the ring holds
        //    only about 200 ms and a reader that is restarting or late would miss a cue written once;
        //  * a cue that would take a grain over 4096 bytes is refused before it is written, because the data sink
        //    stops writing for good when it is given one.
        //
        // The splice time is not stored as a grain index, which the writer does not know (the clock offset belongs to
        // the sink's pipeline). Each copy of a cue carries a *countdown*: the time in milliseconds from this grain to the
        // splice. The reader adds it to the time of the grain it read.
        //
        // The ANC packet identifier below is a PLACEHOLDER, not a value registered with SMPTE. Nothing outside
        // Nandi reads it yet. Replace it in one place when a real private identifier is chosen.
        namespace mxl_carrier {
            constexpr uint8_t  DID  = 0x58;   // NOT REGISTERED
            constexpr uint8_t  SDID = 0x01;   // NOT REGISTERED

            constexpr size_t   MAX_GRAIN_BYTES   = 4096;   // what mxlsink accepts for one grain
            constexpr size_t   GRAIN_MARGIN      = 64;     // kept free: the estimate below is of the packets, not of the sink's header
            constexpr size_t   HEADER_BYTES      = 11;     // 'N' 'S' version total index countdown(4) sectionLength(2)
            constexpr size_t   MAX_UDW           = 255;    // user data words of one ANC packet
            constexpr size_t   FRAGMENT_BYTES    = MAX_UDW - HEADER_BYTES;
            constexpr uint8_t  VERSION           = 2;      // 1 counted the countdown in whole frames, which put a cue up to half a frame (and sometimes a frame) off
            constexpr uint64_t NONE              = UINT64_MAX;   // "no time" (an immediate splice)

            // Bytes a section of this size takes in a grain, as the data sink writes it (RFC 8331: 32-bit words).
            size_t sizeInGrain(size_t sectionBytes);

            // True when sections of these sizes fit in one grain together.
            bool fitsInGrain(const std::vector<size_t> &sectionBytes);

            // The largest section that fits a grain alone.
            size_t maxSectionBytes();

            struct Cue {
                std::vector<uint8_t> section;     // the splice_info_section, table_id 0xFC, as it goes on the wire
                int32_t              countdown;   // milliseconds from this grain to the splice; 0 at or after the splice frame
            };

            // The ST 2038 stream (the buffer the data sink is given) holding these cues. Empty when there are none.
            // Cues that do not fit one grain are not written (the caller has checked with fitsInGrain).
            std::vector<uint8_t> pack(const std::vector<Cue> &cues);

            // The cues in an ST 2038 buffer. Packets of other identifiers are skipped. A broken packet (parity,
            // checksum, truncation, a fragment out of order) ends the read and is reported in `error`; the cues read
            // before it are kept. Returns false when something was wrong.
            bool unpack(const uint8_t *data, size_t size, std::vector<Cue> &cues, std::string *error = nullptr);

            // Writer: which cues go in the grain of each frame.
            //
            // A cue added with its splice time is in every frame from the next one until the frame at the splice
            // time, where its countdown is 0. It then stays for IMMEDIATE_FRAMES frames in all (countdown 0), so that a
            // reader a few frames late still meets it. A cue with no splice time (immediate), or one already in the
            // past, is written for IMMEDIATE_FRAMES frames with countdown 0. Not thread safe: the caller locks.
            class CueQueue {
                public:
                    static constexpr size_t   MAX_PENDING      = 32;
                    static constexpr int      IMMEDIATE_FRAMES = 5;
                    static constexpr uint64_t STALE_NS         = 3000ULL * 1000000ULL;

                    enum class Add {
                        ACCEPTED,
                        EMPTY,        // not a section (no bytes)
                        TOO_LARGE,    // would not fit a grain alone
                        QUEUE_FULL
                    };

                    // Cues can be added before the rate is known; no frame gets a cue until it is (they wait, and go stale).
                    void setRate(int num, int den);

                    // `addedAt` is the running time the cue was received at, or NONE. A cue that no frame has carried within
                    // STALE_NS of it, or whose splice time is that far gone, is dropped, not written late (the channel was idle).
                    Add add(const uint8_t *section, size_t size, uint64_t spliceRunningTime, uint64_t addedAt = NONE);

                    // The cues for the frame at this running time, in the order they were added. Cues that no longer fit
                    // together with the ones before them are left out of this grain (counted in getSkipped()).
                    std::vector<Cue> forFrame(uint64_t frameRunningTime);

                    size_t   getPending() const { return this->_pending.size(); }
                    uint64_t getSkipped() const { return this->_skipped; }
                    uint64_t getStale() const   { return this->_stale; }

                private:
                    struct Pending {
                        std::vector<uint8_t> section;
                        uint64_t             splice;       // NONE: immediate
                        int                  framesLeft;   // frames still to write once the countdown is 0; -1 before
                        uint64_t             addedAt;      // NONE when not known
                    };

                    uint64_t             _period = 0;
                    std::deque<Pending>  _pending;
                    uint64_t             _skipped = 0;
                    uint64_t             _stale   = 0;
            };

            // Reader: a cue is delivered the first time it is seen. The writer repeats it in every grain until its splice
            // frame, so the same section arriving again within the window is a repeat. A section that has been
            // missing for longer than the window is new again (an operator may send the same cue twice).
            class Deduper {
                public:
                    static constexpr uint64_t DEFAULT_WINDOW_NS = 1500ULL * 1000000ULL;

                    explicit Deduper(uint64_t windowNs = DEFAULT_WINDOW_NS) : _window(windowNs) {}

                    // `time` is any increasing time, in nanoseconds (the running time of the grain).
                    bool isNew(uint64_t time, const std::vector<uint8_t> &section);

                    size_t size() const { return this->_seen.size(); }

                private:
                    uint64_t                             _window;
                    std::map<std::string, uint64_t>      _seen;   // section bytes -> last time seen
            };
        }
    }
}

#endif /* __UTPL_TRANSCODER_MXLCARRIER_H__ */
