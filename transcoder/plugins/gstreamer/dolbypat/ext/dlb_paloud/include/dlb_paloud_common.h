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
 *  @file dlb_paloud_common.h
 *
 *  @brief Dolby pro audio loudness leveler component common parameters.
 *
 *  Common parameters for the library.
 */

#ifndef DLB_PALOUD_COMMON_H
#define DLB_PALOUD_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 *@defgroup DATA_STRUCT_HIGH_LEVEL Parameter data structures (high level)
 * @{
 */

/**
 * @brief Opaque state
 */
typedef struct dlb_paloud_state_s dlb_paloud_state;


/**
 * @brief Four part version number,
 * read as <v_major.v_minor.v_func.v_build>
 */
typedef struct
{
    unsigned int     v_major;        /**< Major release  */
    unsigned int     v_minor;        /**< Minor release */
    unsigned int     v_func;         /**< Functional release */
    unsigned int     v_build;        /**< Build number */
} dlb_paloud_version;

/** @} */

/*********************************************/
/*** Error and Warning Code Values ***********/
/*********************************************/

/**
 * @defgroup ERROR_CODES Error and warning code values
 * @{
 */

/**
 * @brief Status return codes for the library
 */
typedef enum dlb_paloud_status_t
{
     DLB_PALOUD_STATUS_OK                       =     0  /**< Function returned successfully. */
    ,DLB_PALOUD_STATUS_WARNING                  =     1  /**< Function returned a warning. */
    ,DLB_PALOUD_ERROR_NULL_POINTER              =   101  /**< A NULL pointer was passed as input. */
    ,DLB_PALOUD_ERROR_NULL_INPUT_POINTER        =   102  /**< A NULL data pointer was passed as a required input parameter. */
    ,DLB_PALOUD_ERROR_NULL_OUTPUT_POINTER       =   103  /**< A NULL data pointer was passed as a required output parameter. */
    ,DLB_PALOUD_ERROR_NO_SETUP                  =   104  /**< dlb_paloud_setup() was not called prior to dlb_paloud_query(). */
    ,DLB_PALOUD_ERROR_NO_QUERY                  =   105  /**< dlb_paloud_query() was not called prior to dlb_paloud_open(). */
    ,DLB_PALOUD_ERROR_CORRUPT_STATE             =   106  /**< Internal state does not point to a valid state. */
    ,DLB_PALOUD_ERROR_MEMORY_ALLOCATION         =   107  /**< Internal memory allocation failure. */
    ,DLB_PALOUD_ERROR_INIT_CHAN_CFG             =   108  /**< The \<channel_cfg\> in the initialization structure is out
                                                          *   of its valid range.
                                                          */

    ,DLB_PALOUD_ERROR_RUNTIME                   =  1000  /**< Error in a dlb_paloud_runtime_config parameter. */
    ,DLB_PALOUD_ERROR_RUNTIME_INPUT_SAMPLES     =  1001  /**< Invalid p_input->num_samples passed to dlb_paloud_add_samples(). */
    ,DLB_PALOUD_ERROR_RUNTIME_OUTPUT_SAMPLES    =  1002  /**< Invalid num_samples passed to dlb_paloud_get_samples(). */
    ,DLB_PALOUD_ERROR_RUNTIME_INPUT_FULL        =  1003  /**< The input is full. Do not call dlb_paloud_add_samples() again
                                                          *   until you have called dlb_paloud_process().
                                                          */
    ,DLB_PALOUD_ERROR_RUNTIME_OUTPUT_FULL       =  1004  /**< The output is full. Do not call dlb_paloud_process() again
                                                          *   until you have called dlb_paloud_get_samples().
                                                          */
    ,DLB_PALOUD_ERROR_RUNTIME_INPUT_EMPTY       =  1005  /**< The input does not contain enough samples to perform processing.
                                                          *   Do not call dlb_paloud_process() again until you have added
                                                          *   samples by calling dlb_paloud_add_samples().
                                                          */
    ,DLB_PALOUD_ERROR_RUNTIME_OUTPUT_EMPTY      =  1006  /**< The output contains less samples than were requested.
                                                          *   Do not call dlb_paloud_get_samples() again until you have added
                                                          *   samples by calling dlb_paloud_process().
                                                          */
    ,DLB_PALOUD_ERROR_RUNTIME_MIF_SEGMENT_ERROR =  1007  /**< The metadata interchange format segment is corrupted. */

    ,DLB_PALOUD_ERROR_INTERNAL                  = 10000  /**< Internal loudness processing error. */
} dlb_paloud_status;

/**
 * @brief Warning codes for the library
 */
typedef enum dlb_paloud_warning_codes_t
{
     DLB_PALOUD_WARNING_COUNT_REACHED                   = 1  /**< More warnings have been reported than the warnings structure can handle. New warnings may not be reported. */
    ,DLB_PALOUD_WARNING_LEVEL_MODE_INVALID              = 2  /**< The chosen setting for loudness leveler mode is invalid. */
    ,DLB_PALOUD_WARNING_REG_TYPE_INVALID                = 3  /**< The chosen setting for loudness regulation type is invalid. */
    ,DLB_PALOUD_WARNING_DIALNORM_INVALID                = 4  /**< The chosen setting for dialnorm is not within the valid range. */
    ,DLB_PALOUD_WARNING_DIALOGUE_INTELLIGENCE_INVALID   = 5  /**< The chosen setting for dialogue intelligence is invalid. */
    ,DLB_PALOUD_WARNING_PEAK_LIMIT_INVALID              = 6  /**< The chosen setting for peak limit is not within the valid range. */
    ,DLB_PALOUD_WARNING_UNEXPECTED_MIF_VERSION          = 7  /**< Unexpected version of incoming metadata interchange format segment. */
} dlb_paloud_warning_codes;

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PALOUD_COMMON_H */
