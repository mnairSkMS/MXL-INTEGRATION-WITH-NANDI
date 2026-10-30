#include "utpl/esam/client.h"
#include "utpl/transcoder/types.h"
#include "utpl/common/types.h"

#define DEBUG_LEVEL 6
#include "utpl/common/debug.h"

guint8 timeSignal[] = {0xfc, 0x30, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xf0, 0x01, 0x06, 0x7f, 0x00, 0x12, 0x02, 0x10, 0x43, 0x55, 0x45, 0x49, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x8a, 0x14, 0xff};
gsize timeSignalSize = sizeof(timeSignal) / sizeof(timeSignal[0]);

guint8 spliceInsert[] = {0xfc, 0x30, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xf0, 0x05, 0x05, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x12, 0x02, 0x10, 0x43, 0x55, 0x45, 0x49, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x37, 0x1c, 0x0b, 0x66};
gsize spliceInsertSize = sizeof(spliceInsert) / sizeof(spliceInsert[0]);

int main(int argc, char const *argv[]) {
    utpl::Debug::initialize();

    // utpl::esam::Client client("https://r28ww6im02.execute-api.eu-west-1.amazonaws.com/v1/esam");
    utpl::esam::Client client("http://localhost:8001/esam");

    LOG(("Sending splice insert"));
    {
        // splice insert table
        guint8 *data = (guint8 *)g_malloc(spliceInsertSize);
        memcpy(data, spliceInsert, spliceInsertSize);

        // takes ownership of data
        GstMpegtsSection *section = gst_mpegts_section_new(1234, data, spliceInsertSize);

        utpl::transcoder::Scte35Data scte35;
        scte35.setSection(section);

        utpl::esam::EventResponse response;
        utpl::esam::SignalParameters signalParams;
        signalParams.sectionData.assign(spliceInsert, spliceInsert + spliceInsertSize);
        signalParams.acquisitionTime = utpl::Time::getUTCTimestamp();
        signalParams.signalTimestamp = utpl::Time::getUTCTimestamp();
        signalParams.pts = 333000;

        client.signalEvent(signalParams, response);

        gst_mpegts_section_unref(section);
    }

    LOG(("Sending time signal"));
    {
        // time_signal
        guint8 *data = (guint8 *)g_malloc(timeSignalSize);
        memcpy(data, timeSignal, timeSignalSize);

        GstMpegtsSection *section = gst_mpegts_section_new(1234, data, timeSignalSize);

        utpl::transcoder::Scte35Data scte35;
        scte35.setSection(section);

        utpl::esam::EventResponse response;
        utpl::esam::SignalParameters signalParams;
        signalParams.sectionData.assign(spliceInsert, spliceInsert + spliceInsertSize);
        signalParams.acquisitionTime = utpl::Time::getUTCTimestamp();
        signalParams.signalTimestamp = utpl::Time::getUTCTimestamp();
        signalParams.pts = 333000;

        client.signalEvent(signalParams, response);

        gst_mpegts_section_unref(section);
    }

    return 0;
}


