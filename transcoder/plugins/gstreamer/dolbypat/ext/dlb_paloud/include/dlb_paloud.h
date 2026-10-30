/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                  Copyright (C) 2020-2023 by Dolby Laboratories.
 *                            All rights reserved.
 ******************************************************************************/

/**
 *  @file dlb_paloud.h
 *
 *  @brief Dolby pro audio loudness leveler component (PALOUD).
 *
 *  Top level functions for the library.
 */

#ifndef DLB_PALOUD_H
#define DLB_PALOUD_H

#include <stdint.h>

#include "dlb_paloud_common.h"
#include "dlb_paloud_params_init.h"
#include "dlb_paloud_params_runtime.h"

#include "dlb_paio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup FUNCTIONS API functions
 * @{
 */

/**
 * @brief Retrieve the version of the library
 *
 * @return dlb_paloud_status
 * - DLB_PALOUD_ERROR_NULL_POINTER:   if p_version is NULL
 * - DLB_PALOUD_STATUS_OK:            otherwise
 */
int
dlb_paloud_version_get
    (dlb_paloud_version           *p_version          /**< [out] Version of the library. */
    );


/**
 * @brief Configure initialization-time data structures to ensure proper operation
 *
 * This function populates each member of the output structure with a default value.
 * It is provided to support backward compatibility for future library versions.
 *
 * @return dlb_paloud_status
 * - DLB_PALOUD_ERROR_NULL_POINTER:         if p_init_config is NULL
 * - DLB_PALOUD_STATUS_OK:                  otherwise
 */
int
dlb_paloud_setup
    (dlb_paloud_init_config     *p_init_config      /**< [out] Initial library configuration structure */
    );


/**
 * @brief Get the requirements of the library
 *
 * This function returns sizes of input and output buffers and the
 * latency of the loudness leveler component.
 *
 * NOTE: dlb_paloud_setup() must be called just once before calling this function.
 *
 * NOTE: This function must be called just once before calling dlb_paloud_open().
 *
 * @return dlb_paloud_status
 * - DLB_PALOUD_ERROR_NULL_POINTER:       if p_init_config or p_requirements is NULL
 * - DLB_PALOUD_ERROR_NO_SETUP:           if dlb_paloud_query() is called before calling dlb_paloud_setup()
 * - DLB_PALOUD_ERROR_INIT_<PARAM>:       if any p_init_config parameter is out of range
 * - DLB_PALOUD_STATUS_OK:                otherwise
 *
 * NOTE: If any error occurs, output parameters and p_requirements are not
 *       modified. We suggest that an integrator clears these parameters before
 *       calling dlb_paloud_query().
 */
int dlb_paloud_query
    (dlb_paloud_init_config   *p_init_config      /**< [in]  PALOUD initialization configuration. */
    ,dlb_paloud_requirements  *p_requirements     /**< [out] Requirements of PALOUD. */
    );

/**
 * @brief Open an instance of the library
 *
 * NOTE: This function MUST be called just once before calling any of the proceeding functions.
 *
 * @return dlb_paloud_status
 * - DLB_PALOUD_ERROR_NULL_POINTER:       if ph_state or p_init_config is NULL
 * - DLB_PALOUD_ERROR_NO_QUERY:           if dlb_paloud_open() is called before calling dlb_paloud_query()
 * - DLB_PALOUD_ERROR_INIT:               if any p_init_config parameter is out of range
 * - DLB_PALOUD_ERROR_INTERNAL:           if creation or configuration of the internal processing graph failed
 * - DLB_PALOUD_STATUS_OK:                otherwise
 */
int
dlb_paloud_open
    (dlb_paloud_state         **ph_state          /**< [out] Library state handle */
    ,dlb_paloud_init_config    *p_init_config     /**< [in]  Initial library configuration, which determines the
                                                   *         capabilities and limitations of the configured library
                                                   *         instance.
                                                   */
    );


/**
 * @brief Close an instance of the library
 *
 * NOTE: dlb_paloud_open() must be called just once before calling this function.
 *
 * @return dlb_paec_status
 * - DLB_PALOUD_ERROR_NULL_POINTER:      if h_state is NULL
 * - DLB_PALOUD_ERROR_MEMORY_ALLOCATION: if any internal memory allocation fails
 * - DLB_PALOUD_STATUS_OK:               otherwise
 */
int
dlb_paloud_close
    (dlb_paloud_state     *h_state            /**< [in]  PALOUD state handle */
    );


/**
 * @brief Configure the library run-time data structures
 *
 * This function can be called (if necessary) prior to each call to dlb_paloud_process().
 *
 * NOTE: dlb_paloud_open() must be called just once before calling this function.
 *
 * @return dlb_paloud_status
 * - DLB_PALOUD_ERROR_NULL_POINTER:       if h_state or p_runtime_config is NULL
 * - DLB_PALOUD_ERROR_CORRUPT_STATE:      if h_state does not point to a valid dlb_paloud_state
 * - DLB_PALOUD_ERROR_RUNTIME:            if any p_runtime_config parameter is out of range
 * - DLB_PALOUD_STATUS_OK:                otherwise
 *
 * Note that a dlb_paloud_status return code other than DLB_PALOUD_STATUS_OK indicates
 * that the entire set of configuration parameters has been rejected due to a critical
 * configuration value being out of range.
 */
int
dlb_paloud_runtime_config_set
    (dlb_paloud_state             *h_state            /**< [in]  Library state handle */
    ,dlb_paloud_runtime_config    *p_runtime_config   /**< [in]  Run-time configuration */
    );


/**
 * @brief Get current library run-time data structures
 *
 * NOTE: dlb_paloud_open() must be called just once before calling this function.
 *
 * @return dlb_paloud_status
 * - DLB_PALOUD_ERROR_NULL_POINTER:   if h_state or p_runtime_config is NULL
 * - DLB_PALOUD_ERROR_CORRUPT_STATE:  if h_state does not point to a valid dlb_paloud_state
 * - DLB_PALOUD_STATUS_OK:            otherwise
 */
int
dlb_paloud_runtime_config_get
    (dlb_paloud_state             *h_state            /**< [in]  Library state handle */
    ,dlb_paloud_runtime_config    *p_runtime_config   /**< [out] Run-time configuration */
    );


/**
 * @brief Get the current library default values for run-time parameters
 * @return dlb_paloud_status
 * - DLB_PALOUD_ERROR_NULL_POINTER:   if h_state or p_runtime_config is NULL
 * - DLB_PALOUD_ERROR_CORRUPT_STATE:  if h_state does not point to a valid dlb_paloud_state
 * - DLB_PALOUD_STATUS_OK:            otherwise
 */
int
dlb_paloud_runtime_defaults_get
    (dlb_paloud_state             *h_state            /**< [in]  Library state handle */
    ,dlb_paloud_runtime_config    *p_runtime_config   /**< [out] Run-time configuration */
    );


/**
 * @brief Add samples into the library
 *
 * Add a number of samples (p_input->num_samples) to the library.
 *
 * NOTE: dlb_paloud_open() must be called just once before calling this function.
 *
 * @return dlb_paloud_status
 * - DLB_PALOUD_ERROR_NULL_POINTER:             if h_state, p_input, b_process_ready, or space_available is NULL
 * - DLB_PALOUD_ERROR_NULL_INPUT_POINTER:       if p_input->p_pcm is NULL
 * - DLB_PALOUD_ERROR_CORRUPT_STATE:            if h_state does not point to a valid dlb_paloud_state
 * - DLB_PALOUD_ERROR_INTERNAL:                 if internal processing failed in any way that cannot be corrected
 *                                              by the library user
 * - DLB_PALOUD_ERROR_RUNTIME_INPUT_SAMPLES     if p_input->num_samples is invalid
 * - DLB_PALOUD_ERROR_RUNTIME_INPUT_FULL        if the library cannot accept any more samples
 * - DLB_PALOUD_STATUS_OK:                      otherwise
 */
int
dlb_paloud_add_samples
    (dlb_paloud_state       *h_state            /**< [in]  Library state handle */
    ,dlb_paio_pcm_block     *p_input            /**< [in]  Input PCM block */
    ,int                    *b_process_ready    /**< [out] Signals that dlb_paloud_process() can be called
                                                 *         -  Enough data has been added */
    ,unsigned int           *space_available    /**< [out] The number of additional samples that can be added.
                                                           If there is not enough space (space_available == 0), then dlb_paloud_process() must be called
                                                           before adding additional samples. */
    );


/**
 * @brief Process samples with the library
 *
 * Process a single PCM block equal to the fixed block size.
 *
 * NOTE: dlb_paloud_open() must be called just once before calling this function.
 *
 * @return dlb_paloud_status
 * - DLB_PALOUD_ERROR_NULL_POINTER:         if h_state, b_process_ready, or samples_available is NULL
 * - DLB_PALOUD_ERROR_CORRUPT_STATE:        if h_state does not point to a valid dlb_paloud_state
 * - DLB_PALOUD_ERROR_INTERNAL:             if internal processing failed in any way that cannot be corrected
 *                                          by the library user
 * - DLB_PALOUD_ERROR_RUNTIME_INPUT_EMPTY   if there are not enough samples to process
 * - DLB_PALOUD_ERROR_RUNTIME_OUTPUT_FULL   if there is no space to store the processed samples
 * - DLB_PALOUD_STATUS_OK:                  otherwise
 */
int
dlb_paloud_process
    (dlb_paloud_state       *h_state            /**< [in]  Library state handle */
    ,int                    *b_process_ready    /**< [out] Signals that dlb_paloud_process() can be called again
                                                 *         -  Enough data has been added */
    ,unsigned int           *samples_available  /**< [out] The number of samples available to extract via
                                                 *         dlb_paloud_get_samples()
                                                 */
    );

/**
 * @brief Flush remaining samples through the library
 *
 * Flush the remaining PCM blocks.
 * Flushing is complete when p_flushed returns with a value of 1.
 *
 * NOTE: dlb_paloud_open() must be called just once before calling this function.
 *
 * @return dlb_paloud_status
 * - DLB_PALOUD_ERROR_NULL_POINTER:         if h_state, p_flushed, or samples_available is NULL
 * - DLB_PALOUD_ERROR_CORRUPT_STATE:        if h_state does not point to a valid dlb_paloud_state
 * - DLB_PALOUD_ERROR_INTERNAL:             if internal processing failed in any way that cannot be corrected
 *                                          by the library user
 * - DLB_PALOUD_ERROR_RUNTIME_INPUT_EMPTY   if there are not enough samples to process
 * - DLB_PALOUD_ERROR_RUNTIME_OUTPUT_FULL   if there is no space to store the processed samples
 * - DLB_PALOUD_STATUS_OK:                  otherwise
 */
int
dlb_paloud_flush
    (dlb_paloud_state       *h_state            /**< [in]  Library state handle */
    ,int                    *p_flushed          /**< [out] Flag indicating that PALOUD has been flushed completely. */
    ,unsigned int           *samples_available  /**< [out] The number of samples available to extract via dlb_paloud_get_samples() */
    );


/**
 * @brief Get samples from the library
 *
 * Retrieve a number of samples (num_samples) from the library.
 *
 * NOTE: dlb_paloud_open() must be called just once before calling this function.
 *
 * @return dlb_paloud_status
 * - DLB_PALOUD_ERROR_NULL_POINTER:             if h_state, p_output, or samples_available is NULL
 * - DLB_PALOUD_ERROR_NULL_OUTPUT_POINTER:      if p_output->p_pcm is NULL
 * - DLB_PALOUD_ERROR_CORRUPT_STATE:            if h_state does not point to a valid dlb_paloud_state
 * - DLB_PALOUD_ERROR_INTERNAL:                 if internal processing failed in any way that cannot be corrected
 *                                              by the library user
 * - DLB_PALOUD_ERROR_RUNTIME_OUTPUT_SAMPLES    if num_samples is invalid
 * - DLB_PALOUD_ERROR_RUNTIME_OUTPUT_EMPTY      if the library contains less samples than were requested
 * - DLB_PALOUD_STATUS_OK:                      otherwise
 */
int
dlb_paloud_get_samples
    (dlb_paloud_state       *h_state            /**< [in]  Library state handle */
    ,unsigned int            num_samples        /**< [in]  The number of samples to get from the library */
    ,dlb_paio_pcm_block     *p_output           /**< [out] Output PCM block */
    ,unsigned int           *samples_available  /**< [out] The number of remaining samples available to extract */
    );

/**
 * @brief Get warnings from last function call
 */
dlb_warnings
dlb_paloud_warnings_get
    (dlb_paloud_state         *h_state            /**< [in]  Library state handle */
    );


/**
 * @brief Convert a warning code to a text description
 *
 * Returns a text description of an input warning code.
 *
 * @return the text description (as const char *)
 * - "Invalid status code" if warning_code is not a valid warning code
 */
const char *
dlb_paloud_warning_description_get
    (int warning_code                     /**< [in] PALOUD warning code @ref dlb_paloud_warning_codes */
    );


/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PALOUD_H */
