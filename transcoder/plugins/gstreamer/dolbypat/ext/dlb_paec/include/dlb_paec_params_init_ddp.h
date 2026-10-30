/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                  Copyright (C) 2021-2022 by Dolby Laboratories.
 *                            All rights reserved.
 ******************************************************************************/

/**
 *  @file dlb_paec_params_init_ddp.h
 *
 *  @brief Dolby pro audio encoder component (PAEC)
 *         Dolby Digital Plus with Dolby Atmos initialization parameters.
 *
 *  Initialization parameters for PAEC.
 */

#ifndef DLB_PAEC_PARAMS_INIT_DDP_H
#define DLB_PAEC_PARAMS_INIT_DDP_H


#ifdef __cplusplus
extern "C" {
#endif


/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter data structures (high level)
 * @{
 */

typedef struct
{
    /** Drop the initial two frames out of the Dolby Digital / Dolby Digital Plus encoder
     *  -  1 = drop the initial two frames; 0 = do not drop the initial two frames
     *  -  default: 1: drop the initial two frames
     */
    int             b_snip_ddp_startup_frames;
} dlb_paec_initcfg_ddp;

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PAEC_PARAMS_INIT_DDP_H */
