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
 *  @file  dlb_padmx_params_runtime.h
 *
 *  @brief File containing run-time related structures definitions
 */

#ifndef DLB_PADMX_PARAMS_RUNTIME_H
#define DLB_PADMX_PARAMS_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "dlb_padmx_params_common.h"

/**
 * @brief PADMX run-time configuration
 */
typedef struct dlb_padmx_runtime_params_s
{
    unsigned int input_speaker_cfg;                 /**< Input speaker configuration for upmix, allowed values defined by
                                                        dlb_padmx_input_speaker_config enum. */
    dlb_padmx_presentation_selection pres_select;   /**< Desired presentation selected for rendering. */

    dlb_padmx_trim_config    trims_5_1;             /**< Trim values for the 5.1 target speaker config. */
    dlb_padmx_downmix_config gains_2_0;             /**< Downmix gains for the 2.0 target speaker config. */
}
dlb_padmx_runtime_params;

#ifdef __cplusplus
}
#endif

#endif // DLB_PADMX_PARAMS_RUNTIME_H

