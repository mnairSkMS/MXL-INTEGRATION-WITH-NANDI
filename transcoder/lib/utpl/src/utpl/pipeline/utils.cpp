#include "utpl/common/debug.h"

#include "utpl/pipeline/utils.h"

using namespace utpl;

pipeline::ElementPtr pipeline::utils::createQueue(BinPtr bin, const std::string &name, guint sizeBuffers, guint sizeBytes, guint64 sizeTime) {
    auto ret = pipeline::Element::make(name, "queue");

    ret->setProperty<gboolean>("silent", true);
    ret->setProperty<guint>   ("max-size-buffers", sizeBuffers);
    ret->setProperty<guint>   ("max-size-bytes",   sizeBytes);
    ret->setProperty<guint64> ("max-size-time",    sizeTime);

    if (bin) {
        ret = bin->add(ret);
    }

    return ret;
}

pipeline::ElementPtr pipeline::utils::createIdentity(BinPtr bin, const std::string &name, bool callHandoffs, bool silent) {
    auto ret = pipeline::Element::make(name, "identity");

    ret->setProperty<gboolean>("silent",          silent);
    ret->setProperty<gboolean>("signal-handoffs", callHandoffs);

    if (bin) {
        ret = bin->add(ret);
    }

    return ret;
}

void pipeline::utils::addDebugTsWatcher(pipeline::PadPtr pad, const std::string &name) {
    pad->addProbe(
        {
            GST_PAD_PROBE_TYPE_BUFFER,
            GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM
        },
        [pad, name](GstPadProbeInfo *info) {
            auto infoType = GST_PAD_PROBE_INFO_TYPE(info);

            if (infoType & GST_PAD_PROBE_TYPE_BUFFER) {
                GstClockTime time;

                {
                    auto *buffer = gst_pad_probe_info_get_buffer(info);

                    time = GST_BUFFER_PTS(buffer);
                }

                {
                    auto *event = gst_pad_get_sticky_event((*pad)->get(), GST_EVENT_SEGMENT, 0);
                    if (event) {
                        const GstSegment *segment;

                        gst_event_parse_segment(event, &segment);
        
                        time = gst_segment_to_running_time(segment, GST_FORMAT_TIME, time);

                        gst_event_unref(event);
                    }
                }
                
                {
                    char str[128];

                    snprintf(str, sizeof(str) - 1, "%" GST_TIME_FORMAT, GST_TIME_ARGS(time));

                    ERR(("Buffer timestamp on element {} = {}", name, str));
                }
            }

            return true;
        }
    );
}

bool pipeline::utils::isDolbyPatCompatible(const utpl::transcoder::StreamParameters &parameters, bool encoded) {
    if (parameters.isPassthrough()) {
        return false;
    }

    if (! parameters.hasAudioParameters()) {
        return false;
    }

    {
        auto &params = parameters.getAudioParameters();

        if (
            encoded &&
            parameters.getCodec() != Codec::AC3 &&
            parameters.getCodec() != Codec::EAC3
        ) {
            return false;
        }

        if (params.getRate() != 48000) {
            return false;
        }

        auto encoderName = parameters.getEncoderParameters().getName();

        if (! encoderName.empty() && (encoderName != "dlbpatenc")) {
            return false;
        }
    }

    return true;
}