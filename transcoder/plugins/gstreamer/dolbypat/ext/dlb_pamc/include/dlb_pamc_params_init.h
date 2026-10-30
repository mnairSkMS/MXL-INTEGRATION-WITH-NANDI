/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                  Copyright (C) 2023 by Dolby Laboratories.
 *                            All rights reserved.
 ******************************************************************************/

/**
 *  @file dlb_pamc_params_init.h
 *
 *  @brief Dolby Pro Audio Metadata Component (PAMC) initialization parameters.
 *
 *  Initialization parameters for the PAMC.
 */

#ifndef DLB_PAMC_PARAMS_INIT_H
#define DLB_PAMC_PARAMS_INIT_H


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Source type for component
 */
typedef enum
{
     DLB_PAMC_TYPE_SADM       = 0     /**< Sets source type to S-ADM */

    ,DLB_PAMC_TYPE_DEFAULT  = DLB_PAMC_TYPE_SADM
} dlb_pamc_source_type;

/**
 * @brief Init-time PAMC configuration parameters.
 */
typedef struct
{
    unsigned int                     source_type;           /**< Input metadata type, see dlb_pamc_source_type.
                                                             *   -  default: DLB_PAMC_TYPE_DEFAULT
                                                             */
} dlb_pamc_init_config;


/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PAMC_PARAMS_INIT_H */
