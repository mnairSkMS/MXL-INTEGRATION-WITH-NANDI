#ifndef __UTPL_TRANSCODER_EVENT_SCHEDULER_SCTE_H__
#define __UTPL_TRANSCODER_EVENT_SCHEDULER_SCTE_H__

#include <cstdint>
#include <vector>

#include <gst/mpegts/gst-scte-section.h>

#include "utpl/common/utils/gstreamer.h"

namespace utpl {
    namespace transcoder {
        namespace scheduler {
            /**
             * Serialises a SIT into a section, dropping the is_running_time flag that
             * gst_mpegts_section_from_scte_sit() sets. The muxer re-times the section from
             * the running-time-map instead, and refuses sections that still carry the flag.
             *
             * Takes ownership of nothing; returns a new reference the caller must unref.
             * Returns nullptr on failure.
             */
            GstMpegtsSection *sectionFromSit(GstMpegtsSCTESIT *sit);

            /**
             * Parses a pre-authored splice_info_section and re-times it for this output.
             *
             * The operator's bytes reach the wire untouched apart from pts_adjustment and the
             * CRC_32. Rather than rewriting the authored splice time - which would mean
             * re-serialising through GStreamer's SIT model, silently dropping anything that
             * model does not represent - the section is shifted wholesale by setting
             * pts_adjustment, which every compliant receiver adds to each splice time. That
             * lands the message at the scheduled moment in this output's timeline rather than
             * at whatever PTS the originating system stamped it with.
             *
             * A section requesting an immediate splice carries no time to re-base and is
             * passed through unshifted; it is muxed at the scheduled moment regardless.
             *
             * Only splice_insert and time_signal are accepted, matching the restriction the
             * channel and the HLS streamer already impose. The REST layer rejects the rest
             * earlier still.
             *
             * Returns a new reference the caller must unref, or nullptr if the section cannot
             * be parsed or does not carry the expected command.
             */
            GstMpegtsSection *sectionFromBase64(
                const std::vector<uint8_t>          &raw,
                GstClockTime                         spliceTimePts,
                GstMpegtsSCTESpliceCommandType       expectedCommand
            );

            /**
             * A copy of the section with the splice time it carries set to 0, and the CRC_32 recomputed; what a receiver
             * works out, pts_time plus pts_adjustment, is unchanged once sectionFromBase64() has set pts_adjustment.
             *
             * Why: the transport stream muxer adds its own offset to pts_adjustment and writes the sum back with
             * `section_data[4] |= pts_adjust >> 32`, which sets bit 32 and never clears it. A section whose authored
             * splice time is later than the time it is re-based to needs a negative adjustment, which is held modulo
             * 2^33 with bit 32 set. After the muxer's offset the true sum is small and has bit 32 clear, but the old bit
             * stays: the splice lands 2^32 ticks (13 hours) away. With the authored time at 0 the adjustment is the splice
             * time itself, never negative.
             *
             * Only a splice_insert or time_signal that carries a specified splice time is changed (a splice_insert that
             * is immediate, cancelled, or in component mode, and every other command, comes back as it was).
             */
            std::vector<uint8_t> withSpliceTimeZeroed(const std::vector<uint8_t> &raw);

            /**
             * Builds the running-time-map that accompanies a section to the muxer.
             *
             * Note what this map does and does not do. For a section that arrives already
             * packetized - which is every section we send, because sectionFromSit() drops
             * the is_running_time flag - mpegtsmux does NOT re-time it from this map. It
             * only adjusts pts_adjustment and recomputes the CRC; the splice times written
             * into the section are what reaches the wire. The map is used solely to request
             * key frames at the splice points. That is why sectionFromBase64() has to
             * rewrite the splice time itself rather than leaving it to the muxer.
             *
             * The keys depend on what the section actually carries, so this reads them back
             * from the section rather than assuming: "splice-time" for a time_signal, and
             * one "event-<id>-splice-time" per splice event for a splice_insert - a
             * pre-authored section may carry more than one.
             *
             * Returns a new structure, transferring ownership to the caller.
             */
            GstStructure *buildRunningTimeMap(GstMpegtsSection *section, GstClockTime runningTime);
        }
    }
}

#endif /* __UTPL_TRANSCODER_EVENT_SCHEDULER_SCTE_H__ */
