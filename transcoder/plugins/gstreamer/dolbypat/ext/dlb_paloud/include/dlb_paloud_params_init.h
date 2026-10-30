/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                  Copyright (C) 2020-2023 by Dolby Laboratories.
 *                            All rights reserved.
 ******************************************************************************/

/**
 *  @file dlb_paloud_params_init.h
 *
 *  @brief Dolby pro audio loudness leveler initialization parameters.
 *
 *  Initialization parameters for the library.
 */

#ifndef DLB_PALOUD_PARAMS_INIT_H
#define DLB_PALOUD_PARAMS_INIT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup STD_PARAM_ENUM Parameter value enumerations
 * @{
*/

/**
 * @brief Channel configuration
 * Use @ref dlb_paloud_channel_config to control the number of channels
 * that the component processes.
 * 
 * @note For AC4_MODE and METADATA_AUTHORING_ONLY_MODE authoring modes, all channels are passed, regardless of channel configuration.
 */
typedef enum dlb_paloud_channel_config_t
{
     DLB_PALOUD_CHANNEL_CONFIG_10          = 0  /**< First channel for a mono program */
    ,DLB_PALOUD_CHANNEL_CONFIG_20          = 1  /**< First two channels for a stereo program */
    ,DLB_PALOUD_CHANNEL_CONFIG_51          = 2  /**< First six channels for a 5.1 program */
    ,DLB_PALOUD_CHANNEL_CONFIG_514         = 3  /**< First ten channels for a 5.1.4 program */

    ,DLB_PALOUD_CHANNEL_CONFIG_DEFAULT    = DLB_PALOUD_CHANNEL_CONFIG_514
} dlb_paloud_channel_config;


/**
 * @brief Authoring mode
 * Use AC4_MODE when encoding to Dolby AC-4.
 * Use NON_AC4_MODE to level the loudness of the input PCM when encoding to Dolby Digital, Dolby Digital Plus, or Dolby Digital Plus with Dolby Atmos content.
 * Use METADATA_AUTHORING_ONLY_MODE to author loudness metadata when a third-party loudness leveler is used; the input PCM will not be leveled.
 *
 * @note ON and OFF loudness leveler modes are still valid for AC4_MODE and NON_AC4_MODE. For METADATA_AUTHORING_ONLY_MODE, the loudness leveler mode is always set to OFF.
 */
typedef enum author_mode_t
{
     NON_AC4_MODE                 = 0     /**< PALOUD is used to level the loudness of input PCM */
    ,AC4_MODE                     = 1     /**< PALOUD mode when encoding to Dolby AC-4, the latency is 0 */
    ,METADATA_AUTHORING_ONLY_MODE = 2     /**< PALOUD is used to author loudness metadata only */
} author_mode;
/** @} */


/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter data structures (high level)
 * @{
 */

/**
 * @brief Initialization configuration
 */
typedef struct dlb_paloud_init_config_s
{
    int                                 channel_cfg;                /**< Input channel configuration @ref dlb_paloud_channel_config */
    unsigned int                        author_mode;                /**< Mode for leveling with Dolby real-time loudness leveler @ref author_mode */
    unsigned int                        internal_data;              /**< Internal library use only, do not edit */
} dlb_paloud_init_config;


/**
 * @brief Loudness requirements structure
 *
 * @latency
 * The library returns a latency number of silent samples before it begins to output processed data.
 */
typedef struct dlb_paloud_requirements_s
{
    unsigned int    max_input_samples;      /**< Maximum number of samples (per channel) to be added as input */
    unsigned int    max_output_samples;     /**< Maximum number of output samples to get from the component at once */
    unsigned int    number_of_channels;     /**< Number of input and output channels */
    unsigned int    latency;                /**< Latency in samples for the PALOUD component */
} dlb_paloud_requirements;

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PALOUD_PARAMS_INIT_H */
