#include "utpl/common/debug.h"
#include "utpl/common/types/exception.h"

#include "utpl/transcoder/stream/output/pad.h"

#include "mpegts.h"

using namespace utpl;

utpl::transcoder::output::MpegTsMuxer::MpegTsMuxer(const std::string &name, const OutputParameters &parameters) {
    this->_name = name;
    this->_outputParameters = parameters;
    this->_bin = pipeline::Bin::create();
}

void utpl::transcoder::output::MpegTsMuxer::link() {
    this->_muxer = this->_bin->add(pipeline::Element::make(this->_name, "mpegtsmux"));

    {
        auto splitter = this->_bin->add(pipeline::Element::make(this->_name + "-tee", "tee"));

        this->_muxer->link(splitter);
        for (auto &pad : this->_srcPads) {
            pad->setTarget(splitter->requestPad("src_%u"));
        }
    }

    // mpegtsmux takes a stream's PID from its pad name. Configured PIDs are requested first:
    // a muxer-numbered pad requested earlier could already hold one of them.
    for (bool fixed : {true, false}) {
        for (auto &pad : this->_sinkPads) {
            if ((pad.first != PID_NULL) != fixed) {
                continue;
            }

            auto target = this->_muxer->requestPad(fixed ? "sink_" + std::to_string(pad.first) : "sink_%d");

            if (! *target) {
                throw InvalidArgumentException("Output '" + this->_outputParameters.getName() + "': unable to mux a stream on PID " + std::to_string(pad.first));
            }

            pad.second->setTarget(target);
        }
    }

    {
        const auto &scte = this->_outputParameters.getScteParameters();

        if (scte.getPid() != PID_NULL) {
            guint scteInterval = scte.getNullInterval();

            if (scteInterval < 0) {
                scteInterval = 0;
            }

            // Convert to 90kHz ticks
            scteInterval *= 90;

            // fixed since 1.25.1
            //(transcoder:410670): GLib-GObject-CRITICAL **: 19:52:17.531: value "0" of type 'guint' is invalid or out of range for property 'scte-35-null-interval' of type 'guint'
            this->_muxer->setProperty<guint>("scte-35-null-interval", scteInterval);
            this->_muxer->setProperty<guint>("scte-35-pid", scte.getPid());
        }
    }
}

const std::string &utpl::transcoder::output::MpegTsMuxer::getName() {
    return this->_name;
}

void utpl::transcoder::output::MpegTsMuxer::putScteData(const Scte35Data &data) {
    if (this->_muxer) {
        auto *event = gst_event_new_mpegts_section((GstMpegtsSection *) data.getSection());

        if (event) {
            auto timingMap = data.getTimingMap();

            if (timingMap) {
                auto *s = gst_event_writable_structure(event);

                gst_structure_set(s,
                 "mpeg-pts-offset", G_TYPE_UINT64, 0,
                 "running-time-map", GST_TYPE_STRUCTURE, data.getTimingMap(),
                 NULL
                );
            }

            if (! gst_element_send_event((*this->_muxer)->get(), event)) {
                ERR(("Unable to send SCTE event to muxer!"));

                gst_event_unref (event);
            }
        }
    }
}

pipeline::ElementPtr utpl::transcoder::output::MpegTsMuxer::getNode() {
    return this->_bin;
}

pipeline::PadPtr utpl::transcoder::output::MpegTsMuxer::getSrcPad() {
    this->_srcPads.push_back(this->_bin->addPad("", nullptr, true));
    
    return this->_srcPads.back();
}

pipeline::PadPtr utpl::transcoder::output::MpegTsMuxer::getSinkPad(MediaType mediaType, Pid pid) {
    this->_sinkPads.push_back({pid, this->_bin->addPad("", nullptr, false)});

    return this->_sinkPads.back().second;
}
