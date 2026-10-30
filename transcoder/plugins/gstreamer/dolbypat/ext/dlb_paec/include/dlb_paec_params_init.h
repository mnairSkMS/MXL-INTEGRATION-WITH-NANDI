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
 *  @file dlb_paec_params_init.h
 *
 *  @brief Dolby pro audio encoder component (PAEC) initialization parameters.
 *
 *  Initialization parameters for PAEC.
 */

#ifndef DLB_PAEC_PARAMS_INIT_H
#define DLB_PAEC_PARAMS_INIT_H

#include "dlb_paec_params_init_ac4.h"
#include "dlb_paec_params_init_ddp.h"

#include <stddef.h>     /* for size_t */

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @defgroup STD_PARAM_ENUM Parameter value enumerations
 * @{
 */

/**
 * @brief Type of encoder
 */
typedef enum
{
     DLB_PAEC_TYPE_DD       = 0     /**< Sets encoder type to Dolby Digital for 2.0 and 5.1 channel configurations.
                                     *   Sets encoder type to Dolby Digital Plus with Dolby Atmos content for all other channel configurations.
                                     */
    ,DLB_PAEC_TYPE_DDP      = 1     /**< Sets encoder type to Dolby Digital Plus for 2.0 and 5.1 channel configurations.
                                     *   Sets encoder type to Dolby Digital Plus with Dolby Atmos content for all other channel configurations.
                                     */
    ,DLB_PAEC_TYPE_AC4      = 2     /**< Sets encoder type to Dolby AC-4. */

    ,DLB_PAEC_TYPE_DEFAULT  = DLB_PAEC_TYPE_DDP
} dlb_paec_type;


/**
 * @brief User PCM delay samples constants
 */
enum dlb_paec_user_pcm_delay_samples_constants
{
     DLB_PAEC_USER_PCM_DELAY_SAMPLES_MIN        = 0         /**< Zero (0) samples of a user PCM delay */
    ,DLB_PAEC_USER_PCM_DELAY_SAMPLES_DDP_MAX    = 1535      /**< 1535 samples of a user PCM delay for Dolby Digital and Dolby Digital Plus */
    ,DLB_PAEC_USER_PCM_DELAY_SAMPLES_AC4_MAX    = 1000      /**< 1000 samples of a user PCM delay for Dolby AC-4 */

    ,DLB_PAEC_USER_PCM_DELAY_SAMPLES_DEFAULT    = DLB_PAEC_USER_PCM_DELAY_SAMPLES_MIN
};

/** @} */


/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter data structures (high level)
 * @{
 */

/**
 * @brief Initialization-time PAEC configuration parameters.
 */
typedef struct
{
    unsigned int    type;               /**< The type of encoder to instantiate.
                                        *   -  default: DLB_PAEC_TYPE_DEFAULT
                                        */
    /** The number of PCM samples of latency introduced by user audio processing.
     *  This is used to keep audio and metadata aligned.
     *  -  @ref dlb_paec_user_pcm_delay_samples_constants
     *  -  DLB_PAEC_USER_PCM_DELAY_SAMPLES_MIN to DLB_PAEC_USER_PCM_DELAY_SAMPLES_DDP_MAX/DLB_PAEC_USER_PCM_DELAY_SAMPLES_AC4_MAX
     *  -  default: DLB_PAEC_USER_PCM_DELAY_SAMPLES_DEFAULT
     */
    unsigned int    user_pcm_delay_samples;

    union {
        dlb_paec_initcfg_ddp      ddp;      /**< Dolby Digital Plus with Dolby Atmos content initialization parameters */
        dlb_paec_initcfg_ac4      ac4;      /**< Dolby AC-4 initialization parameters */
    } config;

    unsigned int    internal_data;      /**< Internal library use only, do not edit */
} dlb_paec_init_config;


/**
 * @brief Encoding requirements structure
 *
 * @verbatim
 *
 * @max_input_samples
 * The maximum number of samples that PAEC supports at the input.
 *
 * When transcoding, the block size at the input can change for each call to dlb_paec_add_samples().
 * The \<max_input_samples\> can be used to calculate the maximum input buffer size in bytes
 * that is currently supported by PAEC. Use it to allocate the input buffer once and for all supported block sizes.
 *
 * @max_output_size
 * The maximum number of bytes that PAEC supports at the output.
 *
 * Use this to allocate the output buffer.
 *
 * @latency
 * The number of audio samples after which the library starts processing the first input data.
 *
 * When encoding video, in order to maintain A/V synchronization, reduce the system audio delay
 * by the number of samples represented by latency.
 *
 * num_blocks=latency/block_size
 *
 * The input buffers load one block of samples at a time and send that block to the library
 * for further processing.
 *
 *    -------      -------      ------
 *   | input  |  | process |  | output |
 *    ------- |    -------      ------
 *   | block  |->|  blocks |->|        |
 *   |   #1   |       1         ------
 *    -------      -------
 *   .
 *    -------      -------      ------
 *   | input  |  | process |  | output |
 *    ------- |    -------      ------
 *   | block  |->|  blocks |->|        |
 *   |   #2   |  |   1,2   |    ------
 *    -------      -------
 *   .
 *   .
 *   .
 *    -------------      -------------      ------
 *   |    input     |  |    process    |  | output |
 *    ------------- |    -------------      ------
 *   |    block#    |->|     blocks    |->|        |
 *   |  num_blocks  |  | 1..num_blocks |    ------
 *    -------------    |   (latency)   |
 *                       -------------
 *
 *   1 frame = 1,536 samples (for Dolby Digital Plus)
 *   num_blocks = 3,072 (samples of latency) /
 *                256 (samples per input block)
 *              = 12
 *
 *   At block #12, the input buffer loads the last block of frame 2.
 *    --------------      ---------------      ------
 *   |     input     |  |      process    |  | output |
 *    -------------- |    ---------------      ------
 *   |    block      |->|      blocks     |->|        |
 *   |     #12       |  |       1..12     |    ------
 *    --------------    |     (latency)   |
 *   |                    ---------------
 *   |    frame 2    |  | frame2, frame 1 |
 *    --------------      ---------------
 *
 *   The library outputs only the frame that is fully processed. Partial processing is not possible.
 *
 *   The input buffer loads the last block of frame 3.
 *    -------------       ---------------      ------
 *   |    input      |  |      process    |  | output |
 *    -------------  |    ---------------      ------
 *   |    block      |->|      blocks     |->| frame 1|
 *   |     #18       |  |       6..18     |    ------
 *    -------------     |     (latency)   |
 *                        ---------------
 *   |    frame 3    |  | frame3, frame 2 |
 *    -------------       ---------------
 *
 * @endverbatim
 */
typedef struct
{
    unsigned int    max_input_samples;      /**< Maximum number of input samples (per channel) that the encoder accepts. */
    unsigned int    max_input_channels;     /**< Maximum number of input channels. */
    size_t          max_output_size;        /**< Maximum output size in bytes that can be extracted from the component. */
    unsigned int    latency;                /**< A latency of PAEC for the selected encoder. */
} dlb_paec_requirements;


/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PAEC_PARAMS_INIT_H */
