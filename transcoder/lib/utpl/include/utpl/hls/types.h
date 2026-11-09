#ifndef __UTPL_HLS_TYPES_H__
#define __UTPL_HLS_TYPES_H__

#include <tsduck/tsduck.h>
#include <tsduck/tsSpliceSegmentationDescriptor.h>

#include "utpl/transcoder/types.h"
#include "utpl/common/utils.h"
#include "utpl/common/utils/gstreamer.h"

namespace utpl {
    namespace hls {
        enum class ScteMarker {
            /**
             * Inserts a CUE: DURATION for each ad avail. Does not insert any CUE-OUT CONT (continuation tags) to indicate to a client player joining 
             * midbreak that there is a current avail. This does not insert a CUE-IN tag at the end of the avail.
             * 
             * Tag contents
             *  * CUE:DURATION contains the following:
             *      * duration - Duration in fractional seconds
             *      * id - An identifier, unique among all ad avails CUE tags
             *      * type – SpliceOut
             *      * time – The PTS time for the ad avail, in fractional seconds
             */
            
            ADOBE,
            /**
             * Tag contents
             *  * CUE-OUT contains DURATION
             *  * CUE-OUT-CONT contains Elapsed time and Duration
             *  * CUE-IN has no content
             */
            ELEMENTAL,

            /**
             * Tag contents
             *  * OATCLS-SCTE35 containing the base64 encoded raw bytes of the original SCTE 35 ad avail message.
             *  * ASSET containing the CAID or UPID as specified in the original SCTE35 message.
             *  * 1 CUE-OUT per ad avail.
             *  * CUE-OUT-CONT containing the following:
             *      * The elapsed time of the avail.
             *      * The duration declared in the original SCTE35 message.
             *      * SCTE35 containing the base64 encoded raw bytes of the original SCTE 35 ad avail message.
             *        These lines repeat until the ad avail ends.
             *      * CUE-IN to indicate the end of the avail.
             *
             * An avail whose splice_insert sets break_duration_auto_return = 0 has no
             * guaranteed end: it closes on an explicit return to network. Such a break is
             * signalled with a bare CUE-OUT and continuation tags carrying ElapsedTime and
             * SCTE35 but no Duration, rather than publishing an end time nothing promises.
             * ELEMENTAL has no way to express that, so it emits no continuation there.
             */
            ENHANCED
        };

        class ScteEvent {
            public:
                ScteEvent& operator=(const ScteEvent&) = delete;

                ScteEvent& operator=(ScteEvent &&other) {
                    this->section    = other.section;
                    this->start      = other.start;
                    this->duration   = other.duration;
                    this->autoReturn = other.autoReturn;
                    this->in         = other.in;
                    this->out        = other.out;
                    this->base64     = std::move(other.base64);

                    other.section = nullptr;

                    return *this;
                }

                ScteEvent(ScteEvent &&other) {
                    *this = std::move(other);
                }

                ScteEvent(const ScteEvent &other) {
                    this->section    = gst_mpegts_section_ref(other.section);
                    this->start      = other.start;
                    this->duration   = other.duration;
                    this->autoReturn = other.autoReturn;
                    this->in         = other.in;
                    this->out        = other.out;
                    this->base64     = other.base64;
                }

                ScteEvent(const transcoder::Scte35Data &data) {
                    this->section    = gst_mpegts_section_ref(data.getSection());
                    this->start      = data.getSpliceTime();
                    this->duration   = GST_CLOCK_TIME_NONE;
                    this->autoReturn = true;

                    // Neither is set by a time_signal that carries no start or end segmentation
                    // type, and a time_signal start only ever sets `out`. Left uninitialised,
                    // `in` held whatever the memory held, so a break start sometimes also
                    // counted as a break end.
                    this->in         = false;
                    this->out        = false;

                    // Encode once here rather than per playlist refresh: serialize() walks
                    // every live event for every segment, on every refresh.
                    if (this->section) {
                        gsize dataSize = 0;

                        auto *raw = gst_mpegts_section_packetize(this->section, &dataSize);
                        if (raw) {
                            this->base64 = utils::toBase64(raw, dataSize);
                        }
                    }

                    if (this->section) {
                        auto *sit = gst_mpegts_section_get_scte_sit(this->section);
                        if (sit) {
                            switch (sit->splice_command_type) {
                                case GstMpegtsSCTESpliceCommandType::GST_MTS_SCTE_SPLICE_COMMAND_INSERT:
                                    {
                                        GstClockTime pts = GST_CLOCK_TIME_NONE;

                                        if (sit->splices->len > 0) {
                                            GstMpegtsSCTESpliceEvent *event = (GstMpegtsSCTESpliceEvent *)g_ptr_array_index(sit->splices, 0);

                                            this->out = event->out_of_network_indicator;
                                            this->in  = ! this->out;

                                            // Take the duration whatever auto_return says, and
                                            // remember the flag separately. With auto_return = 0
                                            // the break ends on an explicit return to network, so
                                            // the duration must not be published as an end time -
                                            // but it is still needed to know the break is running,
                                            // which is what keeps a continuation tag on the
                                            // playlist for a player joining mid-break.
                                            if (event->duration_flag) {
                                                this->duration   = GST_UTILS_MPEGTIME_TO_GSTTIME(event->break_duration);
                                                this->autoReturn = event->break_duration_auto_return;
                                            }
                                        }
                                    }                       
                                    break;

                                case GstMpegtsSCTESpliceCommandType::GST_MTS_SCTE_SPLICE_COMMAND_TIME:
                                    {
                                        if (sit->descriptors) {
                                            ts::DuckContext duckCtx;

                                            for (int i = 0; i < sit->descriptors->len; i++) {
                                                auto *gstDescriptor = reinterpret_cast<GstMpegtsDescriptor *>(g_ptr_array_index(sit->descriptors, i));

                                                if (gstDescriptor->tag == ts::DID_SPLICE_SEGMENT) {
                                                    ts::Descriptor genericDescriptor(gstDescriptor->data, gstDescriptor->length + 2);

                                                    if (genericDescriptor.isValid()) {
                                                        ts::SpliceSegmentationDescriptor segmentationDescriptor(duckCtx, genericDescriptor);
                                                        
                                                        if (segmentationDescriptor.isValid()) {
                                                            bool newIn  = false;
                                                            bool newOut = false;

                                                            switch (segmentationDescriptor.segmentation_type_id) {
                                                                case 0x22: // Break start
                                                                case 0x30: // Provider Advertisement Start
                                                                case 0x32: // Distributor Advertisement Start
                                                                case 0x34: // Provider Placement Opportunity Start
                                                                case 0x36: // Distributor Placement Opportunity Start
                                                                case 0x38: // Provider Overlay Placement Opportunity Start
                                                                case 0x3A: // Distributor Overlay Placement Opportunity Start
                                                                    newOut = true;
                                                                    break;

                                                                case 0x23: // Break end
                                                                case 0x31: // Provider Advertisement end
                                                                case 0x33: // Distributor Advertisement end
                                                                case 0x35: // Provider Placement Opportunity end
                                                                case 0x37: // Distributor Placement Opportunity end
                                                                case 0x39: // Provider Overlay Placement Opportunity end
                                                                case 0x3B: // Distributor Overlay Placement Opportunity end
                                                                    newIn = true;
                                                                    break;

                                                                default:
                                                                    break;
                                                            }

                                                            if (newOut) {
                                                                if (segmentationDescriptor.segmentation_duration) {
                                                                    this->duration = GST_UTILS_MPEGTIME_TO_GSTTIME(segmentationDescriptor.segmentation_duration.value());
                                                                }

                                                                this->out = true;
                                                            }

                                                            if (newIn) {
                                                                this->in = true;
                                                            }
                                                        }
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

                virtual ~ScteEvent() {
                    if (this->section) {
                        gst_mpegts_section_unref(this->section);

                        this->section = nullptr;
                    }
                }

                const GstMpegtsSection *getSection() const {
                    return this->section;
                }

                /**
                 * The section as base64, for the OATCLS-SCTE35 family of playlist tags.
                 */
                const std::string &getBase64() const {
                    return this->base64;
                }

                GstClockTime getStart() const {
                    return this->start;
                }

                GstClockTime getDuration() const {
                    return this->duration;
                }

                /**
                 * break_duration_auto_return from the splice_insert.
                 *
                 * False means the break has no guaranteed end: the duration it carries is
                 * advisory and the avail closes only when a return to network arrives. Such
                 * a break must not publish an end time, and must not be closed implicitly
                 * when that advisory duration elapses.
                 */
                bool isAutoReturn() const {
                    return this->autoReturn;
                }

                // Ad break ends
                bool isIn() const {
                    return this->in;
                }

                // Ad break starts
                bool isOut() const {
                    return this->out;
                }

            private:
                GstMpegtsSection *section;
                GstClockTime      start;
                GstClockTime      duration;
                bool              autoReturn;
                bool              in;
                bool              out;
                std::string       base64;
        };
    }
}

#endif /* __UTPL_HLS_TYPES_H__ */
