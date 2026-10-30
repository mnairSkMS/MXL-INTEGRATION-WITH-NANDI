/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                  Copyright (C) 2023-2024 by Dolby Laboratories.
 *                            All rights reserved.
 ******************************************************************************/

/**
 *  @file dlb_pamc.h
 *
 *  @brief Dolby Pro Audio Metadata Component (PAMC).
 *
 *  Top level functions for the PAMC.
 */

#ifndef DLB_PAMC_H
#define DLB_PAMC_H

#include "dlb_pamc_common.h"
#include "dlb_pamc_params_init.h"
#include "dlb_pamc_external_md.h"
#include "dlb_pamc_md_ui.h"
#include "dlb_pamif.h"
#include "dlb_paio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup FUNCTIONS API functions
 * @{
 */

/**
 * @brief Retrieve the version of the PAMC
 *
 * @return dlb_pamc_status
 * - DLB_PAMC_ERROR_NULL_POINTER:   if p_version is NULL
 * - DLB_PAMC_STATUS_OK:            otherwise
 */
int
dlb_pamc_version_get
    (dlb_pamc_version           *p_version          /**< [out] Version of the PAMC. */
    );

/**
 * @brief Setup initialization-time data structures for valid operation
 *
 * This function populates each member of the output structure with a default value.
 * It is provided to support backward compatibility for future library versions.
 *
 * @return dlb_pamc_status
 * - DLB_PAMC_ERROR_NULL_POINTER:         if p_init_config is NULL
 * - DLB_PAMC_STATUS_OK:                  otherwise
 */
int
dlb_pamc_setup
    (dlb_pamc_init_config     *p_init_config      /**< [out] Initial library configuration structure */
    );

/**
 * @brief Open an instance of the library
 *
 * NOTE: This function MUST be called just once before calling any of the proceeding functions.
 *
 * @return dlb_pamc_status
 * - DLB_PAMC_ERROR_NULL_POINTER:       if ph_state or p_init_config is NULL
 * - DLB_PAMC_ERROR_INIT:               if any parameter in p_init_config is out of range
 * - DLB_PAMC_ERROR_INTERNAL:           An internal error occures
 * - DLB_PAMC_STATUS_OK:                otherwise
 */
int
dlb_pamc_open
    (dlb_pamc_state         **ph_state          /**< [out] PAMC library state handle */
    ,dlb_pamc_init_config    *p_init_config     /**< [in]  Initial library configuration.*/
    );

/**
 * @brief Convert incoming metadata to MIF
 * 
 * NOTE: PAMIF metadata segment should be initialized
 * 
 * NOTE: Duration could be set manually [DLB_PAIO_PCM_BLOCK_SIZE_MIN, DLB_PAIO_PCM_BLOCK_SIZE_MAX] 
 *       or follow the incoming metadata (DLB_PAMC_MD_DURATION_AUTO) 
 * 
 * NOTE: If error occures during conversion, last correct MIF will be returned. 
 * If there isn't previous MIF, default configuration (2.0CM) will be returned.
 * 
 * NOTE: The function performs dynamic memory allocations at run time 
 * during parsing and conversion of S-ADM due to the use of 
 * C++ Standard Template Library containers.
 *
 * @return dlb_pamc_status
 * - DLB_PAMC_ERROR_NULL_POINTER:           if h_state is NULL
 * - DLB_PAMC_STATUS_WARNING                conversion succeeded, warning is added 
 * - DLB_PAMC_ERROR_CONVERT                 conversion failed, previous metadata is used
 * - DLB_PAMC_STATUS_OK:                    otherwise
 */
int
dlb_pamc_convert
    (dlb_pamc_state          *h_state           /**< [in] PAMC library state handle */
    ,const char              *p_buffer          /**< [in] Input buffer, could be NULL */
    ,size_t                   size              /**< [in] Length of input buffer */
    ,uint32_t                 duration          /**< [in] Metadata duration in samples, 
                                                  *       could be set to DLB_PAMC_MD_DURATION_AUTO 
                                                  *       or value between DLB_PAIO_PCM_BLOCK_SIZE_MIN (1) 
                                                  *       and DLB_PAIO_PCM_BLOCK_SIZE_MAX (2048)
                                                  */     
    ,dlb_pamif_md_segment    *h_segment         /**< [in/out] PAMIF metadata segment handle */
    );


/**
 * @brief Close an instance of the PAMC
 *
 * NOTE: dlb_pamc_open() MUST have been called just once before calling this function.
 *
 * @return dlb_pamc_status
 * - DLB_PAMC_ERROR_NULL_POINTER:   if h_state is NULL
 * - DLB_PAMC_STATUS_OK:            otherwise
 */
int
dlb_pamc_close
    (dlb_pamc_state     *h_state            /**< [in]  PAMC library state handle */
    );



/**
 * @brief Get the monitoring metadata from the PAMC
 * 
 * @return dlb_pamc_status
 * - DLB_PAMC_ERROR_NULL_POINTER:                if h_state or p_monitoring_metadata is NULL
 * - DLB_PAMC_ERROR_MONITORING_METADATA_INVALID: if monitoring metadata can not be obtained from ingested metadata
 * - DLB_PAMC_STATUS_OK:                         otherwise
*/
int
dlb_pamc_monitoring_metadata_get
    (dlb_pamc_state    *h_state                 /**< [in] PAMC library state handle */
    ,dlb_pamc_ui_md    *p_monitoring_metadata   /**< [in/out] Pointer to the PAMC monitoring metadata structure */
    );

/**
 * @brief Get warnings from last function call
 */
dlb_warnings
dlb_pamc_warnings_get
    (dlb_pamc_state         *h_state            /**< [in]  PAMC state handle */
    );

/**
 * @brief Setup external metadata structure for valid operation
 *
 * This function populates each member of the output structure with a default value.
 * It is provided to support backward compatibility for future library versions.
 *
 * @return dlb_pamc_status
 * - DLB_PAMC_ERROR_NULL_POINTER:         if p_external_md is NULL
 * - DLB_PAMC_STATUS_OK:                  otherwise
 */
int
dlb_pamc_get_default_external_md
    (dlb_pamc_external_md     *p_external_md   /**< [out] External metadata structure */
    );


/**
 * @brief Convert incoming external metadata to MIF
 * 
 * NOTE: PAMIF metadata segment should be initialized
 * 
 * NOTE: If error occurs during conversion, MIF segment won't be updated
 *
 * @return dlb_pamc_status
 * - DLB_PAMC_ERROR_NULL_POINTER:            if p_external_md or h_segment is NULL
 * - DLB_PAMC_ERROR_NO_EXTERNAL_MD_SETUP     if dlb_pamc_get_default_external_md() was not called prior to dlb_pamc_external_md_convert()
 * - DLB_PAMC_ERROR_<parametr>_OUT_OF_RANGE  if external metadata parameter is out-of-range
 * - DLB_PAMC_ERROR_DURATION_OUT_OF_RANGE    if duration input parameter is out-of-range
 * - DLB_PAMC_STATUS_OK:                     otherwise
*/
int
dlb_pamc_external_md_convert
    (const dlb_pamc_external_md *p_external_md   /**< [in] External metadata structure */
    ,uint32_t                    duration        /**< [in] Metadata duration in samples, could be set
                                                  *        to value between DLB_PAIO_PCM_BLOCK_SIZE_MIN (1)
                                                  *        and DLB_PAIO_PCM_BLOCK_SIZE_MAX (2048)
                                                  */
    ,dlb_pamif_md_segment       *h_segment       /**< [in/out] PAMIF metadata segment handle */
    );

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PAEC_H */
