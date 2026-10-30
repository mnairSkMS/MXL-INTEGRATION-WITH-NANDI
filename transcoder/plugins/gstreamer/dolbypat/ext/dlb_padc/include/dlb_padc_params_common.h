/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                Copyright (C) 2020 by Dolby Laboratories,
 *                Copyright (C) 2020 by Dolby International AB.
 *                            All rights reserved.
 ******************************************************************************/

/**
 *  @file  dlb_padc_params_common.h
 *  @brief File containing common enum definitions and data structures
 */

#ifndef DLB_PADC_PARAMS_COMMON_H
#define DLB_PADC_PARAMS_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup OPAQUE Opaque types
 * @{
 */

/**
 * @typedef dlb_padc_state
 * @brief Opaque Dolby PADC state
 */
typedef struct dlb_padc_state_s dlb_padc_state;

/** @} */


/**
 * @defgroup ERROR_CODES Error and Warning Code Values
 * @{
 */

/**
 * @typedef dlb_padc_status
 * @brief Retrun codes of operations' status
 */
typedef enum
{
    DLB_PADC_STATUS_OK                          = 0,            /**< Function returned successfully */
    DLB_PADC_STATUS_WARNING                     = 1,            /**< Function returned warning(s) */

    DLB_PADC_ERROR_NULL_POINTER                 = 101,          /**< Null pointer was passed as input to function */
    DLB_PADC_ERROR_INVALID_CONFIGURATION        = 102,          /**< Invalid configuration of component */
    DLB_PADC_ERROR_OPEN                         = 103,          /**< Error during component opening */
    DLB_PADC_ERROR_CLOSE                        = 104,          /**< Error during component closing */
    DLB_PADC_ERROR_INPUT_TOO_LARGE              = 105,          /**< Input data chunk bigger than max allowed */
    DLB_PADC_ERROR_OUTPUT_TOO_LARGE             = 106,          /**< Number of requested output samples bigger than max allowed */
    DLB_PADC_ERROR_CORRUPT_STATE                = 107,          /**< Error when state memory is violated */
    DLB_PADC_ERROR_FLUSHED                      = 108,          /**< Component is being or has already been flushed */
    DLB_PADC_ERROR_MEMORY_ALLOCATION            = 109,          /**< Error when memory for component cannot be allocated */
    DLB_PADC_ERROR_TOO_MANY_ELEMENTARY_STREAMS  = 110,          /**< DEPRECATED Error when there are passed to many streams into dlb_padc_add_bytes() */
    DLB_PADC_ERROR_ELEMENTARY_STREAM_COUNT      = 110,          /**< Error when a wrong number of bitstreams is passed to dlb_padc_add_bytes() */
    DLB_PADC_ERROR_PROCESS_METADATA             = 111,          /**< Error during processing metadata */
    DLB_PADC_ERROR_INIT_TYPE                    = 112,          /**< Type is not supported by component */
}
dlb_padc_status;

/**
 * @typedef dlb_padc_warning_codes
 * @brief Warning codes of operation status
 */
typedef enum
{
    DLB_PADC_WARN_INPUT_FULL                    = 1001,         /**< Warning indicating that more bytes are passed than space is available inside the internal buffer */
    DLB_PADC_WARN_OUTPUT_FULL                   = 1002,         /**< Output queue is full, use dlb_padc_output_get() first */
    DLB_PADC_WARN_UNABLE_TO_DECODE              = 1003,         /**< Warning indicating that a frame cannot be decoded */
    DLB_PADC_WARN_PROGRAM_OUT_OF_RANGE          = 1004,         /**< Warning indicating that the selected program for decoding is out of bounds */
    DLB_PADC_WARN_FRAME_INVALID                 = 1005,         /**< Warning indicating that the frame is corrupted */
    DLB_PADC_WARN_UNSUPPORTED_SUBSTREAM         = 1006,         /**< Warning indicating that an unsupported bitstream has been processed for a given component configuration */
} dlb_padc_warning_codes;

/** @} */

/**
 * @defgroup STD_PARAM_ENUM Parameter Value Enumerations
 * @{
 */

/**
 * @typedef dlb_padc_program
 * @brief enum used to select one program (or all) for decoding
 */
typedef enum
{
    DLB_PADC_PROG_ALL = 0,  /**< Decode all available programs */

    DLB_PADC_PROG_1ST = 1,  /**< Decode first program */
    DLB_PADC_PROG_2ND = 2,  /**< Decode second program */
    DLB_PADC_PROG_3RD = 3,  /**< Decode third program */
    DLB_PADC_PROG_4TH = 4,  /**< Decode fourth program */
    DLB_PADC_PROG_5TH = 5,  /**< Decode fifth program */
    DLB_PADC_PROG_6TH = 6,  /**< Decode sixth program */
    DLB_PADC_PROG_7TH = 7,  /**< Decode seventh program */
    DLB_PADC_PROG_8TH = 8,  /**< Decode eighth program */
    DLB_PADC_PROG_DEFAULT = DLB_PADC_PROG_1ST, /**< First program decode as default */
}
dlb_padc_program;

/**
 * @typedef dlb_padc_channel_mode
 * @brief enum used to select one of two available output channel mode for decoding
 */
typedef enum
{
    DLB_PADC_CHANNEL_MODE_LEGACY        = 0,  /**< Select the legacy output channel order, works only when DLB_PADC_PROG_ALL is selected */
    DLB_PADC_CHANNEL_MODE_PROGRAM_WISE  = 1,  /**< Select the program-wise output channel order */
    DLB_PADC_CHANNEL_MODE_DEFAULT = DLB_PADC_CHANNEL_MODE_LEGACY, /**< Legacy output channel order as default */
}
dlb_padc_channel_mode;

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* DLB_PADC_PARAMS_COMMON_H */
