/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                  Copyright (C) 2021-2023 by Dolby Laboratories.
 *                            All rights reserved.
 ******************************************************************************/

/**
 *  @file dlb_paio.h
 *
 *  @brief Dolby pro audio input/output (PAIO) structures.
 *
 *  Structures for the PAIO.
 */

#ifndef DLB_PAIO_H
#define DLB_PAIO_H

#include "dlb_pamif.h"

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


#define DLB_MAX_NUM_WARNINGS (25)


/**
 * @defgroup STD_PARAM_ENUM Parameter Value Enumerations
 * @{
 */

/**
 * @brief PAIO library's current version constants
 *
 */
enum dlb_paio_version
{
     DLB_PAIO_VERSION_MAJOR        = 1    /**< Major release */
    ,DLB_PAIO_VERSION_MINOR        = 2    /**< Minor release */
    ,DLB_PAIO_VERSION_FUNCT        = 0    /**< Functional release */
};

/**
 * @brief PCM constants
 *
 * @see DLB_PAIO_PCM_BLOCK_SIZE_MIN @see DLB_PAIO_PCM_BLOCK_SIZE_MAX
 * The number of samples contained in a @ref dlb_paio_pcm_block structure.
 *
 * The actual minimum and maximum is determined by the components
 * being used.
 *
 * @see DLB_PAIO_PCM_NUM_CHANNELS
 * Set input / output interface to use 16 channels.
 */
enum dlb_paio_pcm_constants
{
     DLB_PAIO_PCM_BLOCK_SIZE_MIN        = 1         /**< One PCM sample */
    ,DLB_PAIO_PCM_BLOCK_SIZE_MAX        = 2048      /**< 2,048 PCM samples */
    ,DLB_PAIO_PCM_NUM_CHANNELS          = 16        /**< 16 channels */
};

enum dlb_paio_bitstream_constants
{
     DLB_PAIO_MAX_NUM_BITSTREAMS        = 2
};

/** @} */

/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter Data Structures (High Level)
 * @{
*/


/**
 * @brief Pro audio I/O PCM sample buffer structure
 *
 * This PCM sample buffer structure is used between two (or more) other components (either Dolby
 * pro audio components or user components). The same buffer structure is used as the output of
 * one component and the input of the other(s). The buffer needs to be sized to the maximum
 * requirements of the two (or more) components. For Dolby pro audio components, this size is
 * obtained from the \<max_input_samples\> or \<max_output_samples\> variables and the
 * \<max_input_channels\> or \<number_of_channels\> variables of the corresponding
 * \<dlb_paxxx_requirements\> structure returned from the \<dlb_paxxx_query\> function.
 * 
 * The PCM buffer \<p_pcm\> must contain raw PCM formatted as signed 32-bit integers
 * with data left-aligned (on little-endian systems). Note that the PCM buffer
 * \<p_pcm\> does not accept direct WAV-wrapped data. The data must be interleaved
 * with a stride (distance to the channel's next sample) equal to the number
 * of channels. Specify the number of interleaved channels with the
 * \<num_channels\> parameter.
 *
 * @TODO: The following is subject to change:
 *
 * The ordering of the channels depends on processing.
 *
 * In most cases, some of the channels may be unused during processing, but must
 * be filled to ensure the proper stride.
 *
 * <pre>
 * Channel       0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
 * 2.0 (stereo)  L   R   x   x   x   x   x   x   x   x   x   x   x   x   x   x
 * 5.1           L   R   C  LFE Ls  Rs   x   x   x   x   x   x   x   x   x   x
 * 5.1.4         L   R   C  LFE Ls  Rs  Tfl Tfr Tbl Tbr  x   x   x   x   x   x
 * 7.1.4         L   R   C  LFE Ls  Rs  Lb  Rb  Tfl Tfr Tbl Tbr  x   x   x   x
 * 9.1.6         L   R   C  LFE Ls  Rs  Lb  Rb  Tfl Tfr Tsl Tsr Tbl Tbr Lw  Rw
 * </pre>
 *
 * NOTE: Currently the number of input channels is FIXED at 16 channels. The
 * \<num_channels\> parameter will be ignored.
 */
typedef struct dlb_paio_pcm_block_s
{
    int32_t                 *p_pcm;         /**< [in,out] Required: PCM sample buffer.
                                             *   -  Samples MUST be of type int32_t and interleaved.
                                             *   -  This buffer must be sized to the maximum of the required
                                             *      sizes of the connected components.
                                             */

    uint32_t                 num_samples;   /**< [in,out] Required: Number of PCM samples (per channel).
                                             *   -  @see dlb_paio_pcm_constants
                                             *   -  DLB_PAIO_PCM_BLOCK_SIZE_MIN to DLB_PAIO_PCM_BLOCK_SIZE_MAX
                                             *   -  [in] For input, this specifies the number of samples being
                                             *           added to the component.
                                             *   -  [out] For output, this specifies the number of samples
                                             *            returned by the component.
                                             */

    uint32_t                 num_channels;  /**< [in,out] Number of channels of PCM audio.
                                             *   -  NOTE: Currently FIXED to 16 channels internally in all Dolby
                                             *      pro audio components.
                                             *   -  Can only be set to 16.
                                             */

    dlb_pamif_md_segment    *p_md;          /**< [in,out] Optional: Inter-component metadata.
                                             *   -  This can be NULL if no metadata is required
                                             *      between components. This differs from
                                             *      component to component.
                                             *   -  If required, call dlb_pamif_md_segment_create()
                                             *      to create the metadata segment.
                                             */
} dlb_paio_pcm_block;

/**
 * @brief Pro audio I/O bitstream block buffer structure
 * 
 * This bitstream block buffer structure is used as an input to the pro audio decoder and as an output
 * from pro audio encoder. When the buffer is used as an input to pro audio decoder its size and number
 * of stored bytes depend on the user's preference. When the buffer is used as an output from pro audio encoder
 * component, its required size is indicated by \<max_output_size\>.
 * 
 */
typedef struct dlb_paio_bitstream_block_s
{
    uint8_t     *p_bitstreams[DLB_PAIO_MAX_NUM_BITSTREAMS];  /**< [in,out] Required: Bitstreams data buffer
                                                              *  - Bitstreams need to be uint8_t* data type
                                                              *  - [in]  For input, this specifies the array of
                                                              *          bitstreams added to the component.
                                                              *  - [out] For output, this specifies the array of
                                                              *          bitstreams returned by the component.
                                                              */
    uint8_t     num_bitstreams;                              /**< [in,out] Required: Number of bitstreams to be processed
                                                              *  - NOTE: This value is currently ignored in the Dolby pro audio encoder
                                                              *          component.
                                                              *  - NOTE: If value is bigger than allowed, an error is returned from
                                                              *          @ref dlb_padc_add_bytes() in Dolby pro audio decoder component.
                                                              */
    uint32_t    num_bytes[DLB_PAIO_MAX_NUM_BITSTREAMS];     /**< [in,out] Required: An array indicating the number of bytes in each bitstream
                                                             *   - [in]  For input, this specifies the number of bytes
                                                             *           added to the component.
                                                             *   - [out] For output, this specifies the number of bytes
                                                             *           returned by the component.
                                                             *   - NOTE: This should not be bigger than the value obtained in
                                                             *           dlb_padc_requirements.max_input_size in the Dolby pro audio
                                                             *           decoder component.
                                                             */
} dlb_paio_bitstream_block;


/**
 * @brief Warnings structure is used to get warnings from pro audio components.
 * 
 * The warning codes depend on components. 
 * 
 */
typedef struct
{
    unsigned int num_warnings;            /**< Number of warnings */
    int warnings[DLB_MAX_NUM_WARNINGS];   /**< An array with warning codes */
} dlb_warnings;


/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PAIO_H */
