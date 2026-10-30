#include <tsduck/tsduck.h>

#include "utpl/common/utils/gstreamer.h"
#include "utpl/common/debug.h"

#include "scte.h"

using namespace utpl;


// splice_info_section fixed header: table_id, section_length, protocol_version,
// encrypted_packet + encryption_algorithm + pts_adjustment, cw_index, tier,
// splice_command_length, splice_command_type.
static constexpr size_t   SECTION_MIN_SIZE = 14;
static constexpr size_t   CRC_SIZE         = 4;
static constexpr uint64_t PTS_MASK         = 0x1FFFFFFFFull;


static const char *_commandName(GstMpegtsSCTESpliceCommandType command) {
    switch (command) {
        case GST_MTS_SCTE_SPLICE_COMMAND_NULL:      return "splice_null";
        case GST_MTS_SCTE_SPLICE_COMMAND_SCHEDULE:  return "splice_schedule";
        case GST_MTS_SCTE_SPLICE_COMMAND_INSERT:    return "splice_insert";
        case GST_MTS_SCTE_SPLICE_COMMAND_TIME:      return "time_signal";
        case GST_MTS_SCTE_SPLICE_COMMAND_BANDWIDTH: return "bandwidth_reservation";
        case GST_MTS_SCTE_SPLICE_COMMAND_PRIVATE:   return "private_command";
        default:
            return "unknown";
    }
}


GstMpegtsSection *transcoder::scheduler::sectionFromSit(GstMpegtsSCTESIT *sit) {
    GstMpegtsSection *ret = nullptr;

    auto *tmpSection = gst_mpegts_section_from_scte_sit(sit, 0);
    if (! tmpSection) {
        ERR(("Unable to build SCTE-35 section from SIT"));

        return nullptr;
    }

    {
        gsize dataSize = 0;

        gst_mpegts_section_packetize(tmpSection, &dataSize);

        auto *gbytes = gst_mpegts_section_get_data(tmpSection);
        if (gbytes) {
            auto *data = (guint8 *) g_bytes_unref_to_data(gbytes, &dataSize);

            ret = gst_mpegts_section_new(0, data, dataSize);
        }
    }

    gst_mpegts_section_unref(tmpSection);

    return ret;
}


GstMpegtsSection *transcoder::scheduler::sectionFromBase64(
    const std::vector<uint8_t>     &raw,
    GstClockTime                    spliceTimePts,
    GstMpegtsSCTESpliceCommandType  expectedCommand
) {
    if (raw.size() < SECTION_MIN_SIZE) {
        ERR(("Refusing to inject a {} byte SCTE-35 section; too short to be valid", raw.size()));

        return nullptr;
    }

    // section_length counts the bytes after itself, so the section is 3 bytes longer.
    //
    // The buffer has to be exactly that long. A shorter one is already refused by
    // gst_mpegts_section_new() below, but a longer one is not: the trailing bytes are
    // silently dropped when the section is truncated to its declared length, and - worse
    // - the CRC recomputed further down would be written into that discarded tail,
    // leaving the emitted section carrying a CRC that no longer matches its contents.
    // Reject the whole buffer instead of guessing which part of it was meant.
    //
    // The REST layer performs this same check in validate_scte35_base64(); this is the
    // engine's own guard, for sections arriving by any other route.
    const size_t declaredSize = (((size_t) (raw[1] & 0x0F) << 8) | raw[2]) + 3;

    if (declaredSize != raw.size()) {
        ERR(("Refusing a SCTE-35 section declaring {} bytes but supplied as {}",
            declaredSize, raw.size()
        ));

        return nullptr;
    }

    // Parse a throwaway copy purely to inspect it. The SIT belongs to that section and is
    // handed to us const; we never write through it, and never hand it to
    // gst_mpegts_section_from_scte_sit() - that call takes ownership of the SIT, which
    // would leave the section holding a freed pointer.
    auto *parsedData = (guint8 *) g_memdup2(raw.data(), raw.size());

    GstMpegtsSection *parsed = gst_mpegts_section_new(0, parsedData, raw.size());

    if (! parsed) {
        ERR(("Supplied SCTE-35 section ({} bytes) could not be parsed", raw.size()));

        // gst_mpegts_section_new() only takes ownership when it succeeds.
        g_free(parsedData);

        return nullptr;
    }

    const auto *sit = gst_mpegts_section_get_scte_sit(parsed);
    if (! sit) {
        ERR(("Supplied section ({} bytes) is not a splice_info_section", raw.size()));

        gst_mpegts_section_unref(parsed);

        return nullptr;
    }

    if (sit->splice_command_type != expectedCommand) {
        ERR(("Supplied SCTE-35 section carries a {} command but a {} was expected - skipping",
            _commandName((GstMpegtsSCTESpliceCommandType) sit->splice_command_type),
            _commandName(expectedCommand)
        ));

        gst_mpegts_section_unref(parsed);

        return nullptr;
    }

    // Find the splice time the operator authored. It is expressed in some other system's
    // PTS domain and means nothing here, so rather than rewriting it - which would mean
    // re-serialising the section and losing anything GStreamer does not model - we shift
    // the whole section with pts_adjustment, which every compliant receiver adds to each
    // splice time. The bytes the operator supplied then reach the wire untouched.
    bool     haveSpliceTime  = false;
    uint64_t authoredTime    = 0;

    switch (sit->splice_command_type) {
        case GST_MTS_SCTE_SPLICE_COMMAND_TIME:
            haveSpliceTime = sit->splice_time_specified;
            authoredTime   = sit->splice_time;
            break;

        case GST_MTS_SCTE_SPLICE_COMMAND_INSERT:
            if (! sit->splices || sit->splices->len == 0) {
                ERR(("Supplied splice_insert section carries no splice events - skipping"));

                gst_mpegts_section_unref(parsed);

                return nullptr;
            }

            {
                auto *event = (GstMpegtsSCTESpliceEvent *) g_ptr_array_index(sit->splices, 0);

                haveSpliceTime = event->program_splice_time_specified;
                authoredTime   = event->program_splice_time;
            }
            break;

        default:
            break;
    }

    gst_mpegts_section_unref(parsed);

    std::vector<uint8_t> patched(raw);

    if (haveSpliceTime) {
        // pts_adjustment is modulo 2^33, as is every splice time.
        uint64_t adjustment =
            (GST_UTILS_GSTTIME_TO_MPEGTIME(spliceTimePts) - authoredTime) & PTS_MASK;

        // encrypted_packet (bit 7) and encryption_algorithm (bits 6..1) share byte 4 with
        // the top bit of pts_adjustment, so only that one bit may be touched.
        patched[4] = (patched[4] & 0xFE) | ((adjustment >> 32) & 0x01);
        patched[5] = (adjustment >> 24) & 0xFF;
        patched[6] = (adjustment >> 16) & 0xFF;
        patched[7] = (adjustment >>  8) & 0xFF;
        patched[8] =  adjustment        & 0xFF;

        // The section is now different, so its CRC_32 has to be recomputed over everything
        // preceding it.
        {
            auto  crcOffset = patched.size() - CRC_SIZE;
            auto  crc       = ts::CRC32(patched.data(), crcOffset).value();

            patched[crcOffset + 0] = (crc >> 24) & 0xFF;
            patched[crcOffset + 1] = (crc >> 16) & 0xFF;
            patched[crcOffset + 2] = (crc >>  8) & 0xFF;
            patched[crcOffset + 3] =  crc        & 0xFF;
        }

    } else {
        // An unspecified splice time means "act immediately on receipt". There is nothing
        // to re-base: the section is muxed at the scheduled moment, which is the intent.
        DBG(("Supplied SCTE-35 section requests an immediate splice; passing it through unshifted"));
    }

    auto *patchedData = (guint8 *) g_memdup2(patched.data(), patched.size());

    auto *ret = gst_mpegts_section_new(0, patchedData, patched.size());

    if (! ret) {
        ERR(("Unable to build a section from the supplied SCTE-35 bytes"));

        g_free(patchedData);
    }

    return ret;
}


GstStructure *transcoder::scheduler::buildRunningTimeMap(GstMpegtsSection *section, GstClockTime runningTime) {
    pipeline::ScteRunningTimeMap map(static_cast<GstStructure *>(nullptr));

    if (! section) {
        return map.release();
    }

    const auto *sit = gst_mpegts_section_get_scte_sit(section);
    if (! sit) {
        return map.release();
    }

    switch (sit->splice_command_type) {
        case GST_MTS_SCTE_SPLICE_COMMAND_TIME:
            map.setTimeSignalTime(runningTime);
            break;

        case GST_MTS_SCTE_SPLICE_COMMAND_INSERT:
            if (sit->splices) {
                for (guint i = 0; i < sit->splices->len; i++) {
                    auto *event = (GstMpegtsSCTESpliceEvent *) g_ptr_array_index(sit->splices, i);

                    map.setSpliceEventTime(event->splice_event_id, runningTime);
                }
            }
            break;

        default:
            break;
    }

    return map.release();
}
