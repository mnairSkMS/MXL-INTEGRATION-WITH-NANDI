/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                Copyright (C) 2020-2022 by Dolby Laboratories,
 *                Copyright (C) 2020-2022 by Dolby International AB.
 *                            All rights reserved.
 ******************************************************************************/

/**
 * @file  dlb_padc_params_init_ddp.h
 *
 * @brief File containing definitions Dolby Digital Plus init parameters
 */

#ifndef DLB_PADC_PARAMS_INIT_DDP_H
#define DLB_PADC_PARAMS_INIT_DDP_H

#include "dlb_padc_params_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup STD_PARAM_ENUM Parameter Value Enumerations
 * @{
 */

/**
 * @typedef dlb_padc_drc_setting
 * @brief enum used to select DRC setting in Dolby Digital Plus decoder
 */
typedef enum
{
    DLB_PADC_DRC_NONE = 0,  /**< DRC mode none */
    DLB_PADC_DRC_LINE = 1,  /**< DRC mode line */
    DLB_PADC_DRC_RF = 2,    /**< DRC mode RF */
    DLB_PADC_DRC_DEFAULT = DLB_PADC_DRC_NONE, /**< DRC mode none as default */
}
dlb_padc_drc_setting;


/**
 * @typedef dlb_padc_speaker_config
 * @brief enum used to select speaker configuration in Dolby Digital Plus decoder
 */
typedef enum
{
    DLB_PADC_SPEAKER_CONFIG_STEREO = 0, /**< Stereo decoder output */
    DLB_PADC_SPEAKER_CONFIG_5_1 = 1,    /**< 5.1 decoder output */
    DLB_PADC_SPEAKER_CONFIG_7_1 = 2,    /**< 7.1 decoder output */
    DLB_PADC_SPEAKER_CONFIG_5_1_2 = 3,  /**< 5.1.2 decoder output */
    DLB_PADC_SPEAKER_CONFIG_5_1_4 = 4,  /**< 5.1.4 decoder output */
    DLB_PADC_SPEAKER_CONFIG_7_1_4 = 5,  /**< 7.1.4 decoder output */
    DLB_PADC_SPEAKER_CONFIG_9_1_6 = 6,   /**< 9.1.6 decoder output */
    DLB_PADC_SPEAKER_CONFIG_DEFAULT = DLB_PADC_SPEAKER_CONFIG_5_1, /**< 5.1 decoder output as default */
}
dlb_padc_speaker_config;


/**
 * @typedef dlb_padc_dmx_mode
 * @brief enum used to select preferred downmix type in Dolby Digital Plus decoder, for stereo downmix only
 */
typedef enum
{
    DLB_PADC_DOWNMIX_AUTO = 0,  /**< Preferred downmix type selected based on bitstream */
    DLB_PADC_DOWNMIX_LTRT = 1,  /**< Preferred downmix type LtRt */
    DLB_PADC_DOWNMIX_LORO = 2,   /**< Preferred downmix type LoRo */
    DLB_PADC_DOWNMIX_DEFAULT = DLB_PADC_DOWNMIX_AUTO, /**< Preferred downmix auto type as default */
}
dlb_padc_dmx_mode;


/**
 * @typedef dlb_padc_error_conceal
 * @brief enums used to select preferred error concealment type
 */
typedef enum
{
    DLB_PADC_ERROR_CONCEAL_MUTE = 0,    /**< Mute output when bitstream error is detected */
    DLB_PADC_ERROR_CONCEAL_REPEAT = 1,  /**< Repeat last correctly decoded frame when bitstream error is detected */
    DLB_PADC_ERROR_CONCEAL_DEFAULT = DLB_PADC_ERROR_CONCEAL_REPEAT, /**< Last frame repeat as default */
}
dlb_padc_error_concealment;

/** @} */


/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter Data Structures
 * @{
 */

/**
 * @brief Initialization parameters for Dolby Digital Plus decoder
 */
typedef struct dlb_padc_initcfg_ddp_s
{
    unsigned int drc_setting;       /**< Allowed values defined by dlb_padc_drc_setting enum */
    unsigned int speaker_config;    /**< Allowed values defined by dlb_padc_speaker_config enum */
    unsigned int dmx_mode;          /**< Allowed values defined by dlb_padc_dmx_mode enum */
    unsigned int error_concealment; /**< Allowed values defined by dlb_padc_error_concealment enum */
}
dlb_padc_initcfg_ddp;

/** @} */

#ifdef __cplusplus
}
#endif


#endif // DLB_PADC_PARAMS_INIT_DDP_H



