/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                  Copyright (C) 2020-2022 by Dolby Laboratories.
 *                            All rights reserved.
 ******************************************************************************/

/**
 *  @file dlb_paec_params_runtime.h
 *
 *  @brief Dolby pro audio encoder component (PAEC) run-time parameters.
 *
 *  Run-time parameters for PAEC.
 */

#ifndef DLB_PAEC_PARAMS_RUNTIME_H
#define DLB_PAEC_PARAMS_RUNTIME_H

#include "dlb_paec_params_runtime_ac4.h"
#include "dlb_paec_params_runtime_ddp.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @defgroup STD_PARAM_ENUM Parameter value enumerations
 * @{
 */


/**
 * @brief Dialogue normalization range
 */
enum dlb_paec_dialnorm_range
{
    /** Acceptable dialogue normalization range:
     *  -  The encoder will accept both positive and negative values for the
     *  -  dialogue normalization parameter. A positive value will be converted to the corresponding
     *  -  negative value. For example, 14 dB will be converted to -14 dB.
     */
     DLB_PAEC_DIALNORM_MIN              = -31   /**< -31 dB */
    ,DLB_PAEC_DIALNORM_MAX              = -1    /**< -1 dB */

    ,DLB_PAEC_DIALNORM_DEFAULT          = -24   /**< -24 dB */
};


/**
 * @brief Dynamic range control profiles
 */
enum dlb_paec_drc_profile
{
     /** Auto mode:
      *  -  in a transcoding use case, follows external metadata if available (dlb_paec_input.p_md)
      *  -  otherwise, DLB_PAEC_DRC_PROFILE_AUTO_DEFAULT is selected
      */
     DLB_PAEC_DRC_PROFILE_AUTO              = -1

    ,DLB_PAEC_DRC_PROFILE_NONE              = 0     /**< No specified profile */
    ,DLB_PAEC_DRC_PROFILE_FILM_STANDARD     = 1     /**< Film Standard */
    ,DLB_PAEC_DRC_PROFILE_FILM_LIGHT        = 2     /**< Film Light */
    ,DLB_PAEC_DRC_PROFILE_MUSIC_STANDARD    = 3     /**< Music Standard */
    ,DLB_PAEC_DRC_PROFILE_MUSIC_LIGHT       = 4     /**< Music Light */
    ,DLB_PAEC_DRC_PROFILE_SPEECH            = 5     /**< Speech */

    ,DLB_PAEC_DRC_PROFILE_DEFAULT           = DLB_PAEC_DRC_PROFILE_AUTO
    ,DLB_PAEC_DRC_PROFILE_AUTO_DEFAULT      = DLB_PAEC_DRC_PROFILE_FILM_STANDARD
};


/** @} */


/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter data structures (high level)
 * @{
 */


/**
 * @brief Encoder run-time configuration
 */
typedef struct
{
    /** Codec-specific run-time configuration */
    union
    {
        dlb_paec_runtime_config_ac4     ac4;    /**< Dolby AC-4 encoding run-time configuration */
        dlb_paec_runtime_config_ddp     ddp;    /**< Dolby Digital Plus with Dolby Atmos encoding
                                                 *   run-time configuration
                                                 */
    } codec;
} dlb_paec_runtime_config;


/**
 * @brief Information about encoded frame
 */
typedef struct
{
    /** Frame information is available only for Dolby AC-4 codec */
    dlb_paec_frame_info_ac4     ac4;    /**< Dolby AC-4 frame information */
} dlb_paec_frame_info;


/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PAEC_PARAMS_RUNTIME_H */
