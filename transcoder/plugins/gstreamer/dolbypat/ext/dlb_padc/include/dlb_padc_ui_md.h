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
 *  @file  dlb_padc_ui_md.h
 *
 *  @brief File containing common definitions for UI metadata
 */

#ifndef __DLB_PADC_UI_MD
#define __DLB_PADC_UI_MD
 
#ifdef __cplusplus
extern "C" {
#endif
 
#include "dlb_padc_ui_md_codec/dlb_padc_ui_md_dolbye.h"
#include "dlb_padc_ui_md_codec/dlb_padc_ui_md_ddp.h"
#include "dlb_padc_ui_md_codec/dlb_padc_ui_md_ed2.h"

/**    
* @defgroup STD_PARAM_ENUM Parameter Value Enumerations    
* @{    
*/      

/**
 * @enum dlb_padc_ui_md_type_t
 * @brief Defines type of updated metadata
 */
typedef enum dlb_padc_ui_md_type_t
{
    DLB_PADC_UI_MD_TYPE_DOLBYE  = 0,         /**< Dolby E metadata type */
    DLB_PADC_UI_MD_TYPE_DDP     = 1,         /**< DolbyDigital/DolbyDigital Plus metadata type */
    DLB_PADC_UI_MD_TYPE_ED2     = 2,         /**< Dolby ED2 metadata type */
}
dlb_padc_ui_md_type;

/** @} */


/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter Data Structures
 * @{
 */

/**
 * @brief Structure stores information of codec specific metadata
 */
typedef struct dlb_padc_ui_md_s
{
    dlb_padc_ui_md_type type;      /**< Type of metadata */
    uint64_t updated_in_sample;    /**< Number of the first sample of the frame from which metadata was obtained */
    union
    {
        dlb_padc_ui_md_dolbye   dolby_e;  /**< Contains Dolby E metadata */
        dlb_padc_ui_md_ddp      ddp;      /**< Contains Dolby Digital/Dolby Digital Plus metadata */
        dlb_padc_ui_md_ed2      ed2;      /**< Contains Dolby ED2 metadta */
    }codec_md;                            /**< Union of metadata structures, content should be interpreted according to 'type' field */
}
dlb_padc_ui_md;

/**
 * @typedef
 * @brief Metadata callback type definition.
 *
 * It defines a type used to create a function that
 * receives metadata updates from decoder.
 */
typedef void (*p_md_updated_callback)(const dlb_padc_ui_md *p_md, void *p_user_data);
 
/** @} */

#ifdef __cplusplus
}
#endif
 
#endif /* __DLB_PADC_UI_MD */
