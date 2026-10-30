/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                  Copyright (C) 2020-2022 by Dolby Laboratories.
 *                            All rights reserved.
 ******************************************************************************/

/**
 *  @file dlb_paloud_params_runtime.h
 *
 *  @brief Dolby pro audio loudness leveler component run-time parameters.
 *
 *  Run-time parameters for the library.
 */

#ifndef DLB_PALOUD_PARAMS_RUNTIME_H
#define DLB_PALOUD_PARAMS_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup STD_PARAM_ENUM Parameter value enumerations
 * @{
 */

/**
 * @brief PALOUD constants
 */
enum dlb_paloud_constants
{
     DLB_PALOUD_BLOCK_SIZE_256             = 256   /**< Fixed number of samples processed for each 
                                                    *   call of dlb_paloud_process()
                                                    */

    ,DLB_PALOUD_NUM_CHANNELS               = 16     /**< Fixed number of input and output channels */

    ,DLB_PALOUD_FLOATING_POINT_SCALEFACTOR = 10    /**< Scale factor for floating point values to represent them as integer values */
    ,DLB_PALOUD_PEAK_LIMIT_MIN             = -80   /**< Minimum peak limit, -8.0 dBTP */
    ,DLB_PALOUD_PEAK_LIMIT_MAX             = -1    /**< Maximum peak limit, -0.1 dBTP */
    ,DLB_PALOUD_PEAK_LIMIT_DEFAULT         = -2    /**< Default peak limit, -0.2 dBTP */    

    ,DLB_PALOUD_DIALNORM_MIN               = -31   /**< Minimum dialogue normalization,-31 dB */
    ,DLB_PALOUD_DIALNORM_MAX               = -10   /**< Maximum dialogue normalization,-10 dB */
    ,DLB_PALOUD_DIALNORM_DEFAULT           = -24   /**< Default dialogue normalization,-24 dB */    
};


/**
 * @brief Loudness regulation type
 * @ref dlb_paloud_reg_type internally sets
 * dialogue intelligence and target loudness parameters so that
 * the output conforms to different loudness regulation standards (per ITU-R BS.1770)
 * - type = DLB_PALOUD_REG_TYPE_ATSC   : dialogue intelligence = ON , target loudness = -24 LKFS
 * - type = DLB_PALOUD_REG_TYPE_EBU    : dialogue intelligence = OFF, target loudness = -23 LKFS
 * - type = DLB_PALOUD_REG_TYPE_ARIB   : dialogue intelligence = ON,  target loudness = -24 LKFS
 * - type = DLB_PALOUD_REG_TYPE_FREETV : dialogue intelligence = OFF, target loudness = -24 LKFS
 * - type = DLB_PALOUD_REG_TYPE_MANUAL : Loudness control configured manually with @ref dlb_paloud_leveler_config 
 */
typedef enum dlb_paloud_reg_type_t
{
     DLB_PALOUD_REG_TYPE_ATSC     = 0                    /**< Program loudness complies with ATSC A/85 */
    ,DLB_PALOUD_REG_TYPE_EBU                             /**< Program loudness complies with EBU R 128 */
    ,DLB_PALOUD_REG_TYPE_ARIB                            /**< Program loudness complies with ARIB TR-B32 */
    ,DLB_PALOUD_REG_TYPE_FREETV                          /**< Program loudness complies with FreeTV OP-59 */
    ,DLB_PALOUD_REG_TYPE_MANUAL                          /**< No specification indicated. Loudness control configured manually. */
    ,DLB_PALOUD_REG_TYPE_DEFAULT = DLB_PALOUD_REG_TYPE_ATSC
} dlb_paloud_reg_type;


/** @brief Loudness leveler mode
 * Use @ref dlb_paloud_level_mode to set loudness leveler operating mode.
 * Every mode of operation ensures consistent processing time.
 */
typedef enum dlb_paloud_level_mode_t
{
     DLB_PALOUD_LEVEL_MODE_AUTO  = -1           /**< Auto mode:
                                                    *  -  If available, follows external metadata (dlb_paio_pcm_block.p_md)
                                                    *     Use external metadata to determine settings. The component automatically bypasses leveling if
                                                    *     external metadata settings match user settings for the loudness regulation type.
                                                    *  -  otherwise, DLB_PALOUD_LEVEL_MODE_AUTO_DEFAULT is selected */
    ,DLB_PALOUD_LEVEL_MODE_OFF                  /**< Bypass audio loudness leveling. */
    ,DLB_PALOUD_LEVEL_MODE_ON                   /**< Ignore external metadata and force loudness leveling of audio so that it is compliant with the standard @ref dlb_paloud_reg_type. */

    ,DLB_PALOUD_LEVEL_MODE_DEFAULT = DLB_PALOUD_LEVEL_MODE_AUTO
    ,DLB_PALOUD_LEVEL_MODE_AUTO_DEFAULT = DLB_PALOUD_LEVEL_MODE_ON
} dlb_paloud_level_mode;

/** @brief The dialogue intelligence ON / OFF parameters when the loudness leveler runs using the DLB_PALOUD_REG_TYPE_MANUAL loudness regulation type
 */
typedef enum dlb_paloud_dialogue_intelligence_t
{
     DLB_PALOUD_DIALOGUE_INTELLIGENCE_NOT_INITIALIZED         /**< Dialogue intelligence not initialized */
    ,DLB_PALOUD_DIALOGUE_INTELLIGENCE_OFF                     /**< Dialogue intelligence off */
    ,DLB_PALOUD_DIALOGUE_INTELLIGENCE_ON                      /**< Dialogue intelligence on */
    ,DLB_PALOUD_DIALOGUE_INTELLIGENCE_DEFAULT = DLB_PALOUD_DIALOGUE_INTELLIGENCE_ON
} dlb_paloud_dialogue_intelligence;

/**
 * @brief Loudness leveler configuration parameters
 *
 * This structure is used only for the loudness regulation type set to DLB_PALOUD_REG_TYPE_MANUAL 
 *
 */
typedef struct
{
    int           dialnorm;                     /**< Sets the dialogue normalization value. Set in the range from DLB_PALOUD_DIALNORM_MIN (-31 dB) to DLB_PALOUD_DIALNORM_MAX (-10 dB) */
    int           dialogue_intelligence;        /**< Switches dialogue intelligence ON / OFF when running the loudness leveler  @ref dlb_paloud_dialogue_intelligence*/
    int           peak_limit;                   /**< Specifies the peak value for the loudness leveler peak limiter.
                                                     Set between -8.0 and -0.1 dBTP using the values from DLB_PALOUD_PEAK_LIMIT_MIN (-80) to DLB_PALOUD_PEAK_LIMIT_MAX (-1)
                                                     (value * DLB_PALOUD_FLOATING_POINT_SCALEFACTOR)
                                                     **Note that values must be expressed in steps of 1 (0.1 dBTP). */
} dlb_paloud_leveler_config;


/** @} */

/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter data structures (high level)
 * @{
 */


/**
 * @brief Run-time configuration
 */
typedef struct dlb_paloud_runtime_config_s
{
    int                         mode;    /**< @see dlb_paloud_level_mode */
    int                         type;    /**< Loudness regulation type @ref dlb_paloud_reg_type
                                         *   If incoming metadata segment (p_md in the dlb_paio_pcm_block)
                                         *   already contains a practice type, then it is compared with the type value:
                                         *   - If they match, incoming PCM is passed through and no loudness leveling is applied
                                         *   - If they do not match, the value set here is used for loudness leveling
                                         */
    dlb_paloud_leveler_config   config;  /**< Loudness leveler configuration parameters @ref dlb_paloud_leveler_config
                                         *    Used only for DLB_PALOUD_REG_TYPE_MANUAL loudness regulation type.
                                         */
} dlb_paloud_runtime_config;

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PALOUD_PARAMS_RUNTIME_H */
