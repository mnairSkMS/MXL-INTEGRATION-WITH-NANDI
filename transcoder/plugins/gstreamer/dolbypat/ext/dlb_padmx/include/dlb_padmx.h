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
 *  @file  dlb_padmx.h
 *
 *  @brief Top level functions for the renderer
 */

#ifndef DLB_PADMX_H
#define DLB_PADMX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "dlb_padmx_params_common.h"
#include "dlb_padmx_params_init.h"
#include "dlb_padmx_params_runtime.h"
#include "dlb_paio.h"

/**
 * @defgroup FUNCTIONS API Functions
 * @{
 */

/**
 * @brief Fills version structure with library's current version
 *
 * @return dlb_padmx_status
 * - DLB_PADMX_ERROR_NULL_POINTER: if p_version is NULL
 * - DLB_PADMX_STATUS_OK:          otherwise
 */
int
dlb_padmx_version_get
    (dlb_padmx_version      *p_version        /**< [out] Pointer to version structure */
    );

/**
 * @brief Fills component default configuration
 *
 * NOTE: This function must be called just once before dlb_padmx_query().
 *
 * @return dlb_padmx_status
 * - DLB_PADMX_ERROR_NULL_POINTER:     if p_config is NULL
 * - DLB_PADMX_STATUS_OK:              otherwise
 */
int
dlb_padmx_setup
    (dlb_padmx_init_config  *p_config            /**< [out] Pointer to configuration structure */
    );

/**
 * @brief Query requirements reported by library
 *
 * NOTE: dlb_padmx_setup() must be called just once before calling this function.
 *
 * NOTE: This function must be called just once before calling dlb_padmx_open().
 *
 * @return dlb_padmx_status
 * - DLB_PADMX_ERROR_NULL_POINTER: if p_initcfg or p_reqs is NULL
 * - DLB_PADMX_STATUS_OK:          otherwise
 */
int
dlb_padmx_query
    (dlb_padmx_init_config  *p_initcfg   /**< [in]  Pointer to configuration structure */
    ,dlb_padmx_requirements *p_reqs      /**< [out] Pointer to component requirements structure */
    );


/**
 * @brief Get warnings from the last library call from the state handle.
 * 
 * @return struct dlb_warnings filled with warnings.
*/
dlb_warnings
dlb_padmx_warnings_get
    (dlb_padmx_state *h_state
    );

/**
 * @brief Opens component instance
 *
 * NOTE: dlb_padmx_query() must be called just once before calling this function.
 *
 * NOTE: This function must be called just once before calling dlb_padmx_process() or dlb_padmx_flush() functions.
 *
 * @return dlb_padmx_status
 * - DLB_PADMX_ERROR_NULL_POINTER:     if ph_state or p_initcfg is NULL
 * - DLB_PADMX_STATUS_OK:              otherwise
 */
int
dlb_padmx_open
    (dlb_padmx_state        **ph_state          /**< [out] Pointer to state */
    ,dlb_padmx_init_config  *p_initcfg          /**< [in]  Pointer to configuration */
    );

/**
 * @brief Populate current (or default) values into PADMX run-time data structures
 *
 * @return dlb_padmx_status
 * - DLB_PADMX_ERROR_NULL_POINTER:      if any of the arguments is NULL
 * - DLB_PADMX_ERROR_CORRUPT_STATE:     if h_state does not point to a valid dlb_padmx_state
 * - DLB_PADMX_STATUS_OK:               otherwise
 */
int
dlb_padmx_runtime_config_get
    (dlb_padmx_state            *h_state        /**< [in] PADMX state handle */
    ,dlb_padmx_runtime_params   *p_config       /**< [out] Run-time configuration structure to populate */
    );

/**
 * @brief Configure the PADMX run-time data structures
 *
 * @return dlb_padmx_status
 * - DLB_PADMX_ERROR_NULL_POINTER:          if any of the arguments is NULL
 * - DLB_PADMX_ERROR_CORRUPT_STATE:         if h_state does not point to a valid dlb_padmx_state
 * - DLB_PADMX_ERROR_INVALID_CONFIGURATION: if run-time parameters are invalid
 * - DLB_PADMX_STATUS_OK:                   otherwise
 */
int
dlb_padmx_runtime_config_set
    (dlb_padmx_state            *h_state        /**< [in] PADMX state handle */
    ,dlb_padmx_runtime_params   *p_config       /**< [in] Run-time configuration to set */
    );

/**
 * @brief Add samples to the renderer component
 *
 * Fills the component internal memory with a given set of samples.
 * When enough samples are collected, sets the flag to signal
 * that the output samples can be taken.
 * 
 * dlb_padmx_add_samples() can issue the following warnings:
 * - DLB_PADMX_WARN_INPUT_FULL
 * - DLB_PADMX_WARN_UNEXPECTED_MIF_VERSION
 *
 * @return dlb_padmx_status
 * - DLB_PADMX_ERROR_NULL_POINTER:    if any of the arguments is NULL
 * - DLB_PADMX_ERROR_FLUSHED:         if the component is being or has been flushed
 * - DLB_PADMX_ERROR_INPUT_TOO_LARGE: if input data contains too many samples to add
 * - DLB_PADMX_STATUS_WARNING:        if the component encountered a warning
 * - DLB_PADMX_STATUS_OK:             otherwise
 */
int
dlb_padmx_add_samples
    (dlb_padmx_state        *h_state            /**< [in] Pointer to state handle */
    ,dlb_paio_pcm_block     *p_input            /**< [in] Pointer to input PCM data structure */
    ,int                    *p_process_ready    /**< [out] Flag indicating that dlb_padmx_process can be called */
    ,unsigned int           *p_space_available  /**< [out] Determines how many samples can still be added */
);

/**
 * @brief Fills the PCM output structure
 *
 * Function fills output structure with rendered samples and metadata.
 *
 * Provides information about samples that are still available to get.
 *
 * @return dlb_padmx_status
 * - DLB_PADMX_ERROR_NULL_POINTER:    if ph_state, p_output or p_samples_available is NULL
 * - DLB_PADMX_STATUS_OK:             otherwise
 */
int
dlb_padmx_get_samples
    (dlb_padmx_state        *h_state                /**< [in] Pointer to state handle */
    ,unsigned int           num_samples             /**< [in] Amount of samples to be extracted */
    ,dlb_paio_pcm_block     *p_output               /**< [out] Output PCM data structure */
    ,unsigned int           *p_samples_available    /**< [out] Amount of samples that can be extracted */
);

/**
 * @brief Process PCM data with PADMX
 *
 * Processes a single data block. The PCM block and the metadata block can be retrieved with the dlb_padmx_get_samples() function.
 * 
 * dlb_padmx_process() can issue the following warnings:
 * - DLB_PADMX_WARN_OUTPUT_FULL
 * - DLB_PADMX_WARN_PRESENTATION_UNAVAILABLE
 * - DLB_PADMX_WARN_MISSING_METADATA
 *
 * @return dlb_padmx_status
 * - DLB_PADMX_ERROR_NULL_POINTER:          if any of the arguments is NULL
 * - DLB_PADMX_ERROR_FLUSHED:               if the component is being or has been flushed
 * - DLB_PADMX_ERROR_INVALID_SPEAKER_CONFIG if the input samples are obtained from the Dolby ED2 decoder and 
 *                                          target speaker configuration is set to DLB_PADMX_TARGET_SPEAKER_CONFIG_9_1_6
 * - DLB_PADMX_STATUS_WARNING:              if component encountered a warning
 * - DLB_PADMX_STATUS_OK:                   otherwise
 */
int
dlb_padmx_process
    (dlb_padmx_state        *h_state                /**< [in] Pointer to state handle */
    ,int                    *p_process_ready        /**< [out] Flag indicating that the process can be called again */
    ,unsigned int           *p_samples_available    /**< [out] Determines how many samples are available to be extracted */
);

/**
 * @brief Flush renderer instance
 * 
 * This function needs to be called before closing renderer instance to extract any internally buffered data.
 *
 *  dlb_padmx_flush() can issue the following warnings:
 * - DLB_PADMX_WARN_OUTPUT_FULL
 * - DLB_PADMX_WARN_PRESENTATION_UNAVAILABLE
 * - DLB_PADMX_WARN_MISSING_METADATA
 * 
 * @return dlb_padmx_status
 * - DLB_PADMX_ERROR_NULL_POINTER: if any of the arguments is NULL
 * - DLB_PADMX_ERROR_FLUSHED:      if the component has already been flushed
 * - DLB_PADMX_ERROR_INVALID_SPEAKER_CONFIG if the input samples are derieved from the Dolby ED2 decoder and 
 *                                          target speaker configuration is set to DLB_PADMX_TARGET_SPEAKER_CONFIG_9_1_6
 * - DLB_PADMX_STATUS_WARNING:     if component encountered a warning
 * - DLB_PADMX_STATUS_OK:          otherwise
 */
int
dlb_padmx_flush
    (dlb_padmx_state        *h_state                /**< [in] Pointer to state handle */
    ,int                    *p_is_flushed           /**< [out] Flag indicating that the renderer instance flushed */
    ,unsigned int           *p_samples_available    /**< [out] Determines how many samples are available to be extracted */
);

/**
 * @brief Closes component instance
 *
 * NOTE: dlb_padmx_open() must be called just once before calling this function.
 *
 * @return dlb_padmx_status
 * - DLB_PADMX_ERROR_NULL_POINTER: if h_state is NULL
 * - DLB_PADMX_STATUS_OK:          otherwise
 */
int
dlb_padmx_close
    (dlb_padmx_state        *h_state
    );

/** }@ */

#ifdef __cplusplus
}
#endif

#endif // DLB_PADMX_H

