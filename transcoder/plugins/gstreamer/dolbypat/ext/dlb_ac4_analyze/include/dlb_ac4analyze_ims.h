/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *              Copyright (C) 2018-2023 by Dolby International AB.
 *                            All rights reserved.
 ******************************************************************************/

/**
 *  @file
 *
 *  @brief AC-4 bitstream analyzer API - IMS.
 */

/**
 * @ingroup ac4analyzer_api_ims
 * @{
 */

#ifndef DLB_AC4ANALYZE_IMS_H
#define DLB_AC4ANALYZE_IMS_H

#define AC4ANALYZE_EMDF_ID_IMS         (18)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Information about IMS frame
 */
typedef struct ac4analyze_ims_info_s
{
    /*
     * ims_header
     */
    unsigned int    ims_version;
    unsigned int    ims_ramp_len;
    int             ims_b_iframe;
    unsigned int    ims_b_loud_corr;
    int             ims_loud_corr;
    
    /*
     * ims_tx_config
     */
    unsigned int    ims_tx_band_config;
    unsigned int    ims_tx_quant_config;
    
    /*
     * ims_fdn_config
     */
    unsigned int    ims_b_fdn_present;
    unsigned int    ims_fdn_band_config;
    unsigned int    ims_fdn_quant_config;
    unsigned int    ims_fdnhelp_quant_config;
    
    /*
     * ims_dlg_config
     */
    unsigned int    ims_b_dlg_present;
    unsigned int    ims_dlg_band_config;
    unsigned int    ims_dlg_quant_config;
    
} ac4analyze_ims_info_t;

#ifdef __cplusplus
}
#endif

#endif /* DLB_AC4ANALYZE_IMS_H */

/**
 *  @}
 */
