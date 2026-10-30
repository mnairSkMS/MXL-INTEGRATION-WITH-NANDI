#include "common/utils.h"


typedef struct _DlbPatChannelsLayout {
    int channels;
    
    GstAudioChannelPosition positions[16];
} DlbPatChannelsLayout;


static const DlbPatChannelsLayout channelLayouts[] = {
    { 0, {} },
    { 1, {
        GST_AUDIO_CHANNEL_POSITION_MONO
    }},
    { 2, {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT
    // 3, 4, 5
    }}, { 0, {}}, { 0, {}}, { 0, {}},
    { 6, { // 5.1
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_LFE1,
        GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT,
        GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT
    // 7
    }}, { 0, {} }, 
    { 8, { // 5.1.2
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_LFE1,
        GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT,
        GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT,
    // 9
    }}, { 0, {} },
    { 10, { // 5.1.4
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_LFE1,
        GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT,
        GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT
    // 11
    }}, { 0, {} },
    { 12, { // 7.1.4
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_LFE1,
        GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT,
        GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT
    // 13, 14, 15
    }}, { 0, {} }, { 0, {} }, { 0, {} },
    { 16, { // 9.1.6
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_LFE1,
        GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT,
        GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_LEFT,
        GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_WIDE_LEFT,
        GST_AUDIO_CHANNEL_POSITION_WIDE_RIGHT
    }}
};


void dlbpat_utils_clearPaioPcmBuffer(dlb_paio_pcm_block *block) {
    block->num_channels = 0;
    block->num_samples  = 0;
    block->p_md         = NULL;
    block->p_pcm        = NULL;
}


void dlbpat_utils_freePaioPcmBuffer(dlb_paio_pcm_block *block) {
    if (NOT_NULL(block->p_pcm)) {
        free(block->p_pcm);
    }

    dlbpat_utils_clearPaioPcmBuffer(block);
}


void dlbpat_utils_clearPaioBitstream(dlb_paio_bitstream_block *block) {
    for (int i = 0; i < DLB_PAIO_MAX_NUM_BITSTREAMS; i++) {
        block->p_bitstreams[i] = NULL;
        block->num_bytes[i]    = 0;
    }

    block->num_bitstreams = 0;
}


void dlbpat_utils_freePaioBitstream(dlb_paio_bitstream_block *block) {
    for (int i = 0; i < DLB_PAIO_MAX_NUM_BITSTREAMS; i++) {
        if (NOT_NULL(block->p_bitstreams[i])) {
            free(block->p_bitstreams[i]);
        }
    }

    dlbpat_utils_clearPaioBitstream(block);
}


void dlbpat_utils_paioPcmBlockInit(dlb_paio_pcm_block *block) {
    block->num_channels = DLB_PAIO_PCM_NUM_CHANNELS;
    block->num_samples  = 0;
    block->p_pcm        = NULL;
    block->p_md         = NULL;
}


void dlbpat_utils_paioPcmBlockAllocate(dlb_paio_pcm_block *block, gsize bufferSize) {
    dlbpat_utils_paioPcmBlockInit(block);

    block->p_pcm = malloc(bufferSize);
}


DlbPatCodec dlbpat_utils_mimeTypeToCodec(const gchar *mimeType) {
    if (NOT_NULL(mimeType)) {
        if (strcmp(mimeType, DLBPAT_MIMETYPE_AC3) == 0) {
            return DLBPAT_CODEC_AC3;
        }

        if (strcmp(mimeType, DLBPAT_MIMETYPE_EAC3) == 0) {
            return DLBPAT_CODEC_EAC3;
        }

        if (strcmp(mimeType, DLBPAT_MIMETYPE_AC4) == 0) {
            return DLBPAT_CODEC_AC4;
        }

        if (strcmp(mimeType, DLBPAT_MIMETYPE_ES) == 0) {
            return DLBPAT_CODEC_ES;
        }
    }
    
    return DLBPAT_CODEC_UNKNOWN;
}


const char *dlbpat_utils_codecToMimeType(DlbPatCodec codec) {
    switch (codec) {
        case DLBPAT_CODEC_AC3:  return DLBPAT_MIMETYPE_AC3;
        case DLBPAT_CODEC_EAC3: return DLBPAT_MIMETYPE_EAC3;
        case DLBPAT_CODEC_AC4:  return DLBPAT_MIMETYPE_AC4;

        default:
            return DLBPAT_MIMETYPE_AC3;
    }
}


void dlbpat_utils_getChannelsPosition(GstAudioChannelPosition positions[DLB_PAIO_PCM_NUM_CHANNELS], gsize channelsNum) {
    const DlbPatChannelsLayout *layout = NULL;

    do {
        if (channelsNum < 0) {
            break;
        }

        if (channelsNum >= sizeof(channelLayouts) / sizeof(*channelLayouts)) {
            break;
        }

        if (channelLayouts[channelsNum].channels == 0) {
            break;
        }

        layout = &channelLayouts[channelsNum];
    } while (0);

    for (int i = 0; i < DLB_PAIO_PCM_NUM_CHANNELS; i++) {
        if (NOT_NULL(layout) && i < layout->channels) {
            positions[i] = layout->positions[i];

        } else {
            positions[i] = GST_AUDIO_CHANNEL_POSITION_INVALID;
        }
    }
}


GstClockTime dlbpat_utils_offsetPts(GstClockTime pts, guint64 samples) {
    if (! GST_CLOCK_TIME_IS_VALID(pts)) {
        return GST_CLOCK_TIME_NONE;
    }

    return pts + gst_util_uint64_scale_int(samples, GST_SECOND, DLBPAT_DEFAULT_SAMPLE_RATE);
}


void dlbpat_utils_normalizePositions(GstAudioChannelPosition *positions, gint channels) {
    gboolean haveSurround = FALSE;

    if (channels != 6) {
        return;
    }

    for (gint i = 0; i < channels; i++) {
        if (positions[i] == GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT || positions[i] == GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT) {
            haveSurround = TRUE;
        }
    }

    if (haveSurround) {
        return;
    }

    for (gint i = 0; i < channels; i++) {
        switch (positions[i]) {
            case GST_AUDIO_CHANNEL_POSITION_REAR_LEFT:
            case GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT:
                positions[i] = GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT;
                break;

            case GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT:
            case GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT:
                positions[i] = GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT;
                break;

            default:
                break;
        }
    }
}
