#include <tsduck/tsduck.h>
#include <tsduck/tsSpliceSegmentationDescriptor.h>

#include <gst/mpegts/gst-scte-section.h>

#include "utpl/common/utils/gstreamer.h"
#include "utpl/common/debug.h"

#include "scte.h"
#include "spliceInsert.h"

using namespace utpl;

transcoder::EventScheduler::ScheduledSpliceInsertEvent::ScheduledSpliceInsertEvent(
    EventProvider *provider, 
    Channel *channel, 
    const EventSpliceInsert &event,
    utpl::Timestamp prerollTimeMs
) : ScheduledEvent(provider, channel, Channel::TimedCallbackSource::OUTPUT, false, false), _event(event) {
    this->_prerollTime = prerollTimeMs;

    this->_prerollScteCallbackId = this->getChannel()->addTimedCallback(
        this->_event.getTimestamp() - this->_prerollTime, 0, Channel::TimedCallbackSource::OUTPUT, [this, prerollTimeMs, event](pipeline::TimedCallbackId id, GstClockTime time) {
            auto *channel = this->getChannel();
            {
                std::unique_lock lock(this->_guard);

                if (this->_prerollScteCallbackId) {
                    this->_prerollScteCallbackId = 0;

                } else {
                    return false;
                }
            }

            auto *processor = this->getChannel()->getVideoProcessor();
            if (! processor) {
                auto audioProcessors = this->getChannel()->getAudioProcessors();
                if (! audioProcessors.empty()) {
                    processor = audioProcessors[0];
                }
            }

            if (! processor) {
                return false;
            }

            processor->getConnector()->peekPad()->probeCurrentPts([prerollTimeMs, event, channel](GstClockTime pts, GstClockTime gst) {
                Scte35Data data;
                GstMpegtsSection *gstSection = nullptr;

                data.setRunningTime(gst);

                const auto spliceTimePts     = pts + prerollTimeMs * GST_MSECOND;
                const auto spliceRunningTime = gst + prerollTimeMs * GST_MSECOND;

                if (event.hasRawSection()) {
                    LOG(("Injecting operator-supplied splice_insert section ({} bytes) for event ID {}",
                        event.getRawSection().size(), event.getEventId()
                    ));

                    gstSection = scheduler::sectionFromBase64(
                        event.getRawSection(), spliceTimePts, GST_MTS_SCTE_SPLICE_COMMAND_INSERT
                    );

                } else {
                    auto *sit = gst_mpegts_scte_sit_new();

                    sit->splice_command_type = GST_MTS_SCTE_SPLICE_COMMAND_INSERT;

                    {
                        GstMpegtsSCTESpliceEvent *spliceEvent = gst_mpegts_scte_splice_event_new();

                        spliceEvent->splice_event_id          = event.getEventId();
                        spliceEvent->out_of_network_indicator = true;
                        spliceEvent->insert_event             = true;

                        spliceEvent->program_splice_time = GST_UTILS_GSTTIME_TO_MPEGTIME(spliceTimePts);

                        if (spliceEvent->program_splice_time) {
                            spliceEvent->program_splice_time_specified = true;
                            spliceEvent->program_splice_flag           = true;
                        }

                        spliceEvent->break_duration = GST_UTILS_GSTTIME_TO_MPEGTIME(event.getDuration() * GST_MSECOND);
                        if (spliceEvent->break_duration) {
                            // duration_flag stays set either way - the break length is
                            // still signalled. auto_return only decides whether the break
                            // closes itself or waits for a return-to-network.
                            spliceEvent->break_duration_auto_return = event.isAutoReturn();
                            spliceEvent->duration_flag              = true;
                        }

                        // This field provides an identification for a specific avail within one
                        // unique_program_id. This value is expected to increment with each new avail within a viewing event. This
                        // value is expected to reset to one for the first avail in a new viewing event. This field is expected to
                        // increment for each new avail. It may optionally carry a zero value to indicate its non-usage.
                        spliceEvent->avail_num = 0;

                        g_ptr_array_add(sit->splices, spliceEvent);
                    }

                    // SCTE-35 carries descriptors on the splice_info_section rather than on
                    // the command, so a splice_insert may carry a segmentation_descriptor
                    // just as a time_signal does. Emitted only when a UPID was supplied;
                    // without one the section keeps its previous shape exactly.
                    {
                        const auto &segmentation = event.getSegmentation();

                        if (! segmentation.isEmpty()) {
                            ts::DuckContext duckContext;
                            ts::SpliceSegmentationDescriptor descriptor;

                            descriptor.segmentation_event_id     = event.getEventId();
                            descriptor.segmentation_event_cancel = false;
                            descriptor.program_segmentation      = true;

                            // Mirrors the break the splice command signals, so the two
                            // cannot disagree about how long the avail runs.
                            if (event.getDuration()) {
                                descriptor.segmentation_duration =
                                    GST_UTILS_GSTTIME_TO_MPEGTIME(event.getDuration() * GST_MSECOND);
                            }

                            descriptor.web_delivery_allowed = true;
                            descriptor.no_regional_blackout = true;
                            descriptor.archive_allowed      = true;
                            descriptor.device_restrictions  = 3;  // none

                            descriptor.segmentation_upid_type = segmentation.upidType;
                            descriptor.segmentation_upid.copy(
                                segmentation.upid.data(), segmentation.upid.size()
                            );

                            descriptor.segmentation_type_id  = segmentation.segmentationTypeId;
                            descriptor.segment_num           = 0;
                            descriptor.segments_expected     = 0;
                            descriptor.sub_segment_num       = 0;
                            descriptor.sub_segments_expected = 0;

                            {
                                ts::Descriptor baseDescriptor;

                                descriptor.serialize(duckContext, baseDescriptor);

                                // Copied because gst_mpegts_parse_descriptors() takes a
                                // mutable pointer, as in the time_signal path.
                                std::vector<uint8_t> rawDescriptors(
                                    baseDescriptor.content(),
                                    baseDescriptor.content() + baseDescriptor.size()
                                );

                                sit->descriptors = gst_mpegts_parse_descriptors(
                                    rawDescriptors.data(), rawDescriptors.size()
                                );
                            }
                        }
                    }

                    gstSection = scheduler::sectionFromSit(sit);
                }

                if (gstSection) {
                    data.setSection(gstSection);
                    data.setTimingMap(scheduler::buildRunningTimeMap(gstSection, spliceRunningTime));

                    channel->injectScte35Event(data);

                    gst_mpegts_section_unref(gstSection);

                    // Injected prerollTimeMs ahead of the splice point; at= is the splice point.
                    const auto spliceAt = utpl::Time::getUTCTimestamp() + prerollTimeMs;

                    logFired(event, spliceAt, spliceRunningTime, fmt::format(
                        "splice_event_id={} out_of_network=yes duration_ms={} auto_return={} preroll_ms={} section={}",
                        event.getEventId(), event.getDuration(), event.isAutoReturn() ? "yes" : "no", prerollTimeMs,
                        event.hasRawSection() ? "operator" : "built"
                    ));

                    // Only an auto-returning break ends by itself, at splice + duration. Without
                    // auto_return the duration is advisory and the break ends on a return to
                    // network, which logs its own FIRED line. The scheduler's own stop timer is
                    // not used: it sees this event through the base class, whose duration is 0.
                    if (event.isAutoReturn() && event.getDuration() > 0) {
                        const auto endAt          = spliceAt + event.getDuration();
                        const auto endRunningTime = spliceRunningTime + event.getDuration() * GST_MSECOND;

                        channel->addTimedCallback(endAt, 0, Channel::TimedCallbackSource::OUTPUT, [event, endAt, endRunningTime](pipeline::TimedCallbackId, GstClockTime) {
                            logEnded(event, endAt, endRunningTime, fmt::format(
                                "splice_event_id={} reason=break_duration_elapsed", event.getEventId()
                            ));

                            return false;
                        });
                    }
                }
            });

            return false;
        }
    );
}

transcoder::EventScheduler::ScheduledSpliceInsertEvent::~ScheduledSpliceInsertEvent() {
    if (this->_prerollScteCallbackId) {
        this->getChannel()->cancelTimedCallback(this->_prerollScteCallbackId);

        this->_prerollScteCallbackId = 0;
    }
}

transcoder::Event *transcoder::EventScheduler::ScheduledSpliceInsertEvent::getEvent() {
    return &this->_event;
}

void transcoder::EventScheduler::ScheduledSpliceInsertEvent::handleStart() {
    DBG(("CALL"));
}

void transcoder::EventScheduler::ScheduledSpliceInsertEvent::handleStop() {
    DBG(("CALL"));
}

void transcoder::EventScheduler::ScheduledSpliceInsertEvent::update(Event *event) {
    
}

bool transcoder::EventScheduler::ScheduledSpliceInsertEvent::cancel() {
    {
        std::unique_lock lock(this->_guard);

        if (this->_prerollScteCallbackId) {
            this->getChannel()->cancelTimedCallback(this->_prerollScteCallbackId);

            this->_prerollScteCallbackId = 0;
        }
    }

    return ScheduledEvent::cancel();
}
