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
 *  @file  dlb_padmx_params_common.h
 *
 *  @brief Defines common types and enumerations used across the API
 */

#ifndef DLB_PADMX_PARAMS_COMMON_H
#define DLB_PADMX_PARAMS_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup OPAQUE Opaque types
 * @{
 */

/**
 * @brief Opaque Dolby PADMX state
 */
typedef struct dlb_padmx_state_s dlb_padmx_state;

/** @} */


/**
 * @defgroup STD_PARAM_ENUM Parameter Value Enumerations
 * @{
 */

/**
 * @typedef dlb_padmx_renderer_mode
 * @brief Mode used to select whether the component is used as the downmixer or upmixer.
 */
typedef enum
{
    DLB_PADMX_RENDERER_MODE_DOWNMIX = 0,    /**< Downmix usage */
    DLB_PADMX_RENDERER_MODE_UPMIX,          /**< Upmix usage */
    DLB_PADMX_RENDERER_MODE_DEFAULT = DLB_PADMX_RENDERER_MODE_DOWNMIX, /**< Downmix usage */
}
dlb_padmx_renderer_mode;

/**
 * @typedef dlb_padmx_trim
 * @brief Trim values supported for the 5.1 speaker configuration
 */
typedef enum
{
    DLB_PADMX_TRIM_DISABLED = 0,    /**< 0dB trim   */
    DLB_PADMX_TRIM_NEG_3DB,         /**< -3dB trim  */
    DLB_PADMX_TRIM_NEG_6DB,         /**< -6dB trim  */
    DLB_PADMX_TRIM_NEG_9DB,         /**< -9dB trim  */
    DLB_PADMX_TRIM_NEG_12DB,        /**< -12dB trim */
    DLB_PADMX_TRIM_HTRIM_DEFAULT = DLB_PADMX_TRIM_NEG_6DB,
    DLB_PADMX_TRIM_STRIM_DEFAULT = DLB_PADMX_TRIM_DISABLED,
}
dlb_padmx_trim;

/**
 * @typedef dlb_padmx_downmix_gain
 * @brief Downmix gains supported for the 2.0 speaker configuration
*/
typedef enum
{
    DLB_PADMX_DMX_GAIN_POS_3_0DB,
    DLB_PADMX_DMX_GAIN_POS_1_5DB,
    DLB_PADMX_DMX_GAIN_POS_0_0DB,
    DLB_PADMX_DMX_GAIN_NEG_1_5DB,
    DLB_PADMX_DMX_GAIN_NEG_3_0DB,
    DLB_PADMX_DMX_GAIN_NEG_4_5DB,
    DLB_PADMX_DMX_GAIN_NEG_6_0DB,
    DLB_PADMX_DMX_GAIN_NEG_INF_DB,
    DLB_PADMX_DMX_GAIN_DEFAULT = DLB_PADMX_DMX_GAIN_NEG_3_0DB,
}
dlb_padmx_downmix_gain;

/**
 * @typedef dlb_padmx_input_speaker_config
 * @brief Type used to select input speaker configuration for the downmixer/upmixer.
 *
 * @note DLB_PADMX_INPUT_SPEAKER_CFG_2_0 and DLB_PADMX_INPUT_SPEAKER_CFG_AUTO 
 *       is not allowed for the downmix.
 * @note Only DLB_PADMX_INPUT_SPEAKER_CFG_2_0, DLB_PADMX_INPUT_SPEAKER_CFG_5_1,
 *       DLB_PADMX_INPUT_SPEAKER_CFG_5_1_4, DLB_PADMX_INPUT_SPEAKER_CFG_METADATA
 *       and DLB_PADMX_INPUT_SPEAKER_CFG_AUTO is allowed for the upmix.
 */
typedef enum
{
    DLB_PADMX_INPUT_SPEAKER_CFG_2_0 = 0,    /**< Use 2.0 input speaker configuration */
    DLB_PADMX_INPUT_SPEAKER_CFG_5_1,        /**< Use 5.1 input speaker configuration */
    DLB_PADMX_INPUT_SPEAKER_CFG_5_1_2,      /**< Use 5.1.2 input speaker configuration */
    DLB_PADMX_INPUT_SPEAKER_CFG_5_1_4,      /**< Use 5.1.4 input speaker configuration */
    DLB_PADMX_INPUT_SPEAKER_CFG_7_1_4,      /**< Use 7.1.4 input speaker configuration */
    DLB_PADMX_INPUT_SPEAKER_CFG_9_1_6,      /**< Use 9.1.6 input speaker configuration */
    DLB_PADMX_INPUT_SPEAKER_CFG_METADATA,   /**< Use MIF to determine input speaker configuration */
    DLB_PADMX_INPUT_SPEAKER_CFG_AUTO,       /**< Use channel mode auto detection (2.0, 5.1, 5.1.4) */
    DLB_PADMX_INPUT_SPEAKER_CFG_DEFAULT = DLB_PADMX_INPUT_SPEAKER_CFG_METADATA, /**< Use MIF to determine input speaker configuration as default */
}
dlb_padmx_input_speaker_config;


/**
 * @typedef dlb_padmx_target_speaker_config
 * @brief Type used to select output speaker configuration for the downmixing/upmixing.
 *
 * @note Only DLB_PADMX_TARGET_SPEAKER_CFG_5_1 and DLB_PADMX_TARGET_SPEAKER_CFG_5_1_4 is allowed for the upmix.
 */
typedef enum
{
    DLB_PADMX_TARGET_SPEAKER_CFG_2_0_LTRT = 0,  /**< Stereo speaker output with LtRt downmix */
    DLB_PADMX_TARGET_SPEAKER_CFG_2_0_LORO,      /**< Stereo speaker output with LoRo downmix */
    DLB_PADMX_TARGET_SPEAKER_CFG_2_0_AUTO,      /**< Stereo speaker output with automatically detected downmix type */
    DLB_PADMX_TARGET_SPEAKER_CFG_5_1,           /**< 5.1 speaker output */
    DLB_PADMX_TARGET_SPEAKER_CFG_5_1_2,         /**< 5.1.2 speaker output */
    DLB_PADMX_TARGET_SPEAKER_CFG_5_1_4,         /**< 5.1.4 speaker output */
    DLB_PADMX_TARGET_SPEAKER_CFG_7_1_4,         /**< 7.1.4 speaker output */
    DLB_PADMX_TARGET_SPEAKER_CFG_9_1_6,         /**< 9.1.6 speaker output */
    DLB_PADMX_TARGET_SPEAKER_CFG_DEFAULT = DLB_PADMX_TARGET_SPEAKER_CFG_2_0_LORO,   /**< Stereo speaker output with LoRo downmix as default */
}
dlb_padmx_target_speaker_config;

/**
 * @typedef dlb_padmx_presentation_selection_mode
 * @brief Type used to select presentation selection mode for rendering in the downmixing mode.
 */
typedef enum
{
    DLB_PADMX_PRES_SELECT_MODE_BY_INDEX = 0,
    DLB_PADMX_PRES_SELECT_MODE_DEFAULT = DLB_PADMX_PRES_SELECT_MODE_BY_INDEX
}
dlb_padmx_presentation_selection_mode;

/** @} */

/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter Data Structures
 * @{
 */

/**
 * @brief Structure used to configure presentation index selected for rendering.
 */
typedef struct dlb_padmx_presentation_selection_s
{
    int mode;                   /**< Presentation selection mode for rendering, allowed values defined by
                                    enum dlb_padmx_presentation_selection_mode. This mode determines 
                                    which field of the union "select_by" will be used to select the presentation
                                    for rendering. */
    union{
        unsigned int index;     /* When presentation is selected by index, the range of allowed values is: [0, 15]*/
    } select_by;
} dlb_padmx_presentation_selection;

/**
 * @brief Structure used to configure trim values for rendering.
 * 
 * @note Trims are applied only in Dolby ED2 and S-ADM rendering workflows.
 * 
 * @see dlb_padmx_trim
 */
typedef struct dlb_padmx_trim_config_t
{
    int height_trim;        /**< Value for height trim */
    int surround_trim;      /**< Value for surround trim */
}
dlb_padmx_trim_config;

/**
 * @brief Structure used to configure stereo downmix gains for rendering.
 * 
 * @note These values are applied only when target speaker config is 2.0.
 * 
 * @see dlb_padmx_downmix_gain
*/
typedef struct dlb_padmx_downmix_config_t
{
    int center;    /**< Attenuation of Center channel when downmixing to stereo. */
    int surround;  /**< Attenuation of surround channels when downmixing to stereo. */
}
dlb_padmx_downmix_config;

/** @} */

/**
 * @defgroup ERROR_CODES Error and Warning Code Values
 * @{
 */

/**
 * @typedef dlb_padmx_status
 * @brief Return codes of operations' status
 */
typedef enum
{
    DLB_PADMX_STATUS_OK                       = 0,   /**< Function returned successfully */
    DLB_PADMX_STATUS_WARNING                  = 1,   /**< Function returned with warning(s) */

    DLB_PADMX_ERROR_NULL_POINTER              = 101, /**< Null pointer was passed as input to function */
    DLB_PADMX_ERROR_INVALID_CONFIGURATION     = 102, /**< Invalid configuration of component */
    DLB_PADMX_ERROR_OPEN                      = 103, /**< Error when opening component */
    DLB_PADMX_ERROR_INPUT_TOO_LARGE           = 105, /**< Number of input samples bigger than max allowed */
    DLB_PADMX_ERROR_OUTPUT_TOO_LARGE          = 106, /**< Number of output samples requested bigger than max allowed */
    DLB_PADMX_ERROR_CORRUPT_STATE             = 107, /**< Error when state memory is violated */
    DLB_PADMX_ERROR_FLUSHED                   = 108, /**< Component is being or has already been flushed */
    DLB_PADMX_ERROR_MEMORY_ALLOCATION         = 109, /**< Error when memory for the component cannot be allocated */
    DLB_PADMX_ERROR_INVALID_SPEAKER_CONFIG    = 110, /**< !Deprecated! Selected speaker configuration for rendered presentation is not valid */
}
dlb_padmx_status;

/**
 * @typedef dlb_padmx_warning_codes
 * @brief Warning codes for operation status
*/
typedef enum
{
    DLB_PADMX_WARN_INPUT_FULL                 = 1001, /**< Input queue is full, use dlb_padmx_process() first */
    DLB_PADMX_WARN_OUTPUT_FULL                = 1002, /**< Output queue is full, use dlb_padmx_get_samples() first */
    DLB_PADMX_WARN_PRESENTATION_UNAVAILABLE   = 1003, /**< The selected presentation is not present in the bitstream */
    DLB_PADMX_WARN_MISSING_METADTA            = 1004, /**< DEPRECATED */
    DLB_PADMX_WARN_MISSING_METADATA           = 1004, /**< Metadata for the selected presentation is not present in the bitstream */
    DLB_PADMX_WARN_UNEXPECTED_MIF_VERSION     = 1005  /**< Metadata version is different than expected. */
}
dlb_padmx_warning_codes;

/** @} */

#ifdef __cplusplus
}
#endif

#endif // DLB_PADMX_PARAMS_COMMON_H

