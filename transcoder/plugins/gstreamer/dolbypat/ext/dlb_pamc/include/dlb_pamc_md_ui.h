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
#ifndef DLB_PAMC_MD_UI_H
#define DLB_PAMC_MD_UI_H

#ifdef __cplusplus
extern "C" {
#endif


#include "dlb_pamc_md_ui_format/dlb_pamc_ui_md_sadm.h"

/**    
* @defgroup STD_PARAM_ENUM Parameter Value Enumerations    
* @{    
*/      

/**
 * @enum dlb_pamc_ui_md_type_t
 * @brief Defines type of updated metadata
 */
typedef enum dlb_pamc_ui_md_type_t
{
    DLB_PAMC_UI_MD_TYPE_SADM  = 0,         /**< Serialized ADM metadata type */

    DLB_PAMC_UI_MD_TYPE_DEFAULT = DLB_PAMC_UI_MD_TYPE_SADM
}
dlb_pamc_ui_md_type;

/** @} */


/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter Data Structures
 * @{
 */

/**
 * @brief Structure stores information of format specific metadata
 */
typedef struct dlb_pamc_ui_md_s
{
    dlb_pamc_ui_md_type type;        /**< Type of metadata */
    union
    {
        dlb_pamc_ui_md_sadm sadm;    /**< Contains Serialized ADM metadata */
    }format_md;                      /**< Union of metadata structures. It needs to be interpreted according to 'type' field */
}
dlb_pamc_ui_md;

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PAMC_MD_UI_H */
