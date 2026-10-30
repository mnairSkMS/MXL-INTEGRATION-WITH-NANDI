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
 * @file  dlb_padc_params_runtime_ddp.h
 *
 * @brief File containing definitions Dolby Digital Plus run-time parameters
 */

#ifndef DLB_PADC_PARAMS_RUNTIME_DDP_H
#define DLB_PADC_PARAMS_RUNTIME_DDP_H

#include "dlb_padc_params_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter Data Structures
 * @{
 */

/**
 * @brief Structure used to configure Dolby Digital Plus run-time parameters.
 */
typedef struct dlb_padc_runtime_params_ddp_s
{
    unsigned int error_concealment;
}
dlb_padc_runtime_params_ddp;

/** @} */

#ifdef __cplusplus
}
#endif

#endif // DLB_PADC_PARAMS_RUNTIME_DDP_H
