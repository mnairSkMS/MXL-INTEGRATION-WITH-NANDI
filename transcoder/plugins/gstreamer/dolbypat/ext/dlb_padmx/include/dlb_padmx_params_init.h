/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                Copyright (C) 2020-2023 by Dolby Laboratories,
 *                Copyright (C) 2020-2023 by Dolby International AB.
 *                            All rights reserved.
 ******************************************************************************/

/**
 * @file  dlb_padmx_params_init.h
 *
 * @brief File containing initialization-time structures and enum definitions
 */

#ifndef DLB_PADMX_PARAMS_INIT_H
#define DLB_PADMX_PARAMS_INIT_H

#include "dlb_padmx_params_common.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter Data Structures
 * @{
 */

/**
 * @typedef dlb_padmx_version
 * @brief Library version structure
 */
typedef struct dlb_padmx_version_s
{
    unsigned int v_major;    /**< Major release */
    unsigned int v_minor;    /**< Minor release */
    unsigned int v_func;     /**< Functional release */
    unsigned int v_build;    /**< Build number */
}
dlb_padmx_version;

/**
 * @typedef dlb_padmx_init_config
 * @brief Structure used for library initialization
 */
typedef struct dlb_padmx_init_config_s
{
    unsigned int renderer_mode;         /**< Desired renderer mode: downmix/upmix, allowed values defined by
                                             dlb_padmx_renderer_mode enum */
    unsigned int input_speaker_cfg;     /**< Input speaker configuration, allowed values defined by
                                             dlb_padmx_input_speaker_config enum. Note that this parameter can be 
                                             modified at run-time, see dlb_padmx_runtime_params structure. */
    unsigned int target_speaker_cfg;    /**< Desired output mode, allowed values defined by dlb_padmx_target_speaker_config enum */
    dlb_padmx_presentation_selection pres_select;     /**< Desired presentation selected for rendering. */

    dlb_padmx_trim_config    trims_5_1; /**< Trim values for the 5.1 target speaker config. */
    dlb_padmx_downmix_config gains_2_0; /**< Downmix gains for the 2.0 target speaker config. */
}
dlb_padmx_init_config;


/**
 * @typedef dlb_padmx_requirements
 * @brief Component requirements reported by the library
 */
typedef struct dlb_padmx_requirements_s
{
    unsigned int latency;            /**< Uniform latency in samples for all component configurations */
    unsigned int max_input_samples;  /**< Maximum number of samples (per channel) to be added as input */
    unsigned int max_output_samples; /**< Maximum number of output samples to get from component at once */
    unsigned int number_of_channels; /**< Number of input and output channels */
}
dlb_padmx_requirements;

/** @} */

#ifdef __cplusplus
}
#endif

#endif // DLB_PADMX_PARAMS_INIT_H

