#ifndef __DLBPAT_COMMON_UTILS_H__
#define __DLBPAT_COMMON_UTILS_H__

#include <gst/gst.h>
#include <gst/audio/audio-channels.h>

#include "common/dolby.h"

G_BEGIN_DECLS

#define DLBPAT_SUCCESS   0
#define DLBPAT_FAILURE  -1

#define DLBPAT_CHANNELS_NOT_SET -1
#define DLBPAT_CHANNELS_AUTO     0

#define DLBPAT_DEFAULT_SAMPLE_RATE     48000
#define DLBPAT_DEFAULT_OUTPUT_CHANNELS 6
#define DLBPAT_DEFAULT_OUTPUT_CODEC    DLBPAT_CODEC_AC3

#define DLBPAT_MIMETYPE_ES   "audio/x-raw"
#define DLBPAT_MIMETYPE_AC3  "audio/x-ac3"
#define DLBPAT_MIMETYPE_EAC3 "audio/x-eac3"
#define DLBPAT_MIMETYPE_AC4  "audio/x-ac4"

#define DLBPAT_SAMPLE_SIZE  sizeof(int32_t)

#define DLBPAT_SAMPLES_COUNT(_bufferSize) ((_bufferSize) / (DLB_PAIO_PCM_NUM_CHANNELS * DLBPAT_SAMPLE_SIZE))
#define DLBPAT_SAMPLES_SIZE(_samplesCount)((_samplesCount) * (DLB_PAIO_PCM_NUM_CHANNELS * DLBPAT_SAMPLE_SIZE))

#define IS_NULL(x)  ((x) == NULL)
#define NOT_NULL(x) ((x) != NULL)

#define DLBPAT_CALL_CHECK(f, args) { \
    int dlbRet = f args; \
    if (dlbRet != DLBPAT_SUCCESS) { \
        GST_ERROR("Calling method " #f " has failed with code: %d!", dlbRet); \
    } \
}

#define DLBPAT_CALL_BREAK(ret, f, args) { \
    ret = f args; \
    if (ret != DLBPAT_SUCCESS) { \
        GST_ERROR("Calling method " #f " has failed with code: %d!", ret); \
        break; \
    } \
}


typedef enum _DlbPatCodec {
    DLBPAT_CODEC_UNKNOWN,
    
    DLBPAT_CODEC_ES,
    DLBPAT_CODEC_AC3,
    DLBPAT_CODEC_EAC3,
    DLBPAT_CODEC_AC4
} DlbPatCodec;


void dlbpat_utils_clearPaioPcmBuffer(dlb_paio_pcm_block *block);
void dlbpat_utils_freePaioPcmBuffer(dlb_paio_pcm_block *block);
void dlbpat_utils_clearPaioBitstream(dlb_paio_bitstream_block *block);
void dlbpat_utils_freePaioBitstream(dlb_paio_bitstream_block *block);

void dlbpat_utils_paioPcmBlockInit(dlb_paio_pcm_block *block);
void dlbpat_utils_paioPcmBlockAllocate(dlb_paio_pcm_block *block, gsize bufferSize);

DlbPatCodec dlbpat_utils_mimeTypeToCodec(const gchar *mimeType);
const char *dlbpat_utils_codecToMimeType(DlbPatCodec codec);

// `pts` advanced by `samples` samples at the Dolby sample rate (GST_CLOCK_TIME_NONE stays NONE).
GstClockTime dlbpat_utils_offsetPts(GstClockTime pts, guint64 samples);

// 5.1 sources name their surrounds rear (most decoders) or side (WAV, some decoders); both are the Dolby Ls/Rs.
// Maps them to the surround positions of the Dolby layout so they are reordered instead of rejected.
void dlbpat_utils_normalizePositions(GstAudioChannelPosition *positions, gint channels);

void dlbpat_utils_getChannelsPosition(GstAudioChannelPosition positions[DLB_PAIO_PCM_NUM_CHANNELS], gsize channelsNum);

G_END_DECLS

#endif /* __DLBPAT_COMMON_UTILS_H__ */
