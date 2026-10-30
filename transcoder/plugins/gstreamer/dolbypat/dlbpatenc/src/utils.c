#include <gst/gst.h>

#include "utils.h"


GST_DEBUG_CATEGORY_EXTERN (gst_dlbpatenc_debug);
#define GST_CAT_DEFAULT gst_dlbpatenc_debug


gboolean dlbpat_utils_gstCodec2paecCodec(DlbPatCodec codec,  dlb_paec_type *paecCodec) {
    switch (codec) {
        case DLBPAT_CODEC_AC3:  *paecCodec = DLB_PAEC_TYPE_DD;  break;
        case DLBPAT_CODEC_EAC3: *paecCodec = DLB_PAEC_TYPE_DDP; break;
        case DLBPAT_CODEC_AC4:  *paecCodec = DLB_PAEC_TYPE_AC4; break;

        default:
            GST_WARNING("Not supported by PAEC module codec type (%d) - returning default value", codec);

            *paecCodec = DLB_PAEC_TYPE_DEFAULT;
            return FALSE;
    }

    return TRUE;
}


gboolean dlbpat_utils_gstDataRateToDdpDataRate(GstDlbPatEncDataRate rate, unsigned int *paecDataRate) {
    switch (rate) {
        case GST_DLBPAT_ENC_DATARATE_32K:  *paecDataRate = DLB_PAEC_DDP_DATARATE_32K;     break;
        case GST_DLBPAT_ENC_DATARATE_56K:  *paecDataRate = DLB_PAEC_DDP_DATARATE_56K;     break;
        case GST_DLBPAT_ENC_DATARATE_64K:  *paecDataRate = DLB_PAEC_DDP_DATARATE_64K;     break;
        case GST_DLBPAT_ENC_DATARATE_96K:  *paecDataRate = DLB_PAEC_DDP_DATARATE_96K;     break;
        case GST_DLBPAT_ENC_DATARATE_128K: *paecDataRate = DLB_PAEC_DDP_DATARATE_128K;    break;
        case GST_DLBPAT_ENC_DATARATE_192K: *paecDataRate = DLB_PAEC_DDP_DATARATE_192K;    break;
        case GST_DLBPAT_ENC_DATARATE_256K: *paecDataRate = DLB_PAEC_DDP_DATARATE_256K;    break;
        case GST_DLBPAT_ENC_DATARATE_384K: *paecDataRate = DLB_PAEC_DDP_DATARATE_384K;    break;
        case GST_DLBPAT_ENC_DATARATE_448K: *paecDataRate = DLB_PAEC_DDP_DATARATE_448K;    break;
        case GST_DLBPAT_ENC_DATARATE_640K: *paecDataRate = DLB_PAEC_DDP_DATARATE_640K;    break;
        case GST_DLBPAT_ENC_DATARATE_768K: *paecDataRate = DLB_PAEC_DDP_DATARATE_768K;    break;
        case GST_DLBPAT_ENC_DATARATE_AUTO: *paecDataRate = DLB_PAEC_DDP_DATARATE_DEFAULT; break;

        default:
            GST_WARNING("Not supported by PAEC DDP module bitrate value (%d) - returning default value", rate);

            *paecDataRate = DLB_PAEC_DDP_DATARATE_DEFAULT;
            return FALSE;
    }

    return TRUE;
}


gboolean dlbpat_utils_gstDataRateToAc4DataRate(GstDlbPatEncDataRate rate, enum dlb_paec_ac4_datarate *paecDataRate) {
    switch (rate) {
        case GST_DLBPAT_ENC_DATARATE_48K:   *paecDataRate = DLB_PAEC_AC4_DATARATE_48K;   break;
        case GST_DLBPAT_ENC_DATARATE_64K:   *paecDataRate = DLB_PAEC_AC4_DATARATE_64K;   break;
        case GST_DLBPAT_ENC_DATARATE_96K:   *paecDataRate = DLB_PAEC_AC4_DATARATE_96K;   break;
        case GST_DLBPAT_ENC_DATARATE_128K:  *paecDataRate = DLB_PAEC_AC4_DATARATE_128K;  break;
        case GST_DLBPAT_ENC_DATARATE_144K:  *paecDataRate = DLB_PAEC_AC4_DATARATE_144K;  break;
        case GST_DLBPAT_ENC_DATARATE_192K:  *paecDataRate = DLB_PAEC_AC4_DATARATE_192K;  break;
        case GST_DLBPAT_ENC_DATARATE_256K:  *paecDataRate = DLB_PAEC_AC4_DATARATE_256K;  break;
        case GST_DLBPAT_ENC_DATARATE_288K:  *paecDataRate = DLB_PAEC_AC4_DATARATE_288K;  break;
        case GST_DLBPAT_ENC_DATARATE_320K:  *paecDataRate = DLB_PAEC_AC4_DATARATE_320K;  break;
        case GST_DLBPAT_ENC_DATARATE_384K:  *paecDataRate = DLB_PAEC_AC4_DATARATE_384K;  break;
        case GST_DLBPAT_ENC_DATARATE_448K:  *paecDataRate = DLB_PAEC_AC4_DATARATE_448K;  break;
        case GST_DLBPAT_ENC_DATARATE_512K:  *paecDataRate = DLB_PAEC_AC4_DATARATE_512K;  break;
        case GST_DLBPAT_ENC_DATARATE_768K:  *paecDataRate = DLB_PAEC_AC4_DATARATE_768K;  break;
        case GST_DLBPAT_ENC_DATARATE_1024K: *paecDataRate = DLB_PAEC_AC4_DATARATE_1024K; break;
        case GST_DLBPAT_ENC_DATARATE_AUTO:  *paecDataRate = DLB_PAEC_AC4_DATARATE_EMISSION_DEFAULT; break;

        default:
            GST_WARNING("Not supported by PAEC DDP module bitrate value (%d) - returning default value", rate);

            *paecDataRate = DLB_PAEC_AC4_DATARATE_EMISSION_DEFAULT;
            return FALSE;
    }

    return TRUE;
}


gboolean dlbpat_utils_gstChannels2peacDdpChannels(gint channels, unsigned int *paecChannels) {
    switch (channels) {
        case  1: *paecChannels = DLB_PAEC_DDP_CHANNEL_CONFIG_10;  break;
        // case  1: *paecChannels = DLB_PAEC_DDP_CHANNEL_CONFIG_1_1; break; // dual mono?? 
        case  2: *paecChannels = DLB_PAEC_DDP_CHANNEL_CONFIG_20;  break;
        case  6: *paecChannels = DLB_PAEC_DDP_CHANNEL_CONFIG_51;  break;
        case 10: *paecChannels = DLB_PAEC_DDP_CHANNEL_CONFIG_514; break;
        case 12: *paecChannels = DLB_PAEC_DDP_CHANNEL_CONFIG_714; break;
        case 16: *paecChannels = DLB_PAEC_DDP_CHANNEL_CONFIG_916; break;

        default:
            GST_WARNING("Not supported by PAEC DDP module channels value (%d) - returning default value", channels);

            *paecChannels = DLB_PAEC_DDP_CHANNEL_CONFIG_DEFAULT;
            return FALSE;
    }

    return TRUE;
}


gboolean dlbpat_utils_gstChannels2paecAc4Channels(gint channels, enum dlb_paec_ac4_bed_channel_config *paecChannels) {
    switch (channels) {
        case  2: *paecChannels = DLB_PAEC_AC4_BED_CHANNEL_CONFIG_20;  break;
        case  6: *paecChannels = DLB_PAEC_AC4_BED_CHANNEL_CONFIG_51;  break;
        case 10: *paecChannels = DLB_PAEC_AC4_BED_CHANNEL_CONFIG_514; break;

        default:
            GST_WARNING("Not supported by PAEC AC4 module channels value (%d) - returning default value", channels);

            *paecChannels = DLB_PAEC_AC4_BED_CHANNEL_CONFIG_PRIMARY_DISTRIBUTION_DEFAULT;
            return FALSE;
    }

    return TRUE;
}