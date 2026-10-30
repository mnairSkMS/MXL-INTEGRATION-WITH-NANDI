#ifndef __DLBPATENC_UTILS_H__
#define __DLBPATENC_UTILS_H__

#include "common/dolby.h"
#include "common/utils.h"

#include "types.h"

gboolean dlbpat_utils_gstCodec2paecCodec(DlbPatCodec codec,  dlb_paec_type *paecCodec);

gboolean dlbpat_utils_gstDataRateToDdpDataRate(GstDlbPatEncDataRate rate, unsigned int *paecDataRate);
gboolean dlbpat_utils_gstDataRateToAc4DataRate(GstDlbPatEncDataRate rate, enum dlb_paec_ac4_datarate *paecDataRate);

gboolean dlbpat_utils_gstChannels2peacDdpChannels(gint channels, unsigned int *paecChannels);
gboolean dlbpat_utils_gstChannels2paecAc4Channels(gint channels, enum dlb_paec_ac4_bed_channel_config *paecChannels);

#endif /* __DLBPATENC_UTILS_H__ */
