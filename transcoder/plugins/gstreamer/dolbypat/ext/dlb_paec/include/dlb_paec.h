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
 *  @file dlb_paec.h
 *
 *  @brief Dolby pro audio encoder component (PAEC).
 *
 *  Top level functions for PAEC.
 */

#ifndef DLB_PAEC_H
#define DLB_PAEC_H


#include "dlb_paec_params_common.h"
#include "dlb_paec_params_init.h"
#include "dlb_paec_params_runtime.h"

#include "dlb_paio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup FUNCTIONS API functions
 * @{
 */

/**
 * @brief Retrieve the version of PAEC
 *
 * @return dlb_paec_status
 * - DLB_PAEC_ERROR_NULL_POINTER:   if p_version is NULL
 * - DLB_PAEC_STATUS_OK:            otherwise
 */
int
dlb_paec_version_get
    (dlb_paec_version           *p_version          /**< [out] Version of PAEC. */
    );


/**
 * @brief Configure initialization-time data structures to ensure proper operation
 *
 * This function populates each member of the output structure with a default value.
 * It is provided to support backward compatibility of future PAEC versions.
 *
 * NOTE: This function must be called just once before dlb_paec_query().
 *
 * @return dlb_paec_status
 * - DLB_PAEC_ERROR_NULL_POINTER:   if p_init_config is NULL
 * - DLB_PAEC_STATUS_OK:            otherwise
 */
int
dlb_paec_setup
    (dlb_paec_init_config       *p_init_config      /**< [out] Initial PAEC configuration structure */
    ,dlb_paec_type               type               /**< [in] Type of encoder */
    );


/**
 * @brief Get the requirements of PAEC
 *
 * This function returns sizes of input and output buffers and the
 * latency of the configured encoder.
 *
 * NOTE: dlb_paec_setup() must be called just once before calling this function.
 *
 * NOTE: This function must be called just once before calling dlb_paec_open().
 *
 * @return dlb_paec_status
 * - DLB_PAEC_ERROR_NULL_POINTER:       if p_init_config or p_requirements is NULL
 * - DLB_PAEC_ERROR_NO_SETUP:           if dlb_paec_query() is called before calling dlb_paec_setup()
 * - DLB_PAEC_ERROR_INIT_<PARAM>:       if any p_init_config parameter is out of range
 * - DLB_PAEC_STATUS_OK:                otherwise
 *
 * NOTE: If any error occurs, output parameters and p_requirements are not
 *       modified. We suggest that an integrator clears these parameters before
 *       calling dlb_paec_query().
 */
int dlb_paec_query
    (dlb_paec_init_config   *p_init_config      /**< [in]  PAEC initialization configuration */
    ,dlb_paec_requirements  *p_requirements     /**< [out] Requirements of PAEC */
    );


/**
 * @brief Open an instance of PAEC
 *
 * NOTE: dlb_paec_query() must be called just once before calling this function.
 *
 * NOTE: This function must be called just once before calling any of the proceeding functions.
 *
 * @return dlb_paec_status
 * - DLB_PAEC_ERROR_NULL_POINTER:       if ph_state or p_init_config is NULL
 * - DLB_PAEC_ERROR_NO_QUERY:           if dlb_paec_open() is called before calling dlb_paec_query()
 * - DLB_PAEC_ERROR_INIT_<PARAM>:       if any p_init_config parameter is out of range
 * - DLB_PAEC_ERROR_INTERNAL:           if creation or configuration of the internal processing graph failed
 * - DLB_PAEC_STATUS_OK:                otherwise
 */
int
dlb_paec_open
    (dlb_paec_state         **ph_state          /**< [out] PAEC state handle */
    ,dlb_paec_init_config    *p_init_config     /**< [in]  Initial PAEC configuration, which determines the
                                                 *         capabilities and limitations of the configured PAEC
                                                 *         instance.
                                                 */
    );


/**
 * @brief Populate default values into run-time data structures
 *
 * This function populates each member of the output structure with a default value.
 * It is provided to support backward compatibility of future PAEC versions.
 *
 * @return dlb_paec_status
 * - DLB_PAEC_ERROR_NULL_POINTER:   if h_state or p_runtime_config is NULL
 * - DLB_PAEC_ERROR_CORRUPT_STATE:  if h_state does not point to a valid dlb_paec_state
 * - DLB_PAEC_STATUS_OK:            otherwise
 */
int
dlb_paec_runtime_defaults_get
    (dlb_paec_state             *h_state            /**< [in]  PAEC state handle */
    ,dlb_paec_runtime_config    *p_runtime_config   /**< [out] Run-time configuration */
    );


/**
 * @brief Configure the PAEC run-time data structures
 *
 * This function can be called (if necessary) prior to each call to dlb_paec_process().
 *
 * NOTE: dlb_paec_open() must be called just once before calling this function.
 *
 * @return dlb_paec_status
 * - DLB_PAEC_ERROR_NULL_POINTER:       if h_state or p_runtime_config is NULL
 * - DLB_PAEC_ERROR_CORRUPT_STATE:      if h_state does not point to a valid dlb_paec_state
 * - DLB_PAEC_ERROR_<CODEC>_<PARAM>:    if any p_runtime_config parameter is out of range
 * - DLB_PAEC_STATUS_OK:                otherwise
 *
 * Note that a dlb_paec_status return code other than DLB_PAEC_STATUS_OK indicates
 * that the entire set of configuration parameters has been rejected due to a critical
 * configuration value being out of range.
 */
int
dlb_paec_runtime_config_set
    (dlb_paec_state             *h_state            /**< [in]  PAEC state handle */
    ,dlb_paec_runtime_config    *p_runtime_config   /**< [in]  Run-time configuration */
    );


/**
 * @brief Get current PAEC run-time data structures
 *
 * NOTE: dlb_paec_open() must be called just once before calling this function.
 *
 * @return dlb_paec_status
 * - DLB_PAEC_ERROR_NULL_POINTER:   if h_state or p_runtime_config is NULL
 * - DLB_PAEC_ERROR_CORRUPT_STATE:  if h_state does not point to a valid dlb_paec_state
 * - DLB_PAEC_STATUS_OK:            otherwise
 */
int
dlb_paec_runtime_config_get
    (dlb_paec_state             *h_state            /**< [in]  PAEC state handle */
    ,dlb_paec_runtime_config    *p_runtime_config   /**< [out] Run-time configuration */
    );

/**
 * @brief Used to manually insert I-frames into the AC-4 bitstream
 *
 * iframe_block_offset can be used to specify the exact time (as a sample offset value) of the PCM input,
 * at which the I-frame is expected to occur. The offset must be within a range of [0, samples_amount - 1] of the PCM input in dlb_paec_add_samples().
 *
 * NOTE: Forcing I-frame should be called just before dlb_paec_add_samples is called,
 * so iframe_block_offset is associated with the set of samples to be input via dlb_paec_add_samples.
 *
 * NOTE: Manual I-frame insertion is associated with samples arriving at the encoder input.
 * Such an I-frame is generated at the encoder output with a Dolby AC-4 encoder latency.
 *
 */
void dlb_paec_set_manual_iframe
    (dlb_paec_state             *h_state                /**< [in] PAEC state handle */
    ,unsigned int               iframe_block_offset     /**< [in] Block offset */
    );


/**
 * @brief Add PCM samples to the component
 *
 * Fill the component internal memory with input data.
 * When sufficient data is collected, @ref p_process_ready flag is set
 * to indicate that @ref dlb_paec_process can be called.
 *
 * @return dlb_paec_status
 * - DLB_PAEC_ERROR_NULL_POINTER:               if h_state or p_input or p_process_ready or p_space_available is NULL
 * - DLB_PAEC_ERROR_NULL_INPUT_POINTER:         if p_input->p_pcm is NULL
 * - DLB_PAEC_ERROR_CORRUPT_STATE:              if h_state does not point to a valid dlb_paec_state
 * - DLB_PAEC_ERROR_RUNTIME_INPUT_SAMPLES:      if p_input->num_samples is less than DLB_PAIO_PCM_BLOCK_SIZE_MIN or
 *                                              greater than DLB_PAIO_PCM_BLOCK_SIZE_MAX
 * - DLB_PAEC_ERROR_RUNTIME_MIF_SEGMENT_ERROR:  if the metadata interchange format segment cannot be copied internally
 * - DLB_PAEC_ERROR_RUNTIME_INPUT_FULL:         if the library cannot accept any more samples
 * - DLB_PAEC_STATUS_OK:                        otherwise
 */
int
dlb_paec_add_samples
    (dlb_paec_state             *h_state                /**< [in] PAEC state handle */
    ,dlb_paio_pcm_block         *p_input                /**< [in] Input PCM */
    ,int                        *p_process_ready        /**< [out] A flag indicating that @ref dlb_paec_process can be called. */
    ,unsigned int               *p_space_available      /**< [out] Determines how many samples can still be added. */
    );


/**
 * @brief Process PCM data
 *
 * Process a single block of data.
 *
 * @return dlb_paec_status
 * - DLB_PAEC_ERROR_NULL_POINTER:               if h_state or p_process_ready or p_frame_available is NULL
 * - DLB_PAEC_ERROR_CORRUPT_STATE:              if h_state does not point to a valid dlb_paec_state
 * - DLB_PAEC_ERROR_RUNTIME_INPUT_EMPTY:        if there is not enough data for processing
 * - DLB_PAEC_ERROR_RUNTIME_OUTPUT_FULL:        if the library cannot hold any more output data
 * - DLB_PAEC_ERROR_INTERNAL:                   if a library internal processing error occurred
 * - DLB_PAEC_STATUS_OK:                        otherwise
 */
int
dlb_paec_process
    (dlb_paec_state             *h_state                        /**< [in] PAEC state handle */
    ,int                        *p_process_ready                /**< [out] A flag indicating that @ref dlb_paec_process can be called.
                                                                 *         If this flag is set when exiting @ref dlb_paec_process,
                                                                 *         then it indicates that there is enough data at the input so that
                                                                 *         @ref dlb_paec_process can be called again.
                                                                 */
    ,int                        *p_frame_available              /**< [out] A flag indicating that a frame is available and can be extracted.
                                                                 *         - 1 = A frame is available and can be extracted
                                                                 *         - 0 = No frame available
                                                                 */
    );


/**
 * @brief Retrieve a single frame of encoded data.
 *
 * @return dlb_paec_status
 * - DLB_PAEC_ERROR_NULL_POINTER:               if h_state or p_output is NULL
 * - DLB_PAEC_ERROR_NULL_OUTPUT_POINTER:        if p_output->p_bitstreams[0] is NULL
 * - DLB_PAEC_ERROR_CORRUPT_STATE:              if h_state does not point to a valid dlb_paec_state
 * - DLB_PAEC_ERROR_RUNTIME_OUTPUT_EMPTY:       if the library does not have enough data to output
 * - DLB_PAEC_STATUS_OK:                        otherwise
 */
int
dlb_paec_get_frame
    (dlb_paec_state             *h_state                /**< [in] PAEC state handle */
    ,dlb_paio_bitstream_block   *p_output               /**< [out] Output bitstream block */
    );


/**
 * @brief Flushes encoded blocks at the end of file or when input PCM has run out
 *
 * Flushing is complete when p_flushed returns 1.
 *
 * @return dlb_paec_status
 * - DLB_PAEC_ERROR_NULL_POINTER:           if h_state or p_flushed or p_frame_available is NULL
 * - DLB_PAEC_ERROR_CORRUPT_STATE:          if h_state does not point to a valid dlb_paec_state
 * - DLB_PAEC_STATUS_OK:                    otherwise
 */
int
dlb_paec_flush
    (dlb_paec_state             *h_state                /**< [in]  PAEC state handle */
    ,int                        *p_flushed              /**< [out] A flag indicating that PAEC has been flushed completely. */
    ,int                        *p_frame_available      /**< [out] A flag indicating that a frame is available and can be extracted.
                                                         *         - 1 = A frame is available and can be extracted
                                                         *         - 0 = No frame available */
    );

/**
 * @brief Get information about most recently encoded frame
 *
 * Frame information is available only for Dolby AC-4 codec
 *
 * Note: dlb_paec_get_frame_info must be called after dlb_paec_get_frame function is called.
 * If there is a zero-length Dolby AC-4 frame collected from the encoder, frame information returns NULL values.
 *
 * @return dlb_paec_status
 * - DLB_PAEC_ERROR_NULL_POINTER:   if h_state is NULL
 * - DLB_PAEC_ERROR_CORRUPT_STATE:  if h_state does not point to a valid dlb_paec_state
 * - DLB_PAEC_STATUS_OK:            otherwise
 */
int
dlb_paec_get_frame_info
    (dlb_paec_state             *h_state                /**< [in]  PAEC state handle */
    ,dlb_paec_frame_info        *p_frame_info           /**< [out] Description of the most recently encoded frame */
    );


/**
 * @brief Close an instance of PAEC
 *
 * NOTE: dlb_paec_open() must be called just once before calling this function.
 *
 * @return dlb_paec_status
 * - DLB_PAEC_ERROR_NULL_POINTER:   if h_state is NULL
 * - DLB_PAEC_ERROR_CORRUPT_STATE:  if h_state does not point to a valid dlb_paec_state
 * - DLB_PAEC_STATUS_OK:            otherwise
 */
int
dlb_paec_close
    (dlb_paec_state     *h_state            /**< [in]  PAEC state handle */
    );


/**
 * @brief Describe a warning code
 *
 * @return const char *
 * Description of a warning code
 */
const char *
dlb_paec_warning_description_get
    (int warning_code     /**< [in] Warning code */
    );

/**
 * @brief Get all warnings from most recent function call for PAEC
 *
 * @return dlb_warnings
 */

dlb_warnings
dlb_paec_warnings_get
    (dlb_paec_state         *h_state            /**< [in]  PAEC state handle */
    );

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PAEC_H */

