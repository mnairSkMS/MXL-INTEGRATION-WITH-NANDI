/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                  Copyright (C) 2021-2024 by Dolby Laboratories.
 *                            All rights reserved.
 ******************************************************************************/

/**
 *  @file dlb_paec_params_runtime_ac4.h
 *
 *  @brief Dolby pro audio encoder component (PAEC)
 *         Dolby AC-4 run-time parameters.
 *
 *  Dolby AC-4 run-time parameters for PAEC.
 */

#ifndef DLB_PAEC_PARAMS_RUNTIME_AC4_H
#define DLB_PAEC_PARAMS_RUNTIME_AC4_H

#ifdef __cplusplus
extern "C" {
#endif

#include "dlb_paec_params_common_ac4.h"

/*
 * PAEC encodes PCM to Dolby AC-4. It is used to encode PCM input and to
 * transcode from one encoded bitstream to Dolby AC-4, when PCM is an intermediate
 * format in the signal chain.
 *
 * The dlb_paec_runtime_config_ac4 structure specifies the values for each
 * run-time parameter. Some or all of these parameters can be displayed on a product UI
 * and changed asynchronously by the end user. In a PCM encoding use-case, these
 * parameters specify the static values applied to the encoder. In a transcoding
 * use case, the encoder can follow (or conform to) the dynamic metadata
 * extracted from the decoded bitstream. To enable the end user to override this
 * dynamic external metadata, a number of run-time parameters provide an enumerated
 * value of DLB_PAEC_AC4_<PARAM>_AUTO that enables automatic mode to set this
 * parameter. When the application sets the parameter to its automatic mode (for
 * example, DLB_PAEC_AC4_DMX_METHOD_AUTO) the encoder follows the dynamic
 * external metadata. When the application sets a parameter to a specific value
 * (for example, DLB_PAEC_AC4_DMX_METHOD_LORO) the encoder overrides
 * the dynamic external metadata for the parameter with the specific value.
 *
 * All parameters that support automatic mode have it enabled by default.
 */

/**
 * @defgroup STD_PARAM_ENUM Parameter value enumerations
 * @{
 */

enum dlb_paec_ac4_datarate
{
     DLB_PAEC_AC4_DATARATE_48K      = 0         /**< 48 kbps */
    ,DLB_PAEC_AC4_DATARATE_64K      = 1         /**< 64 kbps */
    ,DLB_PAEC_AC4_DATARATE_96K      = 2         /**< 96 kbps */
    ,DLB_PAEC_AC4_DATARATE_128K     = 3         /**< 128 kbps */
    ,DLB_PAEC_AC4_DATARATE_144K     = 4         /**< 144 kbps */
    ,DLB_PAEC_AC4_DATARATE_192K     = 5         /**< 192 kbps */
    ,DLB_PAEC_AC4_DATARATE_256K     = 6         /**< 256 kbps */
    ,DLB_PAEC_AC4_DATARATE_288K     = 7         /**< 288 kbps */
    ,DLB_PAEC_AC4_DATARATE_320K     = 8         /**< 320 kbps */
    ,DLB_PAEC_AC4_DATARATE_384K     = 9         /**< 384 kbps */
    ,DLB_PAEC_AC4_DATARATE_448K     = 10        /**< 448 kbps */
    ,DLB_PAEC_AC4_DATARATE_512K     = 11        /**< 512 kbps */
    ,DLB_PAEC_AC4_DATARATE_768K     = 12        /**< 768 kbps */
    ,DLB_PAEC_AC4_DATARATE_1024K    = 13        /**< 1024 kbps */

    ,DLB_PAEC_AC4_DATARATE_EMISSION_DEFAULT  = DLB_PAEC_AC4_DATARATE_320K
    ,DLB_PAEC_AC4_DATARATE_PRIMARY_DISTRIBUTION_DEFAULT  = DLB_PAEC_AC4_DATARATE_512K
};

/**
 * @brief Dolby AC-4 channel configurations
 *
 * The expected ordering of input channels depends on the dlb_paec_ac4_bed_channel_config
 * run-time parameter. The following table illustrates the assignments for a
 * 16-channel PCM input:
 *
 * Input channel 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
 *
 * 2.0 (stereo)  L   R   x   x   x   x   x   x   x   x   x   x   x   x   x   x
 * 5.1           L   R   C  LFE Ls  Rs   x   x   x   x   x   x   x   x   x   x
 * 5.1.4         L   R   C  LFE Ls  Rs  Tfl Tfr Tbl Tbr  x   x   x   x   x   x
 *
 * x   = Unused channel
 */
enum dlb_paec_ac4_bed_channel_config
{
     DLB_PAEC_AC4_BED_CHANNEL_CONFIG_20         = 0     /**< 2.0 (stereo) */
    ,DLB_PAEC_AC4_BED_CHANNEL_CONFIG_51         = 1     /**< 5.1 */
    ,DLB_PAEC_AC4_BED_CHANNEL_CONFIG_514        = 2     /**< 5.1.4 */

    ,DLB_PAEC_AC4_BED_CHANNEL_CONFIG_EMISSION_DEFAULT  = DLB_PAEC_AC4_BED_CHANNEL_CONFIG_514
    ,DLB_PAEC_AC4_BED_CHANNEL_CONFIG_PRIMARY_DISTRIBUTION_DEFAULT  = DLB_PAEC_AC4_BED_CHANNEL_CONFIG_51
};

enum dlb_paec_ac4_bed_classifier
{
     DLB_PAEC_AC4_BED_CLASS_COMPLETE_MAIN          = 0 /**< Complete Main with Dialogue content mixed in */
    ,DLB_PAEC_AC4_BED_CLASS_EMERGENCY              = 1 /**< Stereo Emergency */
    ,DLB_PAEC_AC4_BED_CLASS_MUSIC_EFFECTS          = 2 /**< Bed content consists of Music and Effects only without Dialogue or
                                                            associated content mixed in */
    ,DLB_PAEC_AC4_BED_CLASS_BROADCAST_MIX          = 3 /**< Complete Main with associated audio mixed in, 2.0 is the only valid channel configuration  */
    ,DLB_PAEC_AC4_BED_CLASS_DEFAULT    = DLB_PAEC_AC4_BED_CLASS_COMPLETE_MAIN
};

/**
* @brief Object channel mode
*/
typedef enum
{
     DLB_PAEC_AC4_OBJECT_CHANNEL_MODE_1_0          = 0 /**< Mono */
} dlb_paec_ac4_object_channel_mode;

/**
 * @brief Object content classifier
 *
 * Defines the type of content for the input object audio component.
 */
typedef enum
{
     DLB_PAEC_AC4_OBJECT_CLASS_DIALOGUE                        = 0 /**< Dialogue */
    ,DLB_PAEC_AC4_OBJECT_CLASS_ALTERNATIVE_DIALOGUE_WITH_AD    = 1 /**< Alternative Dialogue with Audio Description */
    ,DLB_PAEC_AC4_OBJECT_CLASS_AUDIO_DESCRIPTION               = 2 /**< Audio Description */
} dlb_paec_ac4_object_classifier;


/**
 * @brief Indicates the preferred way of downmixing to stereo in the decoder for multiple-channel channel modes.
 *
 * The preferred_dmx_method parameter value of the AC-4 bitstream is set accordingly.
 */
enum dlb_paec_ac4_dmx_method
{
    DLB_PAEC_AC4_DMX_METHOD_AUTO                = 0 /**< The parameter value is determined automatically.
                                                        For transcoding use cases, it is determined based on the input
                                                        bitstream metadata and the decoder configuration information.
                                                        For PCM encoding use cases, it is set to DLB_PAEC_AC4_DMX_METHOD_NOT_INDICATED. */
    ,DLB_PAEC_AC4_DMX_METHOD_NOT_INDICATED      = 1 /**< No preferred downmixing method is indicated. */
    ,DLB_PAEC_AC4_DMX_METHOD_LORO               = 2 /**< LoRo downmix is preferred. */
    ,DLB_PAEC_AC4_DMX_METHOD_PL                 = 3 /**< LtRt (Dolby Pro Logic) downmix is preferred. */
    ,DLB_PAEC_AC4_DMX_METHOD_PLII               = 4 /**< LtRt (Dolby Pro Logic II) downmix is preferred. */

    ,DLB_PAEC_AC4_DMX_METHOD_DEFAULT            = DLB_PAEC_AC4_DMX_METHOD_AUTO
};

/**
 * @brief Specifies the downmix gain for center and surround channels.
 */
enum dlb_paec_ac4_mixgain
{
    DLB_PAEC_AC4_MIXGAIN_AUTO       = 0 /**< The parameter value is determined automatically.
                                            For transcoding use cases, it is determined based on the input bitstream metadata.
                                            For PCM encoding use cases, it is set to -3.0 dB. */
    ,DLB_PAEC_AC4_MIXGAIN_3_0_DB    = 1 /**< +3.0 dB */
    ,DLB_PAEC_AC4_MIXGAIN_1_5_DB    = 2 /**< +1.5 dB */
    ,DLB_PAEC_AC4_MIXGAIN_0_0_DB    = 3 /**< 0.0 dB */
    ,DLB_PAEC_AC4_MIXGAIN_M_1_5_DB  = 4 /**< -1.5 dB */
    ,DLB_PAEC_AC4_MIXGAIN_M_3_0_DB  = 5 /**< -3.0 dB */
    ,DLB_PAEC_AC4_MIXGAIN_M_4_5_DB  = 6 /**< -4.5 dB */
    ,DLB_PAEC_AC4_MIXGAIN_M_6_0_DB  = 7 /**< -6.0 dB */
    ,DLB_PAEC_AC4_MIXGAIN_M_INF_DB  = 8 /**< -infinity dB */

    ,DLB_PAEC_AC4_CENTER_MIXGAIN_DEFAULT = DLB_PAEC_AC4_MIXGAIN_AUTO
    ,DLB_PAEC_AC4_SURROUND_MIXGAIN_DEFAULT = DLB_PAEC_AC4_MIXGAIN_AUTO
};

/**
 * @brief Defines to which channels height channels are downmixed.
 */
enum dlb_paec_ac4_height_channel_downmix
{
    DLB_PAEC_AC4_HEIGHT_CHANNEL_DOWNMIX_AUTO                 = 0 /**< The parameter value is determined automatically.
                                                                    For PCM encoding use cases, height channels are downmixed
                                                                    to front and surround channels.
                                                                    For transcoding use cases, it is determined based on the input
                                                                    bitstream metadata. */
    ,DLB_PAEC_AC4_HEIGHT_CHANNEL_DOWNMIX_FRONT               = 1 /**< Height channels are downmixed to front channels in the listening
                                                                    plane. */
    ,DLB_PAEC_AC4_HEIGHT_CHANNEL_DOWNMIX_SURROUND            = 2 /**< Height channels are downmixed to surround channels in the
                                                                    listening plane. */
    ,DLB_PAEC_AC4_HEIGHT_CHANNEL_DOWNMIX_FRONT_AND_SURROUND  = 3 /**< Height channels are downmixed to front and surround channels in
                                                                    the listening plane. For 5.1.2, height channels are mixed into the
                                                                    front only. */

    ,DLB_PAEC_AC4_HEIGHT_CHANNEL_DOWNMIX_DEFAULT = DLB_PAEC_AC4_HEIGHT_CHANNEL_DOWNMIX_AUTO
};

/**
 * @brief Downmix gain that is applied to height channels when downmixing.
 *
 * Defines gain applied to height channels when downmixing.
 * For transcoding use cases and when height_channel_downmix is set to DLB_PAEC_AC4_HEIGHT_CHANNEL_DOWNMIX_AUTO, the
 * height_downmix_gain value is ignored and determined based on the input bitstream.
 */
enum dlb_paec_ac4_height_downmix_gain
{
     DLB_PAEC_AC4_HEIGHT_DOWNMIX_GAIN_0_0_DB         = 0  /**< 0.0 dB */
    ,DLB_PAEC_AC4_HEIGHT_DOWNMIX_GAIN_M_1_5_DB       = 1  /**< -1.5 dB */
    ,DLB_PAEC_AC4_HEIGHT_DOWNMIX_GAIN_M_3_0_DB       = 2  /**< -3.0 dB */
    ,DLB_PAEC_AC4_HEIGHT_DOWNMIX_GAIN_M_4_5_DB       = 3  /**< -4.5 dB */
    ,DLB_PAEC_AC4_HEIGHT_DOWNMIX_GAIN_M_6_0_DB       = 4  /**< -6.0 dB */
    ,DLB_PAEC_AC4_HEIGHT_DOWNMIX_GAIN_M_9_0_DB       = 5  /**< -9.0 dB */
    ,DLB_PAEC_AC4_HEIGHT_DOWNMIX_GAIN_M_12_0_DB      = 6  /**< -12.0 dB */
    ,DLB_PAEC_AC4_HEIGHT_DOWNMIX_GAIN_M_INF_DB       = 7  /**< -infinity dB */

    ,DLB_PAEC_AC4_HEIGHT_DOWNMIX_GAIN_DEFAULT = DLB_PAEC_AC4_HEIGHT_DOWNMIX_GAIN_M_6_0_DB

};

/**
 * @brief Is used as an index to access the corresponding dynamic range control decoder mode in the drc_decoder_mode[] array in dlb_paec_ac4_drc.
 */
enum dlb_paec_ac4_drc_decoder_mode_id
{
     DLB_PAEC_AC4_DRC_DECODER_MODE_ID_HOME_THEATRE        = 0 /**< Home Theatre Mode: fixed output level range from -31 to -27 dB. */
    ,DLB_PAEC_AC4_DRC_DECODER_MODE_ID_FLAT_PANEL_TV       = 1 /**< Flat Panel TV Mode: fixed output level range from -26 to -17 dB. */
    ,DLB_PAEC_AC4_DRC_DECODER_MODE_ID_PORTABLE_SPEAKERS   = 2 /**< Portable Speakers Mode: fixed output level range from -16 to 0 dB. */
    ,DLB_PAEC_AC4_DRC_DECODER_MODE_ID_PORTABLE_HEADPHONES = 3 /**< Portable Headphones Mode: fixed output level range from -16 to 0 dB */
    ,DLB_PAEC_AC4_NUM_DRC_DECODER_MODES                   = 4 /**< Number of dynamic range control decoder modes. */
};

/**
 * @brief Specifies the dynamic range control profile
 * Used to set the dynamic range control profile to be applied when this dynamic range control mode is used within the decoder.
 */
enum dlb_paec_ac4_drc_profile
{
     DLB_PAEC_AC4_DRC_PROFILE_AUTO          = 0         /**< The parameter is determined automatically. In transcoding use cases,
                                                           it is determined based on the input bitstream metadata and
                                                           decoder configuration information input to the library.
                                                           In encoding from PCM use cases, the parameter is set to its default. */
    ,DLB_PAEC_AC4_DRC_PROFILE_NONE          = 1         /**< None */
    ,DLB_PAEC_AC4_DRC_PROFILE_FILMSTD       = 2         /**< Film Standard */
    ,DLB_PAEC_AC4_DRC_PROFILE_FILMLIGHT     = 3         /**< Film Light */
    ,DLB_PAEC_AC4_DRC_PROFILE_MUSICSTD      = 4         /**< Music Standard */
    ,DLB_PAEC_AC4_DRC_PROFILE_MUSICLIGHT    = 5         /**< Music Light */
    ,DLB_PAEC_AC4_DRC_PROFILE_SPEECH        = 6         /**< Speech */

    ,DLB_PAEC_AC4_DRC_PROFILE_EMISSION_DEFAULT = DLB_PAEC_AC4_DRC_PROFILE_AUTO
    ,DLB_PAEC_AC4_DRC_PROFILE_PRIMARY_DISTRIBUTION_DEFAULT = DLB_PAEC_AC4_DRC_PROFILE_NONE
};

/**
 * @brief Constants for the AC-4 I-frame interval.
 */
enum dlb_paec_ac4_iframe_interval_constants
{
     DLB_PAEC_AC4_IFRAME_INTERVAL_INFINITE           = 0        /**< Special value to indicate that only the
                                                                      first frame in the output bitstream is an I-frame. This
                                                                      value is only used if I-frames are to be
                                                                      triggered manually. */
    ,DLB_PAEC_AC4_IFRAME_INTERVAL_MIN_2398_FPS       = 11       /**< Minimum I-frame interval for 23.98 fps, equivalent to an
                                                                      I-frame occuring every 0.458 seconds */
    ,DLB_PAEC_AC4_IFRAME_INTERVAL_MIN_24_FPS         = 12       /**< Minimum I-frame interval for 24 fps, equivalent to an
                                                                     I-frame occuring every 0.5 seconds */
    ,DLB_PAEC_AC4_IFRAME_INTERVAL_MIN_25_FPS         = 12       /**< Minimum I-frame interval for 25 fps, equivalent to an
                                                                     I-frame occuring every 0.48 seconds */
    ,DLB_PAEC_AC4_IFRAME_INTERVAL_MIN_2997_FPS       = 14       /**< Minimum I-frame interval for 29.97 fps, equivalent to an
                                                                     I-frame occuring every 0.467 seconds */
    ,DLB_PAEC_AC4_IFRAME_INTERVAL_MIN_30_FPS         = 15       /**< Minimum I-frame interval for 30 fps, equivalent to an
                                                                     I-frame occuring every 0.5 seconds */
    ,DLB_PAEC_AC4_IFRAME_INTERVAL_MIN_NATIVE_FPS     = 11       /**< Minimum I-frame interval for Native fps, equivalent to an
                                                                     I-frame occuring every 0.469 seconds */
    ,DLB_PAEC_AC4_IFRAME_INTERVAL_DEFAULT_2398_FPS   = 24       /**< Default I-frame interval for 23.98 fps */
    ,DLB_PAEC_AC4_IFRAME_INTERVAL_DEFAULT_24_FPS     = 24       /**< Default I-frame interval for 24 fps */
    ,DLB_PAEC_AC4_IFRAME_INTERVAL_DEFAULT_25_FPS     = 25       /**< Default I-frame interval for 25 fps */
    ,DLB_PAEC_AC4_IFRAME_INTERVAL_DEFAULT_2997_FPS   = 30       /**< Default I-frame interval for 29.97 fps */
    ,DLB_PAEC_AC4_IFRAME_INTERVAL_DEFAULT_30_FPS     = 30       /**< Default I-frame interval for 30 fps */
    ,DLB_PAEC_AC4_IFRAME_INTERVAL_DEFAULT_NATIVE_FPS = 24       /**< Default I-frame interval for Native fps */
    ,DLB_PAEC_AC4_IFRAME_INTERVAL_MAXIMUM            = 1000     /**< Maximum I-frame interval */
};

/**
 * @brief Constants for the Dolby AC-4 gains.
 */
enum dlb_paec_ac4_gains_constants
{
     DLB_PAEC_AC4_FLOATING_POINT_SCALEFACTOR                = 100   /**< Scale factor for floating point values to represent them as integer values */
    ,DLB_PAEC_AC4_NEGATIVE_INFINITY_GAIN                    = -9999 /**< Value representing negative infinite gain in dB */

    ,DLB_PAEC_AC4_APC_BED_GAIN_MAX                          = 0     /**< Maximum bed gain for Dolby AC-4, 0 dB */
    ,DLB_PAEC_AC4_APC_BED_GAIN_MIN                          = -1550 /**< Minimum finite bed gain for Dolby AC-4, -15.5 dB */
    ,DLB_PAEC_AC4_APC_BED_GAIN_STEP                         = 50    /**< The step at which the bed gain for Dolby AC-4 is expressed, 0.5 dB */
    ,DLB_PAEC_AC4_APC_OBJECT_GAIN_MAX                       = 1500  /**< Maximum object gain for Dolby AC-4, 15 dB */
    ,DLB_PAEC_AC4_APC_OBJECT_GAIN_MIN                       = -4900 /**< Minimum finite object gain for Dolby AC-4, -49 dB dB */
    ,DLB_PAEC_AC4_APC_OBJECT_GAIN_STEP                      = 100   /**< The step at which the object gain for Dolby AC-4 is expressed, 1 dB */
    ,DLB_PAEC_AC4_APC_DEFAULT_GAIN                          = 0     /**< Default bed and object gains for Dolby AC-4 */
};

typedef struct
{
    unsigned int             channel_mode;              /**< see dlb_paec_ac4_bed_channel_config */
    unsigned int             classifier;                /**< see dlb_paec_ac4_bed_classifier */
} dlb_paec_ac4_bed;

typedef struct
{
    unsigned int             channel_mode;              /**< see dlb_paec_ac4_object_channel_mode */
    unsigned int             classifier;                /**< see dlb_paec_ac4_object_classifier */
} dlb_paec_ac4_object;

typedef struct
{
    unsigned int bed_component_id;
    int          bed_component_gain;   /**< Specifies the bed gain for Dolby AC-4.
                                            Set between -15.5 and 0 dB using the values from DLB_PAEC_AC4_BED_GAIN_MIN (-1550) to DLB_PAEC_AC4_BED_GAIN_MAX (0)
                                            (value * DLB_PAEC_FLOATING_POINT_SCALEFACTOR) or to minus infinity dB gain using the DLB_PAEC_AC4_NEGATIVE_INFINITY_GAIN value.
                                            ** Note that values ​​must be expressed in increments of DLB_PAEC_AC4_APC_BED_GAIN_STEP (0.5 dB). */
    unsigned int num_object_components;
    unsigned int object_components_ids[DLB_PAEC_AC4_MAX_OBJECT_COMPONENTS_PER_PRESENTATION];
    int          object_components_gains[DLB_PAEC_AC4_MAX_OBJECT_COMPONENTS_PER_PRESENTATION];  /**< Specifies object gains for Dolby AC-4.
                                                                                                     Set between -49 and 15 dB using the values from DLB_PAEC_AC4_OBJECT_GAIN_MIN (-4900) to DLB_PAEC_AC4_OBJECT_GAIN_MAX (1500)
                                                                                                     (value * DLB_PAEC_FLOATING_POINT_SCALEFACTOR) or to minus infinity dB gain using the DLB_PAEC_AC4_NEGATIVE_INFINITY_GAIN value.
                                                                                                     ** Note that values must be expressed in increments of DLB_PAEC_AC4_APC_OBJECT_GAIN_STEP (1 dB). */
    char         language[DLB_PAEC_AC4_MAX_LANGUAGE_TAG_BYTES];                                 /**< Specify 2- or 3-byte IETF language,
                                                                                                     with a NULL terminating character */
} dlb_paec_ac4_presentation;

/**
 * @brief Specifies a dynamic range control decoder mode
 *
 * The dynamic range control decoder mode ID is determined by the index in the drc_decoder_mode[] array in dlb_paec_ac4_drc corresponding to the
 * value of dlb_paec_ac4_drc_decoder_mode_id.
 */
typedef struct
{
    unsigned int     predefined_profile;     /**< see dlb_paec_ac4_drc_profile */
} dlb_paec_ac4_drc_decoder_mode;


/**
 * @brief Set of downmix information for either LoRo or LtRt downmixing in the decoder.
 */
typedef struct
{
    unsigned int  center_mixgain;    /**< See dlb_paec_ac4_mixgain. DLB_PAEC_AC4_MIXGAIN_M_INF_DB is not a valid center_mixgain
                                          value. */
    unsigned int  surround_mixgain;  /**< See dlb_paec_ac4_mixgain. DLB_PAEC_AC4_MIXGAIN_3_0_DB and DLB_PAEC_AC4_MIXGAIN_1_5_DB
                                          are not valid surround_mixgain values. */
} dlb_paec_ac4_dmx_set;


/**
 * @brief This metadata controls a decoder downmix of a channel-based immersive bitstream, such as content with height channels.
 */
typedef struct
{
    unsigned int               height_channel_downmix; /**< see dlb_paec_ac4_height_channel_downmix */
    unsigned int               height_downmix_gain;    /**< see dlb_paec_ac4_height_downmix_gain */
} dlb_paec_ac4_custom_dmx;


/**
 * @brief Encoder configuration of audio program components (APC)
 *
 * This configuration must be passed to the function call to dlb_paec_runtime_config_set().
 *
 * Channel layout - the channel layout for the chosen APC configuration is as follows:
 *   1 - Bed PCM channels (the number of channels is determined by dlb_paec_ac4_bed_channel_config)
 *   2 - All object PCM channels (the number of channels is determined by dlb_paec_ac4_object_channel_mode)
 *
 * Presentations are specified with dlb_paec_ac4_presentation.
 *
 * Encoder bit rate - The total bit rate of the encoded bitstream is specified with dlb_paec_ac4_bitrate.
 *
 * The total bit rate should be set to a good quality or higher level.
 * Audio quality levels
 *                     Good Quality [kbps]       Excellent Quality [kbps]        High Quality Emission [kbps]
 * 1.0                          32                      48                                  64
 * 2.0                          48                      64                                  96
 * 5.1                          96                      144                                 192
 * 5.1.4                        192                     288                                 384
 *
 * In primary distribution mode, the following configurations are supported:
 *    2.0 Complete Main: 192, 256, and 320 kbps
 *    5.1 Complete Main: 512 kbps
 *    5.1.4 Complete Main: 512, 768, and 1024 kbps
 */

typedef struct
{
    unsigned int                num_bed_components;                                            /**< Number of bed components */
    dlb_paec_ac4_bed            bed_components[DLB_PAEC_AC4_MAX_BED_COMPONENTS];               /**< Bed audio program component configuration */
    unsigned int                num_object_components;                                         /**< Number of object components */
    dlb_paec_ac4_object         object_components[DLB_PAEC_AC4_MAX_OBJECT_COMPONENTS];         /**< Object audio program component configuration */
    unsigned int                num_presentations;                                             /**< Number of presentations */
    dlb_paec_ac4_presentation   presentations[DLB_PAEC_AC4_MAX_NUM_PRESENTATIONS];             /**< Presentation configuration */
    unsigned int                bitrate;                                                       /**< Total bit rate,
                                                                                                    dlb_paec_ac4_datarate */
} dlb_paec_ac4_apc;


/**
 * @brief This metadata controls a decoder downmix to 5.1 or stereo.
 */
typedef struct
{
    unsigned int                preferred_dmx_method; /**< See dlb_paec_ac4_dmx_method */
    dlb_paec_ac4_dmx_set        loro_dmx_set;         /**< See struct dlb_paec_ac4_dmx_set */
    dlb_paec_ac4_dmx_set        ltrt_dmx_set;         /**< See struct dlb_paec_ac4_dmx_set */
    dlb_paec_ac4_custom_dmx     custom_dmx;           /**< This is only used for DLB_PAEC_AC4_BED_CHANNEL_CONFIG_514 */
} dlb_paec_ac4_dmx;

/**
 * @brief This metadata controls a decoder dynamic range control processing.
 */
typedef struct
{
    dlb_paec_ac4_drc_decoder_mode  drc_decoder_mode[DLB_PAEC_AC4_NUM_DRC_DECODER_MODES];/**< Use dlb_paec_ac4_drc_decoder_mode_id
                                                                                          as index to access a specific dynamic range control
                                                                                          decoder mode. */
} dlb_paec_ac4_drc;


#define DLB_PAEC_LOUDNESS_NOT_PRESENT   (-120.f)

/**
 * @brief The loudness regulation type status information
 */
typedef enum
{
     DLB_PAEC_LOUDNESS_PRACTICE_STATUS_NOT_INDICATED      /**< Loudness regulation type is not indicated. */
    ,DLB_PAEC_LOUDNESS_PRACTICE_STATUS_ATSC_A_85          /**< Loudness regulation type complies with ATSC A/85 specification. */
    ,DLB_PAEC_LOUDNESS_PRACTICE_STATUS_EBU_R128           /**< Loudness regulation type complies with EBU R128 specification. */
    ,DLB_PAEC_LOUDNESS_PRACTICE_STATUS_ARIB_TR_B32        /**< Loudness regulation type complies with ARIB TR-B32 specification. */
    ,DLB_PAEC_LOUDNESS_PRACTICE_STATUS_FREETV_OP_59       /**< Loudness regulation type complies with FreeTV OP-59 specification. */
    ,DLB_PAEC_LOUDNESS_PRACTICE_STATUS_RESERVED_05
    ,DLB_PAEC_LOUDNESS_PRACTICE_STATUS_RESERVED_06
    ,DLB_PAEC_LOUDNESS_PRACTICE_STATUS_RESERVED_07
    ,DLB_PAEC_LOUDNESS_PRACTICE_STATUS_RESERVED_08
    ,DLB_PAEC_LOUDNESS_PRACTICE_STATUS_RESERVED_09
    ,DLB_PAEC_LOUDNESS_PRACTICE_STATUS_RESERVED_10
    ,DLB_PAEC_LOUDNESS_PRACTICE_STATUS_RESERVED_11
    ,DLB_PAEC_LOUDNESS_PRACTICE_STATUS_RESERVED_12
    ,DLB_PAEC_LOUDNESS_PRACTICE_STATUS_RESERVED_13
    ,DLB_PAEC_LOUDNESS_PRACTICE_STATUS_MANUAL             /**< No specification indicated. Loudness controls are set manually. */
    ,DLB_PAEC_LOUDNESS_PRACTICE_STATUS_CONSUMER_LEVELLER  /**< Loudness is measured with a third-party leveler. */
} dlb_paec_loudness_practice_status;

/**
 * @brief Loudness gating status information
 */
typedef enum
{
     DLB_PAEC_LOUDNESS_UNGATED              /**< No gating has been applied. */
    ,DLB_PAEC_LOUDNESS_GATING_SPEECH       /**< Speech gating has been applied (such as Dialogue Intelligence). */
    ,DLB_PAEC_LOUDNESS_GATING_LEVEL        /**< Level gating has been applied. */
} dlb_paec_loudness_gating_type;

/**
 * @brief Loudness correction type
 */
typedef enum
{
     DLB_PAEC_LOUDNESS_CORRECTION_FILE_BASED = 0        /**< Corrected with infinite lookahead (file based).  */
    ,DLB_PAEC_LOUDNESS_CORRECTION_REALTIME             /**< Corrected with finite lookahead (real time). */
} dlb_paec_loudness_correction;


/*
 * @brief Loudness dialogue gating method
 */
typedef enum
{
     DLB_PAEC_DIALGATE_METHOD_NOT_PRESENT_IN_FRAME = -1 /**< Dialogue gating value is not present in the AC-4 bitstream. */
    ,DLB_PAEC_DIALGATE_METHOD_NOT_INDICATED = 0        /**< Dialogue gating value is present in the AC-4 bitstream, but set to a
                                                            value of not indicated. */
    ,DLB_PAEC_DIALGATE_METHOD_AUTOMATED_C_OR_L_R       /**< Dialogue gating is applied to the center channel of a multiple-channel
                                                            program or to the power sum of left and right channels in a stereo
                                                            program. */
    ,DLB_PAEC_DIALGATE_METHOD_AUTOMATED_L_C_R          /**< Dialogue gating is applied individually to all front (main) channels
                                                            of the program. */
    ,DLB_PAEC_DIALGATE_METHOD_MANUAL                   /**< A manual process was utilized to select representative portions of the
                                                            program containing dialogue for measurement. */
} dlb_paec_dialog_gating_method;

typedef struct
{
    dlb_paec_loudness_practice_status   loudness_practice;      /**< Indicates the loudness regulation type used for program loudness
                                                                     measurement. */
    dlb_paec_loudness_gating_type       gating_type;            /**< Indicates whether program has been corrected using level or dialogue
                                                                     gating. */
    dlb_paec_dialog_gating_method       dialog_gating_method;   /**< Indicates the method of dialogue gating used. */
    dlb_paec_loudness_correction        loudness_correction;    /**< Indicates the type of loudness correction used. */
    float                               loudness_level_gated;   /**< Indicates the integrated loudness (LUFS) of the audio program,
                                                                     measured according to ITU-R BS.1770-3. Set to
                                                                     DLB_PAEC_LOUDNESS_NOT_PRESENT if value is not present. */
    float                               loudness_dialog_gated;  /**< Indicates the integrated loudness (LUFS) of the audio program,
                                                                     measured according to ITU-R BS.1770-3. Set to
                                                                     DLB_PAEC_LOUDNESS_NOT_PRESENT if value is not present. */
    float                               loudness_short_term_3s; /**< Indicates the ungated loudness of the preceding three seconds of the
                                                                     audio program measured according to ITU-R BS.1770-3.
                                                                     DLB_PAEC_LOUDNESS_NOT_PRESENT if value is not present. */
    float                               true_peak;              /**< Indicates the true-peak sample value (LUFS) of the audio program,
                                                                     measured according to ITU-R BS.1771-1. Set to
                                                                     DLB_PAEC_LOUDNESS_NOT_PRESENT if value is not present. */
    float                               loudness_range;         /**< Indicates the loudness range (LU) of the audio program as specified in
                                                                     EBU Tech Document 3342. Set to DLB_PAEC_LOUDNESS_NOT_PRESENT if
                                                                     value is not present. */
} dlb_paec_loudness_info;

typedef struct
{
    unsigned int            presentation_id;                                /**< Presentation identifier */
    char                    language[DLB_PAEC_AC4_MAX_LANGUAGE_TAG_BYTES];  /**< BCP 47 language tag */
    unsigned int            b_loudness;                                     /**< Presence of loudness information */
    dlb_paec_loudness_info  loudness;                                       /**< Measured loudness of the presentation written into the
                                                                                 AC-4 bitstream */
} dlb_paec_presentation_info;

typedef struct
{
    unsigned int                size;                                           /**< Size (in bytes) of an AC-4 frame */
    unsigned int                frame_duration_samples;                         /**< Duration (in samples) of an AC-4 frame. Can be used
                                                                                     to set size of output SMPTE 337 frames. */
    unsigned int                buffer_fullness;                                /**< Current (in bits) buffer fullness or fill level in the
                                                                                     decoder */
    unsigned int                wait_frames;                                    /**< Number of frames the AC-4 decoder has to wait prior
                                                                                     to decoding in order to avoid a buffer underflow */
    unsigned int                is_iframe;                                      /**< Set if this frame is an I-frame */
    unsigned int                sequence_counter;                               /**< Counter ranging from 1 to 1020, used to identify
                                                                                     a frame */
    unsigned int                num_presentations;                              /**< Number of presentations */
    dlb_paec_presentation_info  presentations[DLB_PAEC_AC4_MAX_NUM_PRESENTATIONS]; /**< Information about each presentation */
} dlb_paec_frame_info_ac4;

/**
 * @brief Dolby AC-4 transport configuration parameters
 *
 * iframe_interval - Indicates an interval (in number of frames) between I-frames. Additionally,
 * I-frames can be triggered manually using the dlb_paec_set_manual_iframe function.
 *
 * Default I-frame interval values depend on the frame rate and are stored in dlb_paec_ac4_iframe_interval_constants
 * along with minimum and maximum values for iframe_interval.
 */
typedef struct
{
    unsigned int                iframe_interval;
} dlb_paec_ac4_transport;


/** @} */


/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter data structures (high level)
 * @{
 */


/**
 * @brief Dolby AC-4 encoding configuration.
 */
typedef struct
{
    dlb_paec_ac4_apc           apc;        /**< configuration of audio program components (APC) */
    dlb_paec_ac4_drc           drc;
    dlb_paec_ac4_dmx           dmx;
    dlb_paec_ac4_transport     transport;  /**< transport configuration */
} dlb_paec_runtime_config_ac4;

/** @} */


/*********************************************/
/*** Error and Warning Code Values ***********/
/*********************************************/

/**
 * @defgroup ERROR_CODES Error and warning code values
 * @{
 */


/**
 * @brief Dolby AC-4 encode errors
 *
 * Values 3100 and above are consider internal library errors
 *
 */
enum dlb_paec_status_ac4
{
     DLB_PAEC_ERROR_AC4_INTERNAL                                    = 3000  /**< Internal error */
    ,DLB_PAEC_ERROR_AC4_FRAME_RATE_INVALID                          = 3001
    ,DLB_PAEC_ERROR_AC4_ENCODING_MODE_INVALID                       = 3002
    ,DLB_PAEC_ERROR_AC4_INPUT_NUM_CHANNELS                          = 3003
    ,DLB_PAEC_ERROR_AC4_INVALID_BLOCK_SIZE                          = 3004
    ,DLB_PAEC_ERROR_AC4_INVALID_API_CALL_AFTER_FLUSH                = 3005
    ,DLB_PAEC_ERROR_AC4_INVALID_FLUSH_WITHOUT_ADD_BLOCK             = 3006
    ,DLB_PAEC_ERROR_AC4_SYSTEM_DELAY_INVALID                        = 3007
    ,DLB_PAEC_ERROR_AC4_RUNTIME_CONFIG_REJECTED                     = 3008
    ,DLB_PAEC_ERROR_AC4_SCALE_MAIN_INVALID                          = 3009
    ,DLB_PAEC_ERROR_AC4_LIBRARY                                     = 3010
    ,DLB_PAEC_ERROR_AC4_PRIMARY_DISTRIBUTION_TRANSCODING_REJECTED   = 3011
};

typedef enum
{
    AC4_WARNING_CHAN_CONFIG_LIMITED = 2000,
    AC4_WARNING_PCM_SECURITY_FAILURE,
    AC4_WARNING_NONINITIAL_METADATA_IGNORED,
    AC4_WARNING_DIALNORM_CLAMPED,
    AC4_WARNING_MD_ERROR_CONCEALMENT_ACTIVE,
    AC4_WARNING_MD_SECURITY_FAILURE,
    AC4_WARNING_NO_VALID_INPUT_PAYLOADS,
    AC4_WARNING_UNSUPPORTED_PAYLOADS_DROPPED,
    AC4_WARNING_INTELLIGENT_LOUDNESS_PROCESSING_ERROR,
    AC4_WARNING_INPUT_MD_TRANSLATION_FAILED,
    AC4_WARNING_INVALID_INPUT_MD_DURATION,
    AC4_WARNING_CRITICAL_DDP_MD_VALUE_OUT_OF_RANGE,
    AC4_WARNING_NON_CRITICAL_DDP_MD_VALUE_OUT_OF_RANGE,
    AC4_WARNING_CRITICAL_DOLBY_E_MD_VALUE_OUT_OF_RANGE,
    AC4_WARNING_NON_CRITICAL_DOLBY_E_MD_VALUE_OUT_OF_RANGE,
    AC4_WARNING_DECODER_CONFIG_MD_VALUE_OUT_OF_RANGE,
    AC4_WARNING_CRITICAL_EXT_LOUD_MD_VALUE_OUT_OF_RANGE,
    AC4_WARNING_NON_CRITICAL_EXT_LOUD_MD_VALUE_OUT_OF_RANGE,
    AC4_WARNING_IFRAME_INTERVAL_INVALID,
    AC4_WARNING_PREF_DMX_METHOD_INVALID,
    AC4_WARNING_CDMX_GAIN_INVALID,
    AC4_WARNING_CDMX_TARGET_INVALID,
    AC4_WARNING_CENTER_MIXGAIN_INVALID,
    AC4_WARNING_SURROUND_MIXGAIN_INVALID,
    AC4_WARNING_LOUDNESS_PRACTICE_INVALID,
    AC4_WARNING_DIALNORM_INVALID,
    AC4_WARNING_RTLL_PEAK_LIMIT_INVALID,
    AC4_WARNING_RTLL_DIALOG_INTEL_INVALID,
    AC4_WARNING_DRC_EAC3_PROFILE_INVALID,
    AC4_WARNING_DRC_PREDEFINED_PROFILE_INVALID,
    AC4_WARNING_LANGUAGE_TAG_INVALID,
    AC4_WARNING_BITRATE_INVALID,
    AC4_WARNING_APC_UNSUPPORTED,
    AC4_WARNING_APC_UNREFERENCED_COMPONENT,
    AC4_WARNING_APC_PRESENTATION_REFERENCES_INVALID_COMPONENT,
    AC4_WARNING_APC_BED_CHANNEL_MODE_INVALID,
    AC4_WARNING_APC_BED_CONTENT_CLASS_INVALID,
    AC4_WARNING_APC_OBJECT_CHANNEL_MODE_INVALID,
    AC4_WARNING_APC_OBJECT_CONTENT_CLASS_INVALID,
    AC4_WARNING_APC_NUM_BEDS_INVALID,
    AC4_WARNING_APC_NUM_OBJECTS_INVALID,
    AC4_WARNING_APC_NUM_PRESENTATIONS_INVALID,
    AC4_WARNING_APC_PRESENTATION_LANGUAGE_TAG_INVALID,
    AC4_WARNING_CRITICAL_AC4_MD_VALUE_OUT_OF_RANGE,
    AC4_WARNING_NON_CRITICAL_AC4_MD_VALUE_OUT_OF_RANGE,
    AC4_WARNING_APC_NO_CORRESPONDING_PRESENTATION_WITHOUT_AA,
    AC4_WARNING_INTERNAL_UNEXPECTED,
    AC4_WARNING_PALOUD_AUTHOR_MODE_INVALID,
    AC4_WARNING_BYPASS_ON_COMPLEX_CONFIGURATION, /** DEPRECATED */
    AC4_WARNING_BITRATE_TOO_LOW_FOR_S_ADM,
    AC4_WARNING_S_ADM_REJECTED,
    AC4_WARNING_UNEXPECTED_MIF_VERSION, /** DEPRECATED */
    AC4_WARNING_S_ADM_INVALID_FOR_PRIMARY_DISTRIBUTION_MODE,
    AC4_WARNING_BROADCAST_MIX_CHANNEL_CONFIG_INVALID,
    AC4_WARNING_BROADCAST_MIX_MISSING_COMPLETE_MAIN,
    AC4_WARNING_APC_GAIN_INVALID,
    AC4_WARNING_NONZERO_GAIN_IN_PRESENTATION
} dlb_paec_warning_codes_ac4;

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PAEC_PARAMS_RUNTIME_AC4_H */
