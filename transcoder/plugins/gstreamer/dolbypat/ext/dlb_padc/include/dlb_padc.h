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
 *  @file  dlb_padc.h
 *
 *  @brief Dolby pro audio decode component (PADC).
 *
 *  Top level functions for the PADC.
 */

#ifndef DLB_PADC_H
#define DLB_PADC_H

#include "dlb_padc_params_common.h"
#include "dlb_padc_params_init.h"
#include "dlb_padc_params_runtime.h"
#include "dlb_paio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup FUNCTIONS API Functions
 * @brief Decoder API functions.
 * @{
 */

/**
 * @brief Fills version structure with library's current version
 *
 * @return dlb_padc_status
 * - DLB_PADC_ERROR_NULL_POINTER: if p_version is NULL
 * - DLB_PADC_STATUS_OK:          otherwise
 */
int
dlb_padc_version_get
    (dlb_padc_version *p_version        /**< [out] Pointer to version structure */
    );

/**
 * @brief Fills component configuration for given component type
 *
 * NOTE: This function must be called just once before dlb_padc_query().
 *
 * @return dlb_padc_status
 * - DLB_PADC_ERROR_INIT_TYPE:     if given component type is not supproted
 * - DLB_PADC_ERROR_NULL_POINTER:  if p_config is NULL
 * - DLB_PADC_STATUS_OK:           otherwise
 */
int
dlb_padc_setup
    (dlb_padc_type         type         /**< [in]  Decoder type */
    ,dlb_padc_init_config *p_config     /**< [out] Pointer to configuration structure */
    );


/**
 * @brief Query memory requirements needed by library
 *
 * NOTE: dlb_padc_setup() must be called only once before calling this function.
 *
 * NOTE: This function must be called just once before calling dlb_padc_open().
 *
 * @return dlb_padc_status
 * - DLB_PADC_ERROR_NULL_POINTER:           if any of the arguments is NULL
 * - DLB_PADC_ERROR_INVALID_CONFIGURATION:  if provided initialization parameters are invalid
 * - DLB_PADC_STATUS_OK:                    otherwise
 */
int
dlb_padc_query
    (dlb_padc_init_config  *p_initcfg   /**< [in]  Pointer to configuration structure */
    ,dlb_padc_requirements *p_reqs      /**< [out] Pointer to memory requirements structure */
    );


/**
 * @brief Get warnings from the last library call with specific decoder state handle.
 *
 * @return struct dlb_warnings filled with warnings.
 */
dlb_warnings
dlb_padc_warnings_get
    (dlb_padc_state *h_state            /**< [in]  PADC state handle */
    );


/**
 * @brief Opens component instance
 *
 * NOTE: dlb_padc_query() must be called just once before calling this function.
 *
 * NOTE: This function must be called only once before calling dlb_padc_add_bytes(), dlb_padc_process(),
 *  dlb_padc_get_samples(), dlb_padc_flush() or dlb_padc_close() functions.
 *
 * NOTE: \p ph_state must point to a valid, zero-initialized pointer to dlb_padc_state.
 *
 * @return dlb_padc_status
 * - DLB_PADC_ERROR_NULL_POINTER:           if any of the arguments is NULL
 * - DLB_PADC_ERROR_INVALID_CONFIGURATION:  if provided initialization parameters are invalid
 * - DLB_PADC_ERROR_OPEN:                   if the memory provided by the user points to the already 
 *                                          opened decoder instance or error occurs during the opening
 * - DLB_PADC_STATUS_OK:                    otherwise
 */
int
dlb_padc_open
    (dlb_padc_state       **ph_state    /**< [out] Pointer to PADC state handle */
    ,dlb_padc_init_config  *p_initcfg   /**< [in]  Pointer to configuration structure  */
    );


/**
 * @brief Get current (or default) PADC run-time data structures
 *
 * @return dlb_padc_status
 * - DLB_PADC_ERROR_NULL_POINTER:   if any of the arguments is NULL
 * - DLB_PADC_STATUS_OK:            otherwise
 */
int
dlb_padc_runtime_config_get
    (dlb_padc_state             *h_state    /**< [in]  PADC state handle */
    ,dlb_padc_runtime_params    *p_config   /**< [out] Run-time configuration */
    );


/**
 * @brief Configure the PADC run-time data structures
 *
 * @return dlb_padc_status
 * - DLB_PADC_ERROR_NULL_POINTER:           if any of the arguments is NULL
 * - DLB_PADC_ERROR_INVALID_CONFIGURATION   if provided runtime parameters are invalid
 * - DLB_PADC_STATUS_OK:                    otherwise
 */
int
dlb_padc_runtime_config_set
    (dlb_padc_state             *h_state    /**< [in]  PADC state handle */
    ,dlb_padc_runtime_params    *p_config   /**< [in]  Run-time configuration */
    );


/**
 * @brief Add data to the decoding component
 *
 * Fills the component internal memory with input data from the SMPTE337 
 * unwrapped bitstream. When enough data is collected, sets the flag in 
 * order to signal that the data can be processed. The p_space_available 
 * parameter needs to point to the first element of an array of size equal 
 * to p_input.num_bitstreams. It indicates how many bytes of bitstreams 
 * can still be pushed into the component before processing this data 
 * with the dlb_padc_process() function.
 * 
 * dlb_padc_add_bytes() can issue the following warnings:
 * - DLB_PADC_WARN_INPUT_FULL:                      if more bytes are passed than space is available (@ref p_space_available)
 *                                                  in the internal buffer. No bytes has been added.
 * 
 * @return dlb_padc_status
 * - DLB_PADC_ERROR_NULL_POINTER:                   if any of the arguments is NULL
 * - DLB_PADC_ERROR_ELEMENTARY_STREAM_COUNT:        if there are too few or too many elementary streams
 * - DLB_PADC_ERROR_INPUT_TOO_LARGE:                if input data contains too many bytes of data to add
 * - DLB_PADC_ERROR_FLUSHED:                        if the component is being or was flushed
 * - DLB_PADC_STATUS_WARNING:                       if warning(s) occured
 * - DLB_PADC_STATUS_OK:                            otherwise
 */
int dlb_padc_add_bytes
    (dlb_padc_state                 *h_state            /**< [in] PADC state handle */
    ,dlb_paio_bitstream_block       *p_input            /**< [in] Pointer to the input bitstream structure */
    ,int                            *p_process_ready    /**< [out] Flag indicating that dlb_padc_process() can be called */
    ,unsigned int                   *p_space_available  /**< [out] Pointer to the first element of an array. 
                                                         *         Determines how many bytes of any bitstream can still be added. */
    );


/**
 * @brief Process bitstream with the PADC
 *
 * While processing the input data pushed into the component, the function processes a single frame
 * of the input bitstream, and the p_process_ready flag indicates whether another call can be made.
 * The value of p_samples_available indicates how many samples are available to get
 * with the dlb_padc_get_samples() function.
 *
 * dlb_padc_process() can issue the following warnings:
 * - DLB_PADC_WARN_FRAME_INVALID:         if the decoded frame is corrupted
 * - DLB_PADC_WARN_UNABLE_TO_DECODE:      if the frame cannot be decoded
 * - DLB_PADC_WARN_UNSUPPORTED_SUBSTREAM  if an unsupported bitstream has been processed for given component configuration
 * - DLB_PADC_WARN_OUTPUT_FULL:           if the internal buffer has been filled, use dlb_padc_get_samples()
 *
 * @return dlb_padc_status
 * - DLB_PADC_ERROR_NULL_POINTER:         if any of the arguments is NULL
 * - DLB_PADC_ERROR_PROCESS_METADATA:     if the decoder cannot extract metadata
 * - DLB_PADC_ERROR_FLUSHED:              if the component is being or was flushed
 * - DLB_PADC_STATUS_WARNING:             if warning(s) occured
 * - DLB_PADC_STATUS_OK:                  if processing was successful
 */
int dlb_padc_process
    (dlb_padc_state     *h_state                /**< [in] PADC state handle */
    ,int                *p_process_ready        /**< [out] Flag indicating that dlb_padc_process() can be called again */
    ,unsigned int       *p_samples_available    /**< [out] Amount of samples that can be extracted using dlb_padc_get_samples() */
    );


/**
 * @brief Gets the output samples and metadata
 *
 * The function fills the output structure with the decoded samples and metadata.
 * The function indicates how many samples are still accessible by setting the p_samples_available value.
 *
 * @return dlb_padc_status
 * - DLB_PADC_ERROR_NULL_POINTER:   if h_state, p_output, p_output->p_md, p_output->p_pcm or p_samples_available is NULL
 * - DLB_PADC_STATUS_OK:            otherwise
 */
int dlb_padc_get_samples
    (dlb_padc_state     *h_state                /**< [in] PADC state handle */
    ,unsigned int        num_samples            /**< [in] Amount of samples to be extracted */
    ,dlb_paio_pcm_block *p_output               /**< [out] Pointer to the output PCM data strcutrue */
    ,unsigned int       *p_samples_available    /**< [out] Amount of samples that remain to be extracted */
    );


/**
 * @brief Flush decoder instance
 * 
 * To extract any internally buffered data (optional), the function needs to be called before closing the decoder instance.
 *
 * dlb_padc_flush() can issue the follwoing warnings:
 * - DLB_PADC_WARN_UNABLE_TO_DECODE:      if any of the buffered input data is corrupted
 * - DLB_PADC_WARN_FRAME_INVALID:         if the decoded frame is corrupted
 * - DLB_PADC_WARN_UNSUPPORTED_SUBSTREAM  if an unsupported bitstream has been processed for given component configuration
 * - DLB_PADC_WARN_OUTPUT_FULL:           if the internal buffer has been filled, use dlb_padc_get_samples()
 * 
 * @return dlb_padc_status
 * - DLB_PADC_ERROR_NULL_POINTER:         if any of the arguments is NULL
 * - DLB_PADC_ERROR_FLUSHED:              if the decoding instance has been flushed already
 * - DLB_PADC_STATUS_WARNING:             if any warning(s) occured
 * - DLB_PADC_STATUS_OK:                  otherwise
 */
int dlb_padc_flush
    (dlb_padc_state     *h_state                /**< [in] PADC state handle */
    ,int                *p_is_flushed           /**< [out] Flag indicating that PADC component has been flushed */
    ,unsigned int       *p_samples_available    /**< [out] Amount of samples that may be extracted using dlb_padc_get_samples() */
    );


/**
 * @brief Closes component instance
 *
 * NOTE: dlb_padc_open() must be called just once before calling this function.
 *
 * @return dlb_padc_status
 * - DLB_PADC_ERROR_NULL_POINTER:   if h_state is NULL
 * - DLB_PADC_STATUS_OK:            otherwise
 */
int
dlb_padc_close
    (dlb_padc_state *h_state            /**< [in] PADC state handle */
    );

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* DLB_PADC_H */
