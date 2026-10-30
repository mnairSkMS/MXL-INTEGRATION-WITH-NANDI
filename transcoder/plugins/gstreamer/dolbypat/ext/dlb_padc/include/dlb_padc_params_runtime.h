/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                Copyright (C) 2020 by Dolby Laboratories,
 *                Copyright (C) 2020 by Dolby International AB.
 *                            All rights reserved.
 ******************************************************************************/

/**
 *  @file  dlb_padc_params_runtime.h
 *
 *  @brief File containing run-time related structures definitions
 */

#ifndef DLB_PADC_PARAMS_RUNTIME_H
#define DLB_PADC_PARAMS_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include "dlb_params_runtime_codec/dlb_padc_params_runtime_dolbye.h"
#include "dlb_params_runtime_codec/dlb_padc_params_runtime_ddp.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
* @defgroup DATA_STRUCT_HIGH_LEVEL Parameter Data Structures
* @{
*/  

/**
 * @brief Decoder run-time configuration
 */
typedef struct dlb_padc_runtime_params_s
{
    /* Codec-specific run-time configuration */
    union
    {
        dlb_padc_runtime_params_dolbye dolbye; /**< Dolby E run-time configuration */
        dlb_padc_runtime_params_ddp ddp;       /**< Dolby Digital Plus run-time configuration */
    }
    codec;
}
dlb_padc_runtime_params;

/** @} */

#ifdef __cplusplus
}
#endif


#endif /* DLB_PADC_PARAMS_RUNTIME_H */

