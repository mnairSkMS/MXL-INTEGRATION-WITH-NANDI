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
 *  @file dlb_paec_params_common.h
 *
 *  @brief Dolby pro audio encoder component (PAEC) common parameters.
 *
 *  Common parameters for PAEC.
 */

#ifndef DLB_PAEC_PARAMS_COMMON_H
#define DLB_PAEC_PARAMS_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @defgroup STD_PARAM_ENUM Parameter value enumerations
 * @{
 */

/**
 * @brief System constants
 */
enum dlb_paec_system_constants
{
    DLB_PAEC_SAMPLE_RATE    = 48000     /**< Fixed sample rate for PAEC */
};

/** @} */

/**
 * @brief Warnings for PAEC, extended by specific encoders
 * 1    -  999 - Common
 * 1000 - 1999 - DDP
 * 2000 - 2999 - AC4
 */
typedef enum
{
    DLB_PAEC_WARNING_COUNT_REACHED = 1, /**< More warnings have been reported than the warnings structure can handle. New warnings may not be reported. */
    DLB_PAEC_UNEXPECTED_MIF_VERSION     /**< Incoming metadata interchange format segment created with unexpected version */
} dlb_paec_warning_codes;


/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter data structures (high level)
 * @{
 */

/**
 * @brief Opaque Dolby PAEC state
 */
typedef struct dlb_paec_state_s dlb_paec_state;


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
} dlb_paec_version;


/** @} */


/*********************************************/
/*** Error and Warning Code Values ***********/
/*********************************************/

/**
 * @defgroup ERROR_CODES Error and warning code values
 * @{
 */

/**
 * @brief Status return codes of the PAEC
 */
typedef enum dlb_paec_status_t
{
     DLB_PAEC_STATUS_OK                         =      0    /**< Function returned successfully. */
    ,DLB_PAEC_STATUS_WARNING                    =      1    /**< One or more warnings have been reported, call dlb_paec_warnings_get for more information */
    ,DLB_PAEC_ERROR_NULL_POINTER                =    101    /**< A NULL pointer was passed as input. */
    ,DLB_PAEC_ERROR_NULL_INPUT_POINTER          =    102    /**< A NULL data pointer was passed as a required parameter
                                                             *   of the dlb_paec_input structure.
                                                             */
    ,DLB_PAEC_ERROR_NULL_OUTPUT_POINTER         =    103    /**< A NULL data pointer was passed as a required parameter
                                                             *   of the dlb_paec_output structure.
                                                             */
    ,DLB_PAEC_ERROR_NO_SETUP                    =    104    /**< dlb_paec_setup() was not called prior to dlb_paec_query(). */
    ,DLB_PAEC_ERROR_NO_QUERY                    =    105    /**< dlb_paec_query() was not called prior to dlb_paec_open(). */
    ,DLB_PAEC_ERROR_CORRUPT_STATE               =    106    /**< Either the PAEC internal state was corrupted or dlb_paec_open()
                                                             *   was not called. If the state was corrupted, close PAEC and reopen it.
                                                             */
    ,DLB_PAEC_ERROR_MEMORY_ALLOCATION           =    107    /**< Internal memory allocation failed. System heap is
                                                             *   either out of memory or fragmented.
                                                             */
    ,DLB_PAEC_ERROR_FLUSH_EXCESS_INPUT_SAMPLES  =    108    /**< dlb_paec_flush() received more samples than it can handle.
                                                             *   Process already added samples with dlb_paec_process() and then retrieve the
                                                             *   resulting output frame using dlb_paec_get_frame() before calling dlb_paec_flush().
                                                             */
    ,DLB_PAEC_ERROR_FLUSHED                     =    109    /**< This error is returned by the dlb_paec_add_samples and dlb_paec_process functions,
                                                             *   indicating that the dlb_paec_flush function was called prior.
                                                             */
    ,DLB_PAEC_ERROR_INIT_TYPE                   =    200    /**< The \<type\> in the initialization structure is out of its valid range. */
    ,DLB_PAEC_ERROR_INIT_USER_PCM_DELAY_SAMPLES =    201    /**< The \<user_pcm_delay_samples\> in the initialization structure
                                                             *   is out of its valid range.
                                                             */
    ,DLB_PAEC_ERROR_RUNTIME_BLOCK_SIZE          =   1000    /**< The \<block_size\> in the dlb_paec_input structure is not a valid
                                                             *   fixed block_size.
                                                             *   -  @ref dlb_paec_block_size_constants
                                                             */
    ,DLB_PAEC_ERROR_RUNTIME_INPUT_SAMPLES       =   1001    /**< Invalid number of samples was passed to @ref dlb_paec_add_samples(). */
    ,DLB_PAEC_ERROR_RUNTIME_INPUT_FULL          =   1002    /**< The input is full. Do not call @ref dlb_paec_add_samples() again
                                                             *   until after calling @ref dlb_paec_process().
                                                             */
    ,DLB_PAEC_ERROR_RUNTIME_INPUT_EMPTY         =   1003    /**< The input does not contain enough samples to perform processing.
                                                             *   Do not call dlb_paec_process() again until new samples are added
                                                             *   by calling dlb_paec_add_samples().
                                                             */
    ,DLB_PAEC_ERROR_RUNTIME_INPUT_CHANNELS      =   1004    /**< Invalid number of channels was passed to @ref dlb_paec_add_samples().
                                                             *   Currently, only 16 channels are supported.
                                                             */
    ,DLB_PAEC_ERROR_RUNTIME_OUTPUT_FULL         =   1005    /**< The output already contains a frame.
                                                             *   Do not call @ref dlb_paec_process() until a new frame is retreived
                                                             *   by calling @ref dlb_peac_get_frame().
                                                             */
    ,DLB_PAEC_ERROR_RUNTIME_OUTPUT_EMPTY        =   1006    /**< The output does not contain a frame.
                                                             *   Do not call dlb_paec_get_frame() again until @ref dlb_paec_process()
                                                             *   has finished.
                                                             */
    ,DLB_PAEC_ERROR_DDP_RUNTIME                 =   2000    /**< A parameter in the Dolby Digital Plus run-time configuration structure is out of its valid range.
                                                             *   -  The actual error will have a value starting at this offset (2000)
                                                             *   -  @ref dlb_paec_status_ddp
                                                             */
    ,DLB_PAEC_ERROR_AC4_RUNTIME                 =   3000    /**< A parameter in the Dolby AC-4 run-time configuration structure is out of its valid range.
                                                             *   -  The actual error will have a value starting at this offset (3000)
                                                             *   -  @ref dlb_paec_status_ac4 */
    ,DLB_PAEC_ERROR_RUNTIME_MIF_SEGMENT_ERROR   =  10000    /**< The metadata interchange format segment is corrupted. */
    ,DLB_PAEC_ERROR_INTERNAL                    = 100000    /**< Internal PAEC error. The actual error will have this value (100000)
                                                             *   plus an internal code. Report this to customer support.
                                                             */
} dlb_paec_status;

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PAEC_PARAMS_COMMON_H */
