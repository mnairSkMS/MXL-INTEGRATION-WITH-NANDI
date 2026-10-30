#include "utpl/common/debug.h"
#include "utpl/common/utils/m3u8.h"

#include "utpl/hls/playlist.h"

using namespace utpl;

#define VERSION 5

// Common
#define TAG_EXTM3U        "EXTM3U"
#define TAG_EXT_X_VERSION "EXT-X-VERSION"

// Master playlist
#define TAG_EXT_X_STREAM_INF       "EXT-X-STREAM-INF"
#define EXT_X_STREAM_INF_BANDWIDTH  "BANDWIDTH"
#define EXT_X_STREAM_INF_RESOLUTION "RESOLUTION"
#define EXT_X_STREAM_INF_FRAME_RATE "FRAME-RATE"

#define TAG_EXT_X_MEDIA      "EXT-X-MEDIA"

// Media playlist
#define TAG_EXTINF               "EXTINF"
#define TAG_EXT_X_PROGRAM_DATE_TIME "EXT-X-PROGRAM-DATE-TIME"
#define TAG_EXT_X_TARGETDURATION "EXT-X-TARGETDURATION"
#define TAG_EXT_X_MEDIA_SEQUENCE "EXT-X-MEDIA-SEQUENCE"
#define TAG_EXT_X_ENDLIST        "EXT-X-ENDLIST"

// SCTE markers
// Elemental
#define TAG_EXT_X_CUE_OUT        "EXT-X-CUE-OUT"
#define TAG_EXT_X_CUE_OUT_CONT   "EXT-X-CUE-OUT-CONT"
#define TAG_EXT_X_CUE_IN         "EXT-X-CUE-IN"

// Enhanced (OATCLS): carries the original section as base64 alongside the cue tags, so a
// player or packager downstream can read the signalling the transcoder acted on.
#define TAG_EXT_OATCLS_SCTE35        "EXT-OATCLS-SCTE35"
#define EXT_X_CUE_OUT_CONT_ELAPSED   "ElapsedTime"
#define EXT_X_CUE_OUT_CONT_DURATION  "Duration"
#define EXT_X_CUE_OUT_CONT_SCTE35    "SCTE35"


namespace {
    /**
     * True when `at` falls inside the segment immediately before this one, making this
     * segment the first that begins at or after it.
     */
    bool _endsInThePrecedingSegment(GstClockTime at, const std::shared_ptr<utpl::hls::Segment> &segment) {
        auto begin  = segment->getBeginRunningTime();
        auto window = static_cast<GstClockTime>(segment->getDuration()) * GST_MSECOND;

        return at <= begin && at > (begin > window ? begin - window : 0);
    }
}

#define TAG_EXT_X_PLAYLIST_TYPE  "EXT-X-PLAYLIST-TYPE"
#define X_PLAYLIST_TYPE_VOD      "VOD"
#define X_PLAYLIST_TYPE_EVENT    "EVENT"

#define TAG_EXT_X_MAP            "EXT-X-MAP"
#define EXT_X_MAP_ATTR_URI       "URI"


static bool _hasMarker(const std::vector<hls::ScteMarker> &markers, hls::ScteMarker marker) {
    return std::find(markers.begin(), markers.end(), marker) != markers.end();
}

/**
 * Emits the base64 of the section that opened this avail. Unquoted: the base64 alphabet has
 * no comma, so it is unambiguous inside an attribute list, and unquoted is what the OATCLS
 * convention puts on the wire. AttributeValueString would wrap it in quotes.
 */
static void _addOatclsTag(utils::M3U8 &m3u, const hls::ScteEvent &event) {
    if (event.getBase64().empty()) {
        return;
    }

    m3u.addTagEntry(TAG_EXT_OATCLS_SCTE35)->
        addAttribute(std::make_shared<utils::M3U8::AttributeValueEnum>(event.getBase64()));
}


static void _commonSerialize(utils::M3U8 &m3u) {
    m3u.addTagEntry(TAG_EXTM3U);
    m3u.addTagEntry(TAG_EXT_X_VERSION)
        ->addAttribute(std::make_shared<utils::M3U8::AttributeValueInteger>(VERSION));
}


std::string hls::MasterPlaylist::serialize(const std::string &basePath) {
    utils::M3U8 m3u;

    _commonSerialize(m3u);

    for (auto &variant : this->_variants) {
        // FIXME: bandwidth form playlist or as serialize() parameter
        auto streamInf = m3u.addTagEntry(TAG_EXT_X_STREAM_INF);

        streamInf->addAttribute(EXT_X_STREAM_INF_BANDWIDTH, std::make_shared<utils::M3U8::AttributeValueInteger>(variant.getBandwidth()));

        // Players label and pick renditions by these; without RESOLUTION they show 0x0.
        if (variant.getWidth() > 0 && variant.getHeight() > 0) {
            streamInf->addAttribute(
                EXT_X_STREAM_INF_RESOLUTION,
                std::make_shared<utils::M3U8::AttributeValueEnum>(std::to_string(variant.getWidth()) + "x" + std::to_string(variant.getHeight()))
            );
        }

        if (variant.getFrameRate() > 0.0) {
            streamInf->addAttribute(EXT_X_STREAM_INF_FRAME_RATE, std::make_shared<utils::M3U8::AttributeValueFloat>(variant.getFrameRate()));
        }

        m3u.addUriEntry(basePath + variant.getPlaylist());
    }

    return m3u.serialize();
}

// h:mm:ss.mmm, matching the scheduler's FIRED/ENDED lines.
static std::string _formatRunningTime(GstClockTime time) {
    if (! GST_CLOCK_TIME_IS_VALID(time)) {
        return "-";
    }

    const auto ms = GST_TIME_AS_MSECONDS(time);

    return fmt::format("{}:{:02}:{:02}.{:03}", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
}

// The splice_event_id of a splice_insert, or the segmentation_event_id of a time_signal's first
// segmentation descriptor - whichever identifies the avail - or -1 when neither is present.
static int64_t _cueEventId(const hls::ScteEvent &event) {
    auto *sit = gst_mpegts_section_get_scte_sit((GstMpegtsSection *) event.getSection());
    if (! sit) {
        return -1;
    }

    if (sit->splice_command_type == GST_MTS_SCTE_SPLICE_COMMAND_INSERT && sit->splices && sit->splices->len > 0) {
        return ((GstMpegtsSCTESpliceEvent *) g_ptr_array_index(sit->splices, 0))->splice_event_id;
    }

    if (sit->splice_command_type == GST_MTS_SCTE_SPLICE_COMMAND_TIME && sit->descriptors) {
        for (guint i = 0; i < sit->descriptors->len; i++) {
            auto *gstDescriptor = reinterpret_cast<GstMpegtsDescriptor *>(g_ptr_array_index(sit->descriptors, i));

            if (gstDescriptor->tag == ts::DID_SPLICE_SEGMENT) {
                ts::DuckContext duckCtx;
                ts::Descriptor  genericDescriptor(gstDescriptor->data, gstDescriptor->length + 2);

                if (genericDescriptor.isValid()) {
                    ts::SpliceSegmentationDescriptor descriptor(duckCtx, genericDescriptor);

                    if (descriptor.isValid()) {
                        return descriptor.segmentation_event_id;
                    }
                }
            }
        }
    }

    return -1;
}

std::string hls::MediaPlaylist::serialize(
    const std::string &basePath, 
    bool complete, 
    const std::shared_ptr<Segment> &initialSegment, 
    const std::vector<std::shared_ptr<Segment>> &segments,
    const std::vector<ScteEvent> &scteEvents,
    const std::vector<ScteMarker> &scteMarkers
) {
    utils::M3U8 m3u;

    const bool enhanced = _hasMarker(scteMarkers, ScteMarker::ENHANCED);

    _commonSerialize(m3u);
    
    m3u.addTagEntry(TAG_EXT_X_TARGETDURATION)->
        addAttribute(std::make_shared<utils::M3U8::AttributeValueInteger>(this->getSegmentDuration()));

    {
        std::string playlistType;

        if (! this->_live) {
            if (complete) {
                playlistType = X_PLAYLIST_TYPE_VOD;

            } else {
                playlistType = X_PLAYLIST_TYPE_EVENT;
            }
        }

        if (! playlistType.empty()) {
            m3u.addTagEntry(TAG_EXT_X_PLAYLIST_TYPE)->
                addAttribute(std::make_shared<utils::M3U8::AttributeValueEnum>(playlistType));
        }
    }

    {
        int seq = -1;

        if (! this->_live) {
            seq = 0;

        } else {
            if (! segments.empty()) {
                seq = segments[0]->getSequence();
            }
        }

        if (seq >= 0) {
            m3u.addTagEntry(TAG_EXT_X_MEDIA_SEQUENCE)->
                addAttribute(std::make_shared<utils::M3U8::AttributeValueInteger>(seq));
        }
    }
    
    if (initialSegment) {
        m3u.addTagEntry(TAG_EXT_X_MAP)->
            addAttribute(EXT_X_MAP_ATTR_URI, std::make_shared<utils::M3U8::AttributeValueString>(basePath + initialSegment->getName()));
    }

    // Forget cues on segments that have left the window, so the set stays bounded.
    if (! segments.empty()) {
        const int firstSequence = segments.front()->getSequence();

        for (auto it = this->_loggedCues.begin(); it != this->_loggedCues.end(); ) {
            it = std::get<0>(*it) < firstSequence ? this->_loggedCues.erase(it) : std::next(it);
        }
    }

    // One normal-level line the first time a CUE-OUT or CUE-IN lands on a segment. The segment's
    // program date time is the on-air wall clock of the cue for a player of this playlist.
    auto logCue = [this](const char *tag, const ScteEvent &event, const std::shared_ptr<Segment> &segment) {
        if (! this->_loggedCues.emplace(segment->getSequence(), tag, event.getStart()).second) {
            return;
        }

        auto *sit = gst_mpegts_section_get_scte_sit((GstMpegtsSection *) event.getSection());

        LOG(("HLS {} placed playlist={} segment={} uri={} program_date_time={} splice_running_time={} segment_running_time={} command={} event_id={} duration_ms={} auto_return={}",
            tag, this->_name, segment->getSequence(), segment->getName(), segment->getProgramDateTime(),
            _formatRunningTime(event.getStart()), _formatRunningTime(segment->getBeginRunningTime()),
            ! sit ? "unknown" : (sit->splice_command_type == GST_MTS_SCTE_SPLICE_COMMAND_INSERT ? "splice_insert" : "time_signal"),
            _cueEventId(event),
            event.getDuration() == GST_CLOCK_TIME_NONE ? std::string("-") : std::to_string(GST_TIME_AS_MSECONDS(event.getDuration())),
            event.isAutoReturn() ? "yes" : "no"
        ));
    };

    for (auto &segment : segments) {
        m3u.addTagEntry(TAG_EXT_X_PROGRAM_DATE_TIME)->
            addAttribute(std::make_shared<utils::M3U8::AttributeValueDateTime>(segment->getProgramDateTime())
        );

        // Cue tags belong between the program date time and EXTINF: EXTINF must be the last
        // tag before the segment URI it describes. Emitting them after EXTINF leaves tags
        // sitting between EXTINF and its URI, which players are not required to tolerate -
        // and the enhanced flavour puts two tags there where elemental puts one.
        for (auto &event : scteEvents) {
            auto eventStart  = event.getStart();

            // Belt and braces: Streamer::putScteData() already refuses these, but an
            // unplaceable start would otherwise make every comparison below fail silently
            // and overflow the eventEnd sum.
            if (eventStart == GST_CLOCK_TIME_NONE) {
                continue;
            }

            auto eventMargin = (segment->getDuration() * GST_MSECOND) / 4;

            GstClockTime diff;

            bool inInserted = false;

            diff = eventStart > segment->getBeginRunningTime() ? eventStart - segment->getBeginRunningTime() : segment->getBeginRunningTime() - eventStart;
            if (diff < eventMargin) {
                if (event.isOut()) {
                    if (enhanced) {
                        _addOatclsTag(m3u, event);
                    }

                    auto tag = m3u.addTagEntry(TAG_EXT_X_CUE_OUT);

                    logCue(TAG_EXT_X_CUE_OUT, event, segment);

                    // An auto_return = 0 break carries an advisory duration but ends only on
                    // an explicit return to network, so it is published bare rather than with
                    // an end time nothing guarantees.
                    if (event.getDuration() != GST_CLOCK_TIME_NONE && event.isAutoReturn()) {
                        auto duration = GST_TIME_AS_MSECONDS(event.getDuration()) / 1000.0;

                        tag->addAttribute(std::make_shared<utils::M3U8::AttributeValueFloat>(duration));
                    }

                } else if (event.isIn()) {
                    m3u.addTagEntry(TAG_EXT_X_CUE_IN);
                    inInserted = true;

                    logCue(TAG_EXT_X_CUE_IN, event, segment);
                }

            } else if (event.isIn() && _endsInThePrecedingSegment(eventStart, segment)) {
                // A return to network that landed inside the previous segment rather than
                // near a boundary. The branch above only recognises one close to a segment
                // edge, which an auto-returning break always is - its end is derived from a
                // start that was itself aligned - but an operator-timed cue-in is not, so the
                // avail simply stopped being signalled with no CUE-IN ever closing it.
                //
                // The tag goes on the first segment that begins at or after the return, which
                // is the same segment the continuation stops on, so no segment is marked as
                // both continuing and ending.
                m3u.addTagEntry(TAG_EXT_X_CUE_IN);
                inInserted = true;

                logCue(TAG_EXT_X_CUE_IN, event, segment);

            } else if (event.getDuration() != GST_CLOCK_TIME_NONE) {
                // Whether this break ends by itself. When it does not, its duration says
                // nothing about when it stops: only the matching return to network does,
                // and that is what the search below already looks for.
                const bool openEnded = ! event.isAutoReturn();
                auto eventEnd = eventStart + event.getDuration();

                diff = eventEnd > segment->getBeginRunningTime() ? eventEnd - segment->getBeginRunningTime() : segment->getBeginRunningTime() - eventEnd;
                if (! openEnded && diff < eventMargin) {
                    if (event.isOut()) {
                        m3u.addTagEntry(TAG_EXT_X_CUE_IN);
                        inInserted = true;

                        // The auto-return end of this break, derived from its CUE-OUT.
                        logCue(TAG_EXT_X_CUE_IN, event, segment);
                    }

                } else if (
                    eventStart <= segment->getBeginRunningTime() &&
                    (openEnded || eventEnd >= segment->getBeginRunningTime())
                ) {
                    bool attachContTag = true;

                    if (event.isOut()) {
                        for (auto &e : scteEvents) {
                            if (e.isIn() && e.getStart() > event.getStart()) {
                                if (
                                    openEnded ||
                                    (event.getDuration() == GST_CLOCK_TIME_NONE) ||
                                    (event.getStart() + event.getDuration() > e.getStart())
                                ) {
                                    auto *outSit = gst_mpegts_section_get_scte_sit((GstMpegtsSection *) event.getSection());
                                    auto *inSit  = gst_mpegts_section_get_scte_sit((GstMpegtsSection *) e.getSection());

                                    if (
                                        outSit->splice_command_type == inSit->splice_command_type
                                    ) {
                                        switch (outSit->splice_command_type) {
                                            case GST_MTS_SCTE_SPLICE_COMMAND_INSERT:
                                                {
                                                    GstMpegtsSCTESpliceEvent *inSplice  = (GstMpegtsSCTESpliceEvent *) g_ptr_array_index(inSit->splices, 0);
                                                    GstMpegtsSCTESpliceEvent *outSplice = (GstMpegtsSCTESpliceEvent *) g_ptr_array_index(outSit->splices, 0);

                                                    if (
                                                        (inSplice->splice_event_id == outSplice->splice_event_id) &&
                                                        (e.getStart() <= segment->getBeginRunningTime())
                                                    ) {
                                                        attachContTag = false;
                                                    }
                                                }
                                            break;

                                            case GST_MTS_SCTE_SPLICE_COMMAND_TIME:
                                                if (inSit->descriptors->len > 0 && inSit->descriptors->len == outSit->descriptors->len) {
                                                    ts::DuckContext duckCtx;

                                                    {
                                                        auto *inDescriptorData  = reinterpret_cast<GstMpegtsDescriptor *>(g_ptr_array_index(inSit->descriptors, 0));
                                                        auto *outDescriptorData = reinterpret_cast<GstMpegtsDescriptor *>(g_ptr_array_index(outSit->descriptors, 0));

                                                        if (
                                                            (inDescriptorData->tag  == ts::DID_SPLICE_SEGMENT) &&
                                                            (outDescriptorData->tag == ts::DID_SPLICE_SEGMENT) 
                                                        ) {
                                                            ts::Descriptor inDescriptor(inDescriptorData->data, inDescriptorData->length + 2);
                                                            ts::Descriptor outDescriptor(outDescriptorData->data, outDescriptorData->length + 2);

                                                            if (inDescriptor.isValid() && outDescriptor.isValid()) {
                                                                ts::SpliceSegmentationDescriptor inSegmentationDescriptor(duckCtx, inDescriptor);
                                                                ts::SpliceSegmentationDescriptor outSegmentationDescriptor(duckCtx, outDescriptor);

                                                                if (
                                                                    (inSegmentationDescriptor.segmentation_event_id == outSegmentationDescriptor.segmentation_event_id) &&
                                                                    (e.getStart() <= segment->getBeginRunningTime())
                                                                ) {
                                                                    attachContTag = false;
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                                break;
                                        }
                                    }
                                }
                            }

                            if (! attachContTag) {
                                break;
                            }
                        }
                    }

                    if (attachContTag && ! inInserted) {
                        auto elapsedSec  = GST_TIME_AS_MSECONDS(segment->getBeginRunningTime() - eventStart) / 1000.0;
                        auto durationSec = GST_TIME_AS_MSECONDS(event.getDuration()) / 1000.0;

                        if (enhanced) {
                            auto tag = m3u.addTagEntry(TAG_EXT_X_CUE_OUT_CONT);

                            tag->addAttribute(EXT_X_CUE_OUT_CONT_ELAPSED,
                                std::make_shared<utils::M3U8::AttributeValueFloat>(elapsedSec));

                            // Duration is omitted for an open-ended break: the enhanced flavour
                            // allows it, and stating one would promise an end the break has not
                            // signalled. ElapsedTime and the section still tell a player joining
                            // mid-break that it is inside an avail, which is the whole point of
                            // the continuation tag.
                            if (! openEnded) {
                                tag->addAttribute(EXT_X_CUE_OUT_CONT_DURATION,
                                    std::make_shared<utils::M3U8::AttributeValueFloat>(durationSec));
                            }

                            if (! event.getBase64().empty()) {
                                tag->addAttribute(EXT_X_CUE_OUT_CONT_SCTE35,
                                    std::make_shared<utils::M3U8::AttributeValueEnum>(event.getBase64()));
                            }

                        } else if (! openEnded) {
                            // The elemental flavour is "<elapsed>/<duration>" with no way to
                            // leave the duration out, so an open-ended break gets no
                            // continuation there rather than a fabricated one.
                            m3u.addTagEntry(TAG_EXT_X_CUE_OUT_CONT)->
                                addAttribute(std::make_shared<utils::M3U8::AttributeValueRational>(
                                    elapsedSec, durationSec
                                )
                            );
                        }
                    }
                }
            }
        }

        {
            auto extinf = m3u.addTagEntry(TAG_EXTINF);

            extinf->addAttribute(
                std::make_shared<utils::M3U8::AttributeValueFloat>(segment->getDuration() / 1000.0)
            );

            // EXTINF is "#EXTINF:<duration>,<title>" - the comma is mandatory even when the
            // title is empty. The writer joins attributes with ",", so an empty trailing
            // attribute produces exactly that, without changing how any other tag renders.
            extinf->addAttribute(std::make_shared<utils::M3U8::AttributeValueEnum>(std::string()));
        }

        m3u.addUriEntry(basePath + segment->getName());
    }

    if (! this->_live && complete) {
        m3u.addTagEntry(TAG_EXT_X_ENDLIST);
    }

    return m3u.serialize();
}

std::string hls::MediaPlaylist::serialize(const std::string &basePath) {
    return this->serialize(basePath, false, nullptr, {}, {});
}
