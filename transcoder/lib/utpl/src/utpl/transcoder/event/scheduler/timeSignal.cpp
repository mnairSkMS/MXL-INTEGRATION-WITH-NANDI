#include <tsduck/tsduck.h>
#include <tsduck/tsSpliceSegmentationDescriptor.h>

#include <gst/mpegts/gst-scte-section.h>

#include "utpl/common/debug.h"

#include "scte.h"
#include "timeSignal.h"

using namespace utpl;


transcoder::EventScheduler::ScheduledTimeSignalEvent::ScheduledTimeSignalEvent(
    EventProvider *provider, 
    Channel *channel, 
    const EventTimeSignal &event,
    utpl::Timestamp prerollTimeMs
) : ScheduledEvent(provider, channel, Channel::TimedCallbackSource::OUTPUT, false, false), _event(event) {
    this->_prerollTime = prerollTimeMs;

    this->_prerollScteCallbackId = this->getChannel()->addTimedCallback(
        this->_event.getTimestamp() - this->_prerollTime, 0, Channel::TimedCallbackSource::OUTPUT, [this](pipeline::TimedCallbackId id, GstClockTime time) {
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
                ERR(("Neither the Audio nor Video processor is available—since it's not possible to retrieve the current PTS, the event will be skipped."));

            } else {
                processor->getConnector()->peekPad()->probeCurrentPts([&](GstClockTime pts, GstClockTime gst) {
                    Scte35Data data;                

                    data.setRunningTime(gst);

                    const auto spliceTimePts     = pts + this->_prerollTime * GST_MSECOND;
                    const auto spliceRunningTime = gst + this->_prerollTime * GST_MSECOND;

                    {
                        GstMpegtsSection *gstSection = nullptr;

                        if (this->_event.hasRawSection()) {
                            LOG(("Injecting operator-supplied time_signal section ({} bytes)",
                                this->_event.getRawSection().size()
                            ));

                            gstSection = scheduler::sectionFromBase64(
                                this->_event.getRawSection(), spliceTimePts, GST_MTS_SCTE_SPLICE_COMMAND_TIME
                            );

                        } else {
                            auto *sit = gst_mpegts_scte_sit_new();
                            
                            sit->splice_command_type   = GST_MTS_SCTE_SPLICE_COMMAND_TIME;
                            sit->splice_time_specified = true;
                            sit->splice_time           = GST_UTILS_GSTTIME_TO_MPEGTIME(spliceTimePts);

                            {
                                std::vector<uint8_t> rawDescriptors;

                                ts::DuckContext duckContext;

                                for (const auto &segmentationDescriptor : this->_event.getSegmentationDescriptors()) {
                                    ts::SpliceSegmentationDescriptor descriptor;

                                    descriptor.segmentation_event_id     = segmentationDescriptor.eventId;
                                    descriptor.segmentation_event_cancel = segmentationDescriptor.isEventCancelled;

                                    if (segmentationDescriptor.hasSegmentationDuration) {
                                        descriptor.segmentation_duration = segmentationDescriptor.segmentationDuration;
                                    }

                                    descriptor.web_delivery_allowed = segmentationDescriptor.webDeliveryAllowed;
                                    descriptor.no_regional_blackout = segmentationDescriptor.noRegionalBlackout;
                                    descriptor.archive_allowed      = segmentationDescriptor.archiveAllowed;
                                    
                                    switch (segmentationDescriptor.deviceRestriction) {
                                        case transcoder::EventTimeSignal::DeviceRestriction::GROUP_0: descriptor.device_restrictions = 0; break;
                                        case transcoder::EventTimeSignal::DeviceRestriction::GROUP_1: descriptor.device_restrictions = 1; break;
                                        case transcoder::EventTimeSignal::DeviceRestriction::GROUP_2: descriptor.device_restrictions = 2; break;
                                        case transcoder::EventTimeSignal::DeviceRestriction::NONE:    descriptor.device_restrictions = 3; break;
                                    }

                                    descriptor.segmentation_upid_type = segmentationDescriptor.upidType;
                                    
                                    if (! segmentationDescriptor.upid.empty()) {
                                        descriptor.segmentation_upid.copy(
                                            segmentationDescriptor.upid.data(),
                                            segmentationDescriptor.upid.size()
                                        );
                                    }
                                    
                                    descriptor.segmentation_type_id  = segmentationDescriptor.segmentationTypeId;
                                    descriptor.segment_num           = segmentationDescriptor.segmentNum;
                                    descriptor.segments_expected     = segmentationDescriptor.segmentsExpected;
                                    descriptor.sub_segment_num       = segmentationDescriptor.subSegmentNum;
                                    descriptor.sub_segments_expected = segmentationDescriptor.subSegmentsExpected;
                                    
                                    descriptor.program_segmentation = true;

                                    {
                                        ts::Descriptor baseDescriptor;

                                        descriptor.serialize(duckContext, baseDescriptor);

                                        rawDescriptors.insert(rawDescriptors.end(), baseDescriptor.content(), baseDescriptor.content() + baseDescriptor.size());
                                    }
                                }

                                sit->descriptors = gst_mpegts_parse_descriptors(rawDescriptors.data(), rawDescriptors.size());

                            }

                            gstSection = scheduler::sectionFromSit(sit);
                        }

                        if (gstSection) {
                            data.setSection(gstSection);
                            data.setTimingMap(scheduler::buildRunningTimeMap(gstSection, spliceRunningTime));

                            gst_mpegts_section_unref(gstSection);

                            this->getChannel()->injectScte35Event(data);

                            // The longest segmentation_duration, in milliseconds (the field is 90 kHz).
                            utpl::Timestamp durationMs = 0;
                            std::string     segmentationIds;

                            for (const auto &descriptor : this->_event.getSegmentationDescriptors()) {
                                if (descriptor.hasSegmentationDuration) {
                                    durationMs = std::max<utpl::Timestamp>(durationMs, descriptor.segmentationDuration / 90);
                                }

                                segmentationIds += fmt::format("{}{}:0x{:02X}", segmentationIds.empty() ? "" : ",", descriptor.eventId, descriptor.segmentationTypeId);
                            }

                            // Injected _prerollTime ahead of the splice point; at= is the splice point.
                            const auto spliceAt = utpl::Time::getUTCTimestamp() + this->_prerollTime;

                            logFired(this->_event, spliceAt, spliceRunningTime, fmt::format(
                                "segmentation={} duration_ms={} preroll_ms={} section={}",
                                segmentationIds.empty() ? "-" : segmentationIds, durationMs, this->_prerollTime,
                                this->_event.hasRawSection() ? "operator" : "built"
                            ));

                            // A segmentation with a duration ends by itself; the scheduler's stop
                            // timer never runs for it, because the event's own duration is 0.
                            if (durationMs > 0) {
                                const auto endAt          = spliceAt + durationMs;
                                const auto endRunningTime = spliceRunningTime + durationMs * GST_MSECOND;

                                this->getChannel()->addTimedCallback(endAt, 0, Channel::TimedCallbackSource::OUTPUT, [event = this->_event, endAt, endRunningTime](pipeline::TimedCallbackId, GstClockTime) {
                                    logEnded(event, endAt, endRunningTime, "reason=segmentation_duration_elapsed");

                                    return false;
                                });
                            }
                        }
                    }
                });
            }

            return false;
        }
    );
}

transcoder::EventScheduler::ScheduledTimeSignalEvent::~ScheduledTimeSignalEvent() {
    if (this->_prerollScteCallbackId) {
        this->getChannel()->cancelTimedCallback(this->_prerollScteCallbackId);

        this->_prerollScteCallbackId = 0;
    }
}

transcoder::Event *transcoder::EventScheduler::ScheduledTimeSignalEvent::getEvent() {
    return &this->_event;
}

void transcoder::EventScheduler::ScheduledTimeSignalEvent::handleStart() {
    DBG(("CALL"));
}

void transcoder::EventScheduler::ScheduledTimeSignalEvent::handleStop() {
    DBG(("CALL"));
}

void transcoder::EventScheduler::ScheduledTimeSignalEvent::update(Event *event) {
    
}

bool transcoder::EventScheduler::ScheduledTimeSignalEvent::cancel() {
    {
        std::unique_lock lock(this->_guard);

        if (this->_prerollScteCallbackId) {
            this->getChannel()->cancelTimedCallback(this->_prerollScteCallbackId);

            this->_prerollScteCallbackId = 0;
        }
    }

    return ScheduledEvent::cancel();
}