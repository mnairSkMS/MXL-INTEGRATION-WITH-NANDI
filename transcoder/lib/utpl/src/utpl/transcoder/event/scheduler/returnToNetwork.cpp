#include <gst/mpegts/gst-scte-section.h>

#include "utpl/common/utils/gstreamer.h"
#include "utpl/common/debug.h"

#include "scte.h"
#include "returnToNetwork.h"

using namespace utpl;


transcoder::EventScheduler::ScheduledReturnToNetworkEvent::ScheduledReturnToNetworkEvent(
    EventProvider *provider, 
    Channel *channel, 
    const EventReturnToNetwork &event,
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
                return false;
            }

            processor->getConnector()->peekPad()->probeCurrentPts([&](GstClockTime pts, GstClockTime gst) {
                Scte35Data data;
                GstMpegtsSection *gstSection = nullptr;

                data.setRunningTime(gst);

                const auto spliceTimePts     = pts + this->_prerollTime * GST_MSECOND;
                const auto spliceRunningTime = gst + this->_prerollTime * GST_MSECOND;

                if (this->_event.hasRawSection()) {
                    LOG(("Injecting operator-supplied return-to-network section ({} bytes) for event ID {}",
                        this->_event.getRawSection().size(), this->_event.getEventId()
                    ));

                    gstSection = scheduler::sectionFromBase64(
                        this->_event.getRawSection(), spliceTimePts, GST_MTS_SCTE_SPLICE_COMMAND_INSERT
                    );

                } else {
                    auto *sit = gst_mpegts_scte_sit_new();

                    sit->splice_command_type = GST_MTS_SCTE_SPLICE_COMMAND_INSERT;

                    {
                        GstMpegtsSCTESpliceEvent *event = gst_mpegts_scte_splice_event_new();

                        event->splice_event_id          = this->_event.getEventId();
                        event->out_of_network_indicator = false;
                        event->insert_event             = true;

                        event->program_splice_time = GST_UTILS_GSTTIME_TO_MPEGTIME(spliceTimePts);

                        if (event->program_splice_time) {
                            event->program_splice_time_specified = true;
                            event->program_splice_flag           = true;
                        }

                        event->break_duration_auto_return = false;
                        event->duration_flag              = false;

                        // This field provides an identification for a specific avail within one
                        // unique_program_id. This value is expected to increment with each new avail within a viewing event. This
                        // value is expected to reset to one for the first avail in a new viewing event. This field is expected to
                        // increment for each new avail. It may optionally carry a zero value to indicate its non-usage.
                        event->avail_num = 0;

                        g_ptr_array_add(sit->splices, event);
                    }

                    gstSection = scheduler::sectionFromSit(sit);
                }

                if (gstSection) {
                    data.setSection(gstSection);
                    data.setTimingMap(scheduler::buildRunningTimeMap(gstSection, spliceRunningTime));

                    this->getChannel()->injectScte35Event(data);

                    gst_mpegts_section_unref(gstSection);

                    // Injected _prerollTime ahead of the splice point; at= is the splice point.
                    logFired(this->_event, utpl::Time::getUTCTimestamp() + this->_prerollTime, spliceRunningTime, fmt::format(
                        "splice_event_id={} out_of_network=no preroll_ms={} section={}",
                        this->_event.getEventId(), this->_prerollTime,
                        this->_event.hasRawSection() ? "operator" : "built"
                    ));
                }
            });

            return false;
        }
    );
}

transcoder::EventScheduler::ScheduledReturnToNetworkEvent::~ScheduledReturnToNetworkEvent() {
    if (this->_prerollScteCallbackId) {
        this->getChannel()->cancelTimedCallback(this->_prerollScteCallbackId);

        this->_prerollScteCallbackId = 0;
    }
}

transcoder::Event *transcoder::EventScheduler::ScheduledReturnToNetworkEvent::getEvent() {
    return &this->_event;
}

void transcoder::EventScheduler::ScheduledReturnToNetworkEvent::handleStart() {
    
}

void transcoder::EventScheduler::ScheduledReturnToNetworkEvent::handleStop() {
    
}

void transcoder::EventScheduler::ScheduledReturnToNetworkEvent::update(Event *event) {
    
}

bool transcoder::EventScheduler::ScheduledReturnToNetworkEvent::cancel() {
    {
        std::unique_lock lock(this->_guard);

        if (this->_prerollScteCallbackId) {
            this->getChannel()->cancelTimedCallback(this->_prerollScteCallbackId);

            this->_prerollScteCallbackId = 0;
        }
    }

    return ScheduledEvent::cancel();
}