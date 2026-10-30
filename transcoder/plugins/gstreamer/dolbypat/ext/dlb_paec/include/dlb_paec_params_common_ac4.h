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
  *  @file dlb_paec_params_common_ac4.h
  *
  *  @brief Dolby pro audio encoder component (PAEC)
  *         Dolby AC-4 common parameters for run time and initialization time.
  *
  */

#ifndef DLB_PAEC_PARAMS_COMMON_AC4_H
#define DLB_PAEC_PARAMS_COMMON_AC4_H

/**
 * @brief AC-4 constants
 *
 */
enum dlb_paec_ac4_constants
{
     DLB_PAEC_AC4_MAX_BED_COMPONENTS                        = 3     /**< Maximum number of bed components */
    ,DLB_PAEC_AC4_MAX_OBJECT_COMPONENTS                     = 4     /**< Maximum number of object components */
    ,DLB_PAEC_AC4_MAX_OBJECT_COMPONENTS_PER_PRESENTATION    = 1     /**< Maximum number of object components */
    ,DLB_PAEC_AC4_MAX_NUM_PRESENTATIONS                     = 10    /**< Maximum number of presentations */
    ,DLB_PAEC_AC4_MAX_LANGUAGE_TAG_BYTES                    = 11    /**< Maximum number of language tag bytes,
                                                                         including a NULL terminator */
    ,DLB_PAEC_AC4_MAX_NUM_LANGUAGES                         = 8     /**< Maximum number of languages */
};

#endif  /* DLB_PAEC_PARAMS_COMMON_AC4_H */
