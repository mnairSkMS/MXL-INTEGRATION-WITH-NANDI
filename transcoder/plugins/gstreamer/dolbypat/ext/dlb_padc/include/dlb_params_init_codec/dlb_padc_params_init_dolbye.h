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
 * @file  dlb_padc_params_init_dolbye.h
 *
 * @brief File containing definitions Dolby E init parameters
 */

#ifndef DLB_PADC_PARAMS_INIT_DOLBYE_H
#define DLB_PADC_PARAMS_INIT_DOLBYE_H

#include "dlb_padc_params_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter Data Structures
 * @{
 */

/**
 * @typedef dlb_padc_dolbye_decoding_mode
 * @brief enum used to select one of the frame decoding modes
 */
typedef enum
{
    DLB_PADC_DOLBYE_DECODING_MODE_REGULAR,
    DLB_PADC_DOLBYE_DECODING_MODE_LOW_LATENCY,
    DLB_PADC_DOLBYE_DECODING_MODE_DEFAULT = DLB_PADC_DOLBYE_DECODING_MODE_REGULAR
}
dlb_padc_dolbye_decoding_mode;

/**
 * @brief Initialization parameters for Dolby E decoder
 */
typedef struct dlb_padc_initcfg_dolbye_s
{
    unsigned int program;       /**< Init-time selection of a program to decode, allowed values defined by dlb_padc_program enum */
    unsigned int output_mode;   /**< Init-time selection of an output channel mode of the decoder, allowed values defined by dlb_padc_channel_mode enum  */
    unsigned int decoding_mode; /**< Init-time selection of a frame decoding mode, allowed values defined by dlb_padc_dolbye_decoding_mode enum  */
}
dlb_padc_initcfg_dolbye;

/** @} */

#ifdef __cplusplus
}
#endif


#endif // DLB_PADC_PARAMS_INIT_DOLBYE_H



