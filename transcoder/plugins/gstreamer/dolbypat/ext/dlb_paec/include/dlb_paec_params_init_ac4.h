/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                  Copyright (C) 2021-2023 by Dolby Laboratories.
 *                            All rights reserved.
 ******************************************************************************/

/**
 *  @file dlb_paec_params_init_ac4.h
 *
 *  @brief Dolby pro audio encoder component (PAEC)
 *         Dolby AC-4 initialization parameters.
 *
 *  Initialization parameters for PAEC.
 */

#ifndef DLB_PAEC_PARAMS_INIT_AC4_H
#define DLB_PAEC_PARAMS_INIT_AC4_H


#ifdef __cplusplus
extern "C" {
#endif

#include "dlb_paec_params_common_ac4.h"

/**
 * @defgroup STD_PARAM_ENUM Parameter value enumerations
 * @{
 */

/**
 * @brief Dolby AC-4 frame rates
 *
 * The Dolby AC-4 frame rate is assumed to match the frame rate of the corresponding video presentation.
 * DLB_PAEC_AC4_FRAME_RATE_NATIVE may be used if no video presentation is present or alignment is not needed.
 *
 * For primary distribution workflows, supported frame rates are DLB_PAEC_AC4_FRAME_RATE_29_97, DLB_PAEC_AC4_FRAME_RATE_25,
 * and DLB_PAEC_AC4_FRAME_RATE_NATIVE.
 */
enum dlb_paec_ac4_frame_rate
{
     DLB_PAEC_AC4_FRAME_RATE_23_98  = 0     /**< 23.976 fps, 2002 samples per frame */
    ,DLB_PAEC_AC4_FRAME_RATE_24     = 1     /**< 24 fps, 2000 samples per frame */
    ,DLB_PAEC_AC4_FRAME_RATE_25     = 2     /**< 25 fps, 1920 samples per frame */
    ,DLB_PAEC_AC4_FRAME_RATE_29_97  = 3     /**< 29.97 fps, number of samples per frame changes in the following pattern:
                                                 1602, 1602, 1601, 1602, 1601 */
    ,DLB_PAEC_AC4_FRAME_RATE_30     = 4     /**< 30 fps, 1600 samples per frame */
    ,DLB_PAEC_AC4_FRAME_RATE_NATIVE = 5     /**< AC-4 native frame rate, 2048 samples per frame */
    ,DLB_PAEC_AC4_FRAME_RATE_DEFAULT  = DLB_PAEC_AC4_FRAME_RATE_29_97
};

/**
 * @brief Specifies encoding mode.
 *
 * In primary distribution mode, the bitstream meets the following requirements: low-latency independent dialogue enhancement,
 * low-latency encoding, all I-frames.
 * In emission mode, a look-ahead analysis is utilized to improve the quality of independent dialogue enhancement.
 */
enum dlb_paec_ac4_encoding_mode
{
     DLB_PAEC_AC4_EMISSION             = 0       /**< Emission mode */
    ,DLB_PAEC_AC4_PRIMARY_DISTRIBUTION = 1       /**< Primary distribution mode */
    ,DLB_PAEC_AC4_ENCODING_MODE_DEFAULT  = DLB_PAEC_AC4_EMISSION
};

/**
 * @brief Constants for main audio ducking level (scale_main)
 */
enum dlb_paec_ac4_scale_main_constants
{
     DLB_PAEC_AC4_SCALE_MAIN_MIN       = -24     /**< Minimum allowed value for main audio ducking level (scale_main) in dB */
    ,DLB_PAEC_AC4_SCALE_MAIN_MAX       = 0       /**< Maximum allowed value for main audio ducking level (scale_main) in dB */
    ,DLB_PAEC_AC4_SCALE_MAIN_DEFAULT   = -10     /**< Default value for main audio ducking level (scale_main) in dB */
};

/**
 * @brief Dolby system component latencies
 *
 * There are different latencies that depend on component settings.
 * Component latencies are queried from their requirement functions and passed to the encoder at initialization time.
 */
typedef struct system_latency {
    unsigned int padc_latency;      /**< A latency queried from PADC (if exists), else 0 */
    unsigned int padmx_latency;     /**< A latency queried from PADMX (if exists), else 0 */
    unsigned int paloud_latency;    /**< A latency queried from PALOUD (if exists), else 0 */
} system_latency ;

/**
 * @brief Defines the amount of attenuation (in dB) applied to a Complete Main or
 * a mix of Music and Effects with Dialogue when associated audio is active.
 * The valid range is from DLB_PAEC_AC4_SCALE_MAIN_MIN to DLB_PAEC_AC4_SCALE_MAIN_MAX.
 * The default value is DLB_PAEC_AC4_SCALE_MAIN_DELAULT.
 */
typedef struct
{
    int audio_description;         /**< attenuation of Audio Description audio */
} dlb_paec_ac4_scale_main;

/** @} */

/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter data structures (high level)
 * @{
 */
typedef struct
{
    unsigned int                   frame_rate;                  /**< See dlb_paec_ac4_frame_rate */
    unsigned int                   encoding_mode;               /**< Encoding mode, see dlb_paec_ac4_encoding_mode */
    system_latency                 pcm_delay_samples;           /**< Delay (in samples) coming from other Dolby components included in a signal chain: PADC, PALOUD, and PADMX */
    dlb_paec_ac4_scale_main        scale_main;                  /**< See dlb_paec_ac4_scale_main */
    char                           languages[DLB_PAEC_AC4_MAX_NUM_LANGUAGES][DLB_PAEC_AC4_MAX_LANGUAGE_TAG_BYTES];
                                                                /**< Specifies a list of supported languages. For each language, specify a language tag according to
                                                                 *   ISO 639-1, ISO 639-2 or BCP-47 and terminate it with a NULL character. For BCP-47, only the primary
                                                                 *   subtag is used. The first string in the array starting with a NULL character marks the end of the list.
                                                                 *   Presentations passed with dlb_paec_ac4_apc must be in the same order as they appear in the array. Languages
                                                                 *   which are not passed in the same order or are not part of the array are encoded, but may not be played
                                                                 *   back flawlessly across program boundaries.*/

} dlb_paec_initcfg_ac4;

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PAEC_PARAMS_INIT_AC4_H */
