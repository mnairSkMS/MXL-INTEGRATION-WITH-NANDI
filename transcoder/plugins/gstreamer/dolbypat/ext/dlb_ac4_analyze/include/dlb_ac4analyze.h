/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                Copyright (C) 2015-2024 by Dolby International AB.
 *                            All rights reserved.
 ******************************************************************************/

/**
 *  @file
 *
 *  @brief AC-4 bitstream analyzer API.
 */

/**
 * @ingroup ac4analyzer_api
 * @{
 */

#ifndef DLB_AC4ANALYZE_H
#define DLB_AC4ANALYZE_H

#include <stddef.h>                                 /* size_t */
#include "dlb_ac4analyze_ims.h"

#define AC4ANALYZE_MAX_NUM_SUPPORTED_PRESENTATIONS     64      /**< Maximal number of supported presentations. AC-4 ETSI specification: 511 */
#define AC4ANALYZE_MAX_NUM_SUPPORTED_SUBSTREAMS       128      /**< Maximal number of supported (audio) substreams. AC-4 ETSI specification: 1024 */
#define AC4ANALYZE_MAX_NUM_SUPPORTED_SUBSTREAM_GROUPS  64      /**< Maximum number of substream groups. AC-4 ETSI specification: 127 */
#define AC4ANALYZE_MAX_NUM_SUPPORTED_SUBSTREAMS_PER_GROUP 11   /**< Maximum number of substreams per substream group (per bitstream conformance) */
#define AC4ANALYZE_MAX_NUM_SUPPORTED_EMDF_INFO         16      /**< Maximal number of supported EMDF info entries. */
#define AC4ANALYZE_MAX_NUM_SUPPORTED_EMDF_PAYLOADS     8       /**< Maximal number of supported EMDF payloads per EMDF payloads substream. */
#define AC4ANALYZE_MAX_PRESNAME_LEN_BYTES              (32+1)  /**< Maximal number of bytes available for the alternative presentation name
                                                                    (plus 1 extra byte for the terminator char). */
#define AC4ANALYZE_MAX_LANGUAGE_TAG_BYTES              64      /**< The maximum supported size of a serialized language tag is MAX_LANGUAGE_TAG_BYTES-1. */
#define AC4ANALYZE_MAX_BCP47_LANGUAGE_TAG_BYTES        11      /**< Maximum supported size of a language tag in an Iframe (including null terminating character) */ 
#define AC4ANALYZE_MAX_ISO639_LANGUAGE_TAG_BYTES       4       /**< Maximum supported size of a language tag in a non Iframe (including null terminating character) */
#define AC4ANALYZE_PROGRAM_UUID_LEN_BYTES              16      /**< Maximal number of bytes available for the program uuid (16*8 bits). */
#define AC4ANALYZE_MAX_NUM_DRC_DECODER_MODES           8       /**< Maximal number of DRC decoder modes transmitted in a bitstream. */
#define AC4ANALYZE_MAX_ABBREV_LANGUAGE_TAG_BYTES       AC4ANALYZE_MAX_LANGUAGE_TAG_BYTES  /**< Deprecated */
#define AC4ANALYZE_MAX_TARGETS_PER_PRESENTATION        32      /**< Maximum number of targets per presentation */

#define AC4ANALYZE_SYNC_WORD_BYTES          2
#define AC4ANALYZE_SIZE_INFO_EXTENDED       5
#define AC4ANALYZE_CRC_BYTES                2
#define AC4ANALYZE_SIMPLE_FORMAT_OVERHEAD   (AC4ANALYZE_SYNC_WORD_BYTES   + \
                                             AC4ANALYZE_SIZE_INFO_EXTENDED + \
                                             AC4ANALYZE_CRC_BYTES)

/**
 * @brief Channel assignment according to the nonstd_bed_channel_assignment_mask in the bitstream specification.
 */
#define AC4ANALYZE_BIT_MASK_EMPTY (0x00000ul) /**< Empty. */
#define AC4ANALYZE_BIT_MASK_L     (0x00001ul) /**< Left channel. */
#define AC4ANALYZE_BIT_MASK_R     (0x00002ul) /**< Right channel. */
#define AC4ANALYZE_BIT_MASK_C     (0x00004ul) /**< Center channel. */
#define AC4ANALYZE_BIT_MASK_LFE   (0x00008ul) /**< Low-frequency effects. */
#define AC4ANALYZE_BIT_MASK_LS    (0x00010ul) /**< Left surround. */
#define AC4ANALYZE_BIT_MASK_RS    (0x00020ul) /**< Right surround. */
#define AC4ANALYZE_BIT_MASK_LB    (0x00040ul) /**< Left back. */
#define AC4ANALYZE_BIT_MASK_RB    (0x00080ul) /**< Right back. */
#define AC4ANALYZE_BIT_MASK_TFL   (0x00100ul) /**< Top front left. */
#define AC4ANALYZE_BIT_MASK_TFR   (0x00200ul) /**< Top front right. */
#define AC4ANALYZE_BIT_MASK_TSL   (0x00400ul) /**< Top side left. */
#define AC4ANALYZE_BIT_MASK_TSR   (0x00800ul) /**< Top side right. */
#define AC4ANALYZE_BIT_MASK_TBL   (0x01000ul) /**< Top rear left. */
#define AC4ANALYZE_BIT_MASK_TBR   (0x02000ul) /**< Top rear right. */
#define AC4ANALYZE_BIT_MASK_LW    (0x04000ul) /**< Left front wide. */
#define AC4ANALYZE_BIT_MASK_RW    (0x08000ul) /**< Right front wide. */
#define AC4ANALYZE_BIT_MASK_LFE2  (0x10000ul) /**< Low-frequency effects 2. */

/* Define for -Inf dB used for e.g. dialog gain. */
#define AC4ANALYZE_GAIN_M_INF    (-120.0f)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief AC-4 bitstream channel modes. (Definition matches ac4toc_input_channel_mode)
 */
typedef enum
{
     AC4ANALYZE_CHANNEL_MODE_UNDEFINED  = 0            /**< Indicates an undefined/invalid channel mode. */
    ,AC4ANALYZE_CHANNEL_MODE_MONO       = 0x0002       /**< 0b10           ch_mode ==  0 */
    ,AC4ANALYZE_CHANNEL_MODE_STEREO     = 0x0004       /**< 0b100          ch_mode ==  1 */
    ,AC4ANALYZE_CHANNEL_MODE_IMS_ALL    = 0x0008       /**< 0b1000         ch_mode ==  1 */
    ,AC4ANALYZE_CHANNEL_MODE_IMS_ATMOS  = 0x000a       /**< 0b1010         ch_mode ==  1 */
    ,AC4ANALYZE_CHANNEL_MODE_3_0        = 0x0018       /**< 0b11000        ch_mode ==  2 */
    ,AC4ANALYZE_CHANNEL_MODE_5_0        = 0x001a       /**< 0b11010        ch_mode ==  3 */
    ,AC4ANALYZE_CHANNEL_MODE_5_1        = 0x001c       /**< 0b11100        ch_mode ==  4 */
    ,AC4ANALYZE_CHANNEL_MODE_70_34      = 0x00f0       /**< 0b11110000     ch_mode ==  5 */
    ,AC4ANALYZE_CHANNEL_MODE_71_34      = 0x00f2       /**< 0b11110010     ch_mode ==  6 */
    ,AC4ANALYZE_CHANNEL_MODE_70_52      = 0x00f4       /**< 0b11110100     ch_mode ==  7 */
    ,AC4ANALYZE_CHANNEL_MODE_71_52      = 0x00f6       /**< 0b11110110     ch_mode ==  8 */
    ,AC4ANALYZE_CHANNEL_MODE_70_322     = 0x00f8       /**< 0b11111000     ch_mode ==  9 */
    ,AC4ANALYZE_CHANNEL_MODE_71_322     = 0x00fa       /**< 0b11111010     ch_mode == 10 */
    ,AC4ANALYZE_CHANNEL_MODE_7_0_4      = 0x01f8       /**< 0b111111000    ch_mode == 11 */
    ,AC4ANALYZE_CHANNEL_MODE_7_1_4      = 0x01fa       /**< 0b111111010    ch_mode == 12 */
    ,AC4ANALYZE_CHANNEL_MODE_9_0_4      = 0x03f8       /**< 0b1111111000   ch_mode == 13 */
    ,AC4ANALYZE_CHANNEL_MODE_9_1_4      = 0x03fa       /**< 0b1111111010   ch_mode == 14 */
    ,AC4ANALYZE_CHANNEL_MODE_22_2       = 0x03fc       /**< 0b1111111100   ch_mode == 15 */
    ,AC4ANALYZE_CHANNEL_MODE_ESCAPE     = 0x03fe       /**< 0b1111111110                 */
} ac4analyze_channel_mode_t;

/**
 * @brief AC-4 bitstream analyzer: bitstream format.
 */
typedef enum
{
     AC4ANALYZE_BSFORMAT_RAW_AC4_FRAME        = 0x02    /**< Raw AC-4 frame. */
    ,AC4ANALYZE_BSFORMAT_AC4_SIMPLE_TRANSPORT = 0x03    /**< AC-4 simple transport stream elements. */
} ac4analyze_bsformat_t;

/**
 * @brief Return codes of the AC-4 bitstream analyzer.
 */
typedef enum
{
     AC4ANALYZE_OK                          = 0x00     /**< Success. */
    ,AC4ANALYZE_EOS                                    /**< End of segment, no further complete raw AC-4 frame available. */
    /* API usage error */
    ,AC4ANALYZE_ERROR_INVALID_HANDLE        = 0x10     /**< Invalid AC-4 bitstream analyzer handle. */
    ,AC4ANALYZE_ERROR_UNINITIALIZED_POINTER            /**< Uninitialized pointer. Pointer is zero. */
    ,AC4ANALYZE_ERROR_WRONG_BSFORMAT                   /**< Wrong input bitstream format. */
    ,AC4ANALYZE_ERROR_ILLEGAL_CONFIG                   /**< Illegal AC-4 analyzer configuration. */
    /* processing error */
    ,AC4ANALYZE_ERROR_ANALYZE               = 0x20     /**< AC-4 analyzer error. */
    ,AC4ANALYZE_ERROR_COMPONENT                        /**< Component (AC-4 framer or TOC reader) error. */
} ac4analyze_error_code;

/**
 * @brief Bitrate modes.
 */
typedef enum
{
     AC4ANALYZE_TOC_BR_MODE_UNDEFINED   = 0x00   /**< Undefined bitrate mode. */
    ,AC4ANALYZE_TOC_BR_MODE_VBR         = 0x02   /**< Variable bitrate. */
    ,AC4ANALYZE_TOC_BR_MODE_ABR         = 0x03   /**< Average bitrate. */
    ,AC4ANALYZE_TOC_BR_MODE_CBR         = 0x04   /**< Constant bitrate. */
} ac4analyze_toc_br_mode;

/**
 * @brief Bitrate estimation status.
 */
typedef enum
{
     AC4ANALYZE_TOC_BR_EST_AVAILABLE            = 0x00      /**< Bitrate estimate is available. */
    ,AC4ANALYZE_TOC_BR_EST_AVAILABLE_PREVIOUS   = 0x01      /**< Bitrate estimate is available based on information from previous frames. */
    ,AC4ANALYZE_TOC_BR_EST_UNAVAILABLE          = 0x02      /**< Bitrate estimate is not available. */
} ac4analyze_toc_br_est_status;

/**
 * @brief Presentation configuration.
 */
typedef enum
{
     AC4ANALYZE_PRESENTATION_CONFIG_UNDEFINED = 0                                /**< Undefined. */
    ,AC4ANALYZE_PRESENTATION_CONFIG_COMPLETE_MAIN                                /**< Complete Main. */
    ,AC4ANALYZE_PRESENTATION_CONFIG_MAIN_PLUS_DE                                 /**< Main + DE. */
    ,AC4ANALYZE_PRESENTATION_CONFIG_MAIN_PLUS_ASSOCIATE                          /**< Main + Associate. */
    ,AC4ANALYZE_PRESENTATION_CONFIG_MAIN_PLUS_DE_PLUS_ASSOCIATE                  /**< Main + DE + Associate. */
    ,AC4ANALYZE_PRESENTATION_CONFIG_MAIN_PLUS_HSF_EXT                            /**< Main + HSF ext. */
    ,AC4ANALYZE_PRESENTATION_CONFIG_MUSIC_AND_EFFECTS_PLUS_DIALOG                /**< Music and Effects + Dialog. */
    ,AC4ANALYZE_PRESENTATION_CONFIG_MUSIC_AND_EFFECTS_PLUS_DIALOG_PLUS_ASSOCIATE /**< Music and Effects + Dialog + Associate. */
    ,AC4ANALYZE_PRESENTATION_CONFIG_EMDF_ONLY                                    /**< EMDF only. */
    ,AC4ANALYZE_PRESENTATION_CONFIG_ARBITRARY_SUBSTREAM_GROUPS                   /**< PC5 - Arbitrary number of substream groups */
} ac4analyze_presentation_config_t;

/**
 * @brief Presentation type.
 *
 * @note The presentation type is not a bitstream element.
 */
typedef enum
{
     AC4ANALYZE_PRESENTATION_TYPE_UNDEFINED = 0     /**< Undefined. */
    ,AC4ANALYZE_PRESENTATION_TYPE_MAIN              /**< (Main only) or (Main + Dialog Enhancement) or (Music and Effects + Dialog),
                                                          no alternative presentation. */
    ,AC4ANALYZE_PRESENTATION_TYPE_ASSOC             /**< Associate only, no alternative presentation. */
    ,AC4ANALYZE_PRESENTATION_TYPE_MAIN_AND_ASSOC    /**< Associate with ((Main only) or (Main + Dialog Enhancement) or
                                                         (Music and Effects + Dialog)), no alternative presentation. */
    ,AC4ANALYZE_PRESENTATION_TYPE_ALTERNATIVE       /**< Any alternative presentation. */
} ac4analyze_presentation_type_t;

/**
 * @brief Content classifiers.
 */
typedef enum
{
     AC4ANALYZE_CONTENT_UNDEFINED = -1          /**< Unknown/undefined content. */
    ,AC4ANALYZE_CONTENT_CM = 0                  /**< Main audio service: complete main. */
    ,AC4ANALYZE_CONTENT_ME                      /**< Main audio service: music and effects. */
    ,AC4ANALYZE_CONTENT_VI                      /**< Associated service: visually impaired. */
    ,AC4ANALYZE_CONTENT_HI                      /**< Associated service: hearing impaired. */
    ,AC4ANALYZE_CONTENT_D                       /**< Associated service: dialog. */
    ,AC4ANALYZE_CONTENT_C                       /**< Associated service: commentary. */
    ,AC4ANALYZE_CONTENT_E                       /**< Associated service: emmergency. */
    ,AC4ANALYZE_CONTENT_VO                      /**< Associated service: voice over. */
} ac4analyze_content_classifier_t;

/**
 * @brief Program ID availability.
 */
typedef enum
{
     AC4ANALYZE_TOC_PROGRAM_ID_NOT_AVAILABLE = 0        /**< No Program Identifier is available. */
    ,AC4ANALYZE_TOC_PROGRAM_SHORT_ID_AVAILABLE          /**< Only Short Program Identifier is available. */
    ,AC4ANALYZE_TOC_PROGRAM_SHORT_AND_UUID_AVAILABLE    /**< Both Short and Universally Unique Program Identifier are available. */
} ac4analyze_programid_availability_t;

/**
 * @brief AC-4 substream type.
 */
typedef enum
{
     AC4ANALYZE_SUBSTREAM_TYPE_UNDEFINED = 0        /**< Substream type is undefined. */
    ,AC4ANALYZE_SUBSTREAM_TYPE_MAIN                 /**< Substream type is main. */
    ,AC4ANALYZE_SUBSTREAM_TYPE_DIALOG               /**< Substream type is dialog. */
    ,AC4ANALYZE_SUBSTREAM_TYPE_ASSOCIATE            /**< Substream type is associate. */
    ,AC4ANALYZE_SUBSTREAM_TYPE_DE                   /**< Substream type is dialog enhancement. */
} ac4analyze_substream_type_t;

/**
 * @brief Substream content, describing what kind of audio data is carried within a substream.
 */
typedef enum
{
     AC4ANALYZE_SUBSTREAM_CONTENT_UNDEFINED = 0   /**< Undefined state. */
    ,AC4ANALYZE_SUBSTREAM_CONTENT_CHANNELS        /**< Substream contains channel signals only.
                                               Channel signals are connected to a specific channel/speaker configuration. */
    ,AC4ANALYZE_SUBSTREAM_CONTENT_DYNAMIC_OBJECTS /**< Substream contains dynamic objects, but with the exception that it may contain
                                               bed objects as well. For a non-JOC configuration these bed objects may consist of
                                               one LFE only, whereas a JOC configuration can contain several bed objects.
                                               These bed objects are signalled by the bed channel assignment mask.
                                               Dynamic objects are transmitted in individual audio signals and rendered using
                                               positional/gain metadata from the bitstream. */
    ,AC4ANALYZE_SUBSTREAM_CONTENT_BED_OBJECTS     /**< Substream contains bed/static objects only.
                                               Bed objects are transmitted as individual audio signals and mapped to a
                                               specific channel/speaker configuration using gain metadata from the bitstream. */
    ,AC4ANALYZE_SUBSTREAM_CONTENT_ISF_OBJECTS     /**< Substream contains ISF (Intermediate Spatial Format) objects only.
                                               ISF objects are transmitted as individual audio signals and rendered to a
                                               specific channel/speaker configuration. */
    ,AC4ANALYZE_SUBSTREAM_CONTENT_JOC_STATIC_DMX  /**< The substream contains Joint Object Coded objects as static downmix. */
    ,AC4ANALYZE_SUBSTREAM_CONTENT_JOC_ADAPT_DMX   /**< The substream contains Joint Object Coded objects as adaptive downmix. */
} ac4analyze_substream_content_t;

/**
* @brief Sampling frequency index.
*/
typedef enum
{
     AC4ANALYZE_FS_INDEX_44100 = 0          /**< Base sampling frequency of 44,100 Hz */
    ,AC4ANALYZE_FS_INDEX_48000              /**< Base sampling frequency of 48,000 Hz */
} ac4analyze_fs_index_t;


/**
 * @brief Loudness practice type.
 */
typedef enum
{
     AC4ANALYZE_LOUD_PRAC_TYPE_NOT_INDICATED = 0        /**< Loudness regulation compliance not indicated. */
    ,AC4ANALYZE_LOUD_PRAC_TYPE_ATSC_A85                 /**< Programme loudness measured according to ATSC A/85. */
    ,AC4ANALYZE_LOUD_PRAC_TYPE_EBU_R128                 /**< Programme loudness measured according to EBU R128. */
    ,AC4ANALYZE_LOUD_PRAC_TYPE_ARIB_TR_B32              /**< Programme loudness measured according to ARIB TR-B32. */
    ,AC4ANALYZE_LOUD_PRAC_TYPE_FREETV_OP_59             /**< Programme loudness measured according to FreeTV OP-59. */
    ,AC4ANALYZE_LOUD_PRAC_TYPE_RESERVED                 /**< Reserved (first reserved value). */
    ,AC4ANALYZE_LOUD_PRAC_TYPE_RESERVED_LAST = 13       /**< Reserved (last reserved value). */
    ,AC4ANALYZE_LOUD_PRAC_TYPE_MANUAL = 14              /**< Manual. */
    ,AC4ANALYZE_LOUD_PRAC_TYPE_CONSUMER_LEVELLER        /**< Consumer leveller. */
} ac4analyze_loud_prac_t;

/**
* @brief Frame rate index.
*/
typedef enum
{
     AC4ANALYZE_FRAME_RATE_INDEX_23_976 = 0             /**< Rate of 23.976 frames per second */
    ,AC4ANALYZE_FRAME_RATE_INDEX_24                     /**< Rate of 24 frames per second */
    ,AC4ANALYZE_FRAME_RATE_INDEX_25                     /**< Rate of 25 frames per second */
    ,AC4ANALYZE_FRAME_RATE_INDEX_29_97                  /**< Rate of 29.97 frames per second */
    ,AC4ANALYZE_FRAME_RATE_INDEX_30                     /**< Rate of 30 frames per second */
    ,AC4ANALYZE_FRAME_RATE_INDEX_47_95                  /**< Rate of 47.95 frames per second */
    ,AC4ANALYZE_FRAME_RATE_INDEX_48                     /**< Rate of 48 frames per second */
    ,AC4ANALYZE_FRAME_RATE_INDEX_50                     /**< Rate of 50 frames per second */
    ,AC4ANALYZE_FRAME_RATE_INDEX_59_94                  /**< Rate of 59.94 frames per second */
    ,AC4ANALYZE_FRAME_RATE_INDEX_60                     /**< Rate of 60 frames per second */
    ,AC4ANALYZE_FRAME_RATE_INDEX_100                    /**< Rate of 100 frames per second */
    ,AC4ANALYZE_FRAME_RATE_INDEX_119_88                 /**< Rate of 119.88 frames per second */
    ,AC4ANALYZE_FRAME_RATE_INDEX_120                    /**< Rate of 120 frames per second */
    ,AC4ANALYZE_FRAME_RATE_INDEX_23_44                  /**< Rate of 23.44 frames per second */
    ,AC4ANALYZE_FRAME_RATE_INDEX_RESERVED_1             /**< Reserved frame rate */
    ,AC4ANALYZE_FRAME_RATE_INDEX_RESERVED_2 = 15        /**< Reserved frame rate */
} ac4analyze_frame_rate_index_t;

/**
 * @brief Dialog gating practice type.
 */
typedef enum
{
     AC4ANALYZE_DIALGATE_PRAC_TYPE_UNDEFINED = -1       /**< Dialog gating method - Not in bitstream. */
    ,AC4ANALYZE_DIALGATE_PRAC_TYPE_NOT_INDICATED = 0    /**< Dialog gating method - Not Indicated. */
    ,AC4ANALYZE_DIALGATE_PRAC_TYPE_AUTOMATED_C_OR_L_R   /**< Dialog gating method - Automated Center or Left+Right Channel(s). */
    ,AC4ANALYZE_DIALGATE_PRAC_TYPE_AUTOMATED_L_C_R      /**< Dialog gating method - Automated Left, Center, and/or Right Channel(s). */
    ,AC4ANALYZE_DIALGATE_PRAC_TYPE_MANUAL               /**< Dialog gating method - Manual Selection. */
    ,AC4ANALYZE_DIALGATE_PRAC_TYPE_RESERVED             /**< Reserved (first reserved value). */
    ,AC4ANALYZE_DIALGATE_PRAC_TYPE_RESERVED_LAST = 7    /**< Reserved (last reserved value). */
} ac4analyze_dialgate_prac_t;

/**
 * @brief Loudness range measurement practice type.
 */
typedef enum
{
     AC4ANALYZE_LRA_PRAC_TYPE_EBU_TECH_3342_V1 = 0      /**< Loudness Range as per EBU Tech 3342 v1. */
    ,AC4ANALYZE_LRA_PRAC_TYPE_EBU_TECH_3342_V2          /**< Loudness Range as per EBU Tech 3342 v2. */
    ,AC4ANALYZE_LRA_PRAC_TYPE_RESERVED                  /**< Reserved (first reserved value). */
    ,AC4ANALYZE_LRA_PRAC_TYPE_RESERVED_LAST = 7         /**< Reserved (last reserved value). */
} ac4analyze_lra_prac_t;

/**
 * @brief Stereo downmix gain.
 */
typedef enum
{
     AC4ANALYZE_STEREO_DMXGAIN_UNDEFINED    = -1        /**< Not in bitstream. */
    ,AC4ANALYZE_STEREO_DMXGAIN_PLUS_3_0_DB  = 0         /**< +3.0 dB. */
    ,AC4ANALYZE_STEREO_DMXGAIN_PLUS_1_5_DB              /**< +1.5 dB. */
    ,AC4ANALYZE_STEREO_DMXGAIN_0_DB                     /**<  0.0 dB. */
    ,AC4ANALYZE_STEREO_DMXGAIN_MINUS_1_5_DB             /**< -1.5 dB. */
    ,AC4ANALYZE_STEREO_DMXGAIN_MINUS_3_0_DB             /**< -3.0 dB. */
    ,AC4ANALYZE_STEREO_DMXGAIN_MINUS_4_5_DB             /**< -4.5 dB. */
    ,AC4ANALYZE_STEREO_DMXGAIN_MINUS_6_0_DB             /**< -6.0 dB. */
    ,AC4ANALYZE_STEREO_DMXGAIN_MINUS_INF_DB             /**< -inf dB. */
} ac4analyze_stereo_dmxgain_t;

/**
 * @brief Preferred downmix method.
 */
typedef enum
{
     AC4ANALYZE_PREFERRED_DOWNMIX_METHOD_UNDEFINED      = -1 /**< Not in bitstream. */
    ,AC4ANALYZE_PREFERRED_DOWNMIX_METHOD_NOT_INDICATED  = 0  /**< Not Indicated. */
    ,AC4ANALYZE_PREFERRED_DOWNMIX_METHOD_LORO                /**< LoRo downmix. */
    ,AC4ANALYZE_PREFERRED_DOWNMIX_METHOD_LTRT                /**< LtRt downmix. */
    ,AC4ANALYZE_PREFERRED_DOWNMIX_METHOD_PL_II               /**< ProLogic II downmix. */
} ac4analyze_preferred_downmix_method_t;

/**
 * @brief DRC profile.
 */
typedef enum
{
     AC4ANALYZE_DRC_PROFILE_NONE = 0               /**< No profile selected. */
    ,AC4ANALYZE_DRC_PROFILE_FILM_STANDARD          /**< Film standard profile. */
    ,AC4ANALYZE_DRC_PROFILE_FILM_LIGHT             /**< Film light profile. */
    ,AC4ANALYZE_DRC_PROFILE_MUSIC_STANDARD         /**< Music standard profile. */
    ,AC4ANALYZE_DRC_PROFILE_MUSIC_LIGHT            /**< Music light profile. */
    ,AC4ANALYZE_DRC_PROFILE_SPEECH                 /**< Speech profile. */
    ,AC4ANALYZE_DRC_PROFILE_RESERVED               /**< Reserved (first reserved value). */
    ,AC4ANALYZE_DRC_PROFILE_RESERVED_LAST          /**< Reserved (last reserved value). */
} ac4analyze_drc_profile_t;

/**
 * @brief DRC decoder modes.
 */
typedef enum
{
     AC4ANALYZE_DRC_DECODER_MODE_HOME_THEATRE = 0       /**< Home Theatre. */
    ,AC4ANALYZE_DRC_DECODER_MODE_FLAT_PANEL_TV          /**< Flat panel TV. */
    ,AC4ANALYZE_DRC_DECODER_MODE_PORTABLE_SPEAKERS      /**< Portable - Speakers. */
    ,AC4ANALYZE_DRC_DECODER_MODE_PORTABLE_HEADPHONES    /**< Portable - Headphones. */
    ,AC4ANALYZE_DRC_DECODER_MODE_CUSTOM_1               /**< Reserved; 1st custom mode. */
    ,AC4ANALYZE_DRC_DECODER_MODE_CUSTOM_2               /**< Reserved; 2nd custom mode. */
    ,AC4ANALYZE_DRC_DECODER_MODE_CUSTOM_3               /**< Reserved; 3rd custom mode. */
    ,AC4ANALYZE_DRC_DECODER_MODE_CUSTOM_4               /**< Reserved; 4th custom mode. */
} ac4analyze_drc_decoder_mode_t;

/**
 * @brief Downmix to 2 channels configurations.
 */
typedef enum
{
     AC4ANALYZE_DMX_CONFIG_2CH_UNDEFINED = -1       /**< Undefined downmix configuration. */
    ,AC4ANALYZE_DMX_CONFIG_2CH_UNKNOWN = 0          /**< Unknown downmix configuration. */
    ,AC4ANALYZE_DMX_CONFIG_2CH_LO_RO_SIMPLE         /**< LoRo/simple downmix. */
    ,AC4ANALYZE_DMX_CONFIG_2CH_PL                   /**< ProLogic downmix. */
    ,AC4ANALYZE_DMX_CONFIG_2CH_PLII                 /**< ProLogic II downmix. */
    ,AC4ANALYZE_DMX_CONFIG_2CH_RESERVED             /**< Reserved (first reserved value). */
    ,AC4ANALYZE_DMX_CONFIG_2CH_RESERVED_LAST = 7    /**< Reserved (last reserved value). */
} ac4analyze_dmx_config_2ch_t;

/**
 * @brief Downmix gains for height channels.
 */
typedef enum
{
     AC4ANALYZE_CDMX_GAIN_0_0_DB
    ,AC4ANALYZE_CDMX_GAIN_M_1_5_DB
    ,AC4ANALYZE_CDMX_GAIN_M_3_0_DB
    ,AC4ANALYZE_CDMX_GAIN_M_4_5_DB
    ,AC4ANALYZE_CDMX_GAIN_M_6_0_DB
    ,AC4ANALYZE_CDMX_GAIN_M_9_0_DB
    ,AC4ANALYZE_CDMX_GAIN_M_12_0_DB
    ,AC4ANALYZE_CDMX_GAIN_M_INF_DB
}ac4analyze_cdmx_gain_t;

/**
 * @brief Downmix targets for height channels.
 * 
 * Note: The values are reversed from those in the encoder (the encoder authors 1-bit indicating top-to-front)
 */
typedef enum
{
     AC4ANALYZE_CDMX_TARGET_HEIGHT_CHANNELS_LS_RS
    ,AC4ANALYZE_CDMX_TARGET_HEIGHT_CHANNELS_L_R
}ac4analyze_cdmx_target_height_channels_t;
/**
 * @brief Phase 90 shift configurations.
 */
typedef enum
{
     AC4ANALYZE_PHASE90_NOT_INDICATED = 0   /**< Not Indicated. */
    ,AC4ANALYZE_PHASE90_RESERVED            /**< Reserved. */
    ,AC4ANALYZE_PHASE90_APPLIED             /**< Applied. */
    ,AC4ANALYZE_PHASE90_NOT_APPLIED         /**< Not Applied. */
} ac4analyze_phase90_info_t;

/**
 * @brief DC blocking configurations.
 */
typedef enum
{
     AC4ANALYZE_DC_BLOCKING_CONFIG_NOT_INDICATED = 0  /**< Not Indicated. */
    ,AC4ANALYZE_DC_BLOCKING_CONFIG_DISABLED           /**< Disabled. */
    ,AC4ANALYZE_DC_BLOCKING_CONFIG_ENABLED            /**< Enabled. */
} ac4analyze_dc_blocking_info_t;

/**
 * @brief Downmix to 5 channels configurations.
 */
typedef enum
{
     AC4ANALYZE_DMX_CONFIG_5CH_UNDEFINED     = -1   /**< Undefined downmix configuration. */
    ,AC4ANALYZE_DMX_CONFIG_PL_II_X           = 0    /**< Mix Down PLIIX Surround. */
    ,AC4ANALYZE_DMX_CONFIG_PL_II_X_MOVIE            /**< Mix Down PLIIx Movie Surround. */
    ,AC4ANALYZE_DMX_CONFIG_PL_II_X_MUSIC            /**< Mix Down PLIIx Music Surround. */
    ,AC4ANALYZE_DMX_CONFIG_PL_II_Z                  /**< Mix Down PLIIz Surround. */
    ,AC4ANALYZE_DMX_CONFIG_5CH_RESERVED             /**< Reserved (first reserved value). */
    ,AC4ANALYZE_DMX_CONFIG_5CH_RESERVED_LAST = 7    /**< Reserved (last reserved value). */
} ac4analyze_dmx_config_5ch_t;

/**
 * @brief Upmix to 5 channels configurations.
 */
typedef enum
{
     AC4ANALYZE_UMX_CONFIG_5CH_UNDEFINED      = -1 /**< Undefined upmix configuration. */
    ,AC4ANALYZE_UMX_CONFIG_5CH_PL             = 0  /**< ProLogic upmix. */
    ,AC4ANALYZE_UMX_CONFIG_5CH_PL_II_MOVIE         /**< ProLogic II Movie Mode upmix. */
    ,AC4ANALYZE_UMX_CONFIG_5CH_PL_II_MUSIC         /**< ProLogic II Music Mode upmix. */
    ,AC4ANALYZE_UMX_CONFIG_5CH_PROFESSIONAL        /**< Dolby Professional upmixer. */
    ,AC4ANALYZE_UMX_CONFIG_5CH_RESERVED            /**< Reserved (first reserved value). */
    ,AC4ANALYZE_UMX_CONFIG_5CH_RESERVED_LAST = 15  /**< Reserved (last reserved value). */
} ac4analyze_umx_config_5ch_t;

/**
 * @brief Upmix to 7 channels configurations.
 */
typedef enum
{
     AC4ANALYZE_UMX_CONFIG_7CH_UNDEFINED      = -1 /**< Undefined upmix configuration. */
    ,AC4ANALYZE_UMX_CONFIG_7CH_PL_II_X_MOVIE  = 0  /**< ProLogic IIx Movie Mode upmix. */
    ,AC4ANALYZE_UMX_CONFIG_7CH_PL_II_X_MUSIC       /**< ProLogic IIx Music Mode upmix. */
    ,AC4ANALYZE_UMX_CONFIG_7CH_PL_II_Z_HEIGHT      /**< ProLogic IIz Height upmix. */
    ,AC4ANALYZE_UMX_CONFIG_7CH_RESERVED            /**< Reserved. */
} ac4analyze_umx_config_7ch_t;

/**
 * @brief Dialog pan signal selector for 3_0_channel_element.
 */
typedef enum
{
     AC4ANALYZE_PAN_SIGNAL_SELECT_ALL_3         = 0 /**< Dialog in all 3 tracks. */
    ,AC4ANALYZE_PAN_SIGNAL_SELECT_TRACK_1_AND_2     /**< Pan signal in track 1 and track 2. */
    ,AC4ANALYZE_PAN_SIGNAL_SELECT_TRACK_0_AND_2     /**< Pan signal in track 0 and track 2. */
    ,AC4ANALYZE_PAN_SIGNAL_SELECT_TRACK_0_AND_1     /**< Pan signal in track 0 and track 1. */
} ac4analyze_pan_signal_selector_t;

/**
 * @brief Dialog enhancement method.
 */
typedef enum
{
     AC4ANALYZE_DE_METHOD_UNDEFINED             = -1  /**< Undefined DE method. */
    ,AC4ANALYZE_DE_METHOD_CHANNEL_INDEPENDENT   = 0   /**< Channel independent dialog enhancement. */
    ,AC4ANALYZE_DE_METHOD_CROSS_CHANNEL               /**< Cross-channel dialog enhancement. */
    ,AC4ANALYZE_DE_METHOD_HYBRID_CHANNEL_INDEPENDENT  /**< Waveform-parametric hybrid; channel independent enhancement and waveform channel. */
    ,AC4ANALYZE_DE_METHOD_HYBRID_CROSS_CHANNEL        /**< Waveform-parametric hybrid; cross-channel enhancement and waveform channel. */
} ac4analyze_de_method_t;

/**
 * @brief Dialog enhancement channel configuration.
 */
typedef enum
{
     AC4ANALYZE_DE_CHANNEL_CONFIG_NO_PARAMETERS = 0  /**< No parameters. */
    ,AC4ANALYZE_DE_CHANNEL_CONFIG_CENTER             /**< Center. */
    ,AC4ANALYZE_DE_CHANNEL_CONFIG_RIGHT              /**< Right. */
    ,AC4ANALYZE_DE_CHANNEL_CONFIG_RIGHT_CENTER       /**< Right, center. */
    ,AC4ANALYZE_DE_CHANNEL_CONFIG_LEFT               /**< Left. */
    ,AC4ANALYZE_DE_CHANNEL_CONFIG_LEFT_CENTER        /**< Left, center. */
    ,AC4ANALYZE_DE_CHANNEL_CONFIG_LEFT_RIGHT         /**< Left, right. */
    ,AC4ANALYZE_DE_CHANNEL_CONFIG_LEFT_RIGHT_CENTER  /**< Left, right, center. */
} ac4analyze_de_channel_config_t;

/**
 * @brief AC-4 sync word in AC-4 simple transport.
 */
typedef enum
{
     AC4ANALYZE_SYNC_WORD_UNDEFINED = 0         /**< Undefined AC-4 sync word. */
    ,AC4ANALYZE_SYNC_WORD_AC40      = 0xAC40    /**< 'AC40' (no CRC). */
    ,AC4ANALYZE_SYNC_WORD_AC41      = 0xAC41    /**< 'AC41' (with CRC). */
} ac4analyze_ac4_sync_word_t;

/**
 * @brief Top channel configuration
 */
typedef enum
{
     AC4ANALYZE_TOP_CHANNELS_NOT_PRESENT = 0         /**< Tfl, Tfr, Tbl, Tbr contain silence */
    ,AC4ANALYZE_TOP_CHANNELS_TFL_TFR                 /**< Original content of Tsl, Tsr is carried in Tfl, Tfr.
                                                          Tbl, Tbr contain silence. */
    ,AC4ANALYZE_TOP_CHANNELS_TBL_TBR                 /**< Original content of Tsl, Tsr is carried in Tbr, Tbl.
                                                          Tfl, Tfr contain silence. */
    ,AC4ANALYZE_TOP_CHANNELS_TFL_TFR_TBL_TBR         /*< Tfl, Tfr, Tbl, Tbr contain original content*/
} ac4analyze_top_channels_present_t;

/**
 * @brief Dolby Atmos indicator
 */
typedef enum
{
     AC4ANALYZE_DOLBY_ATMOS_INDICATOR_NONE  = -1     /**< No b_dolby_atmos_indicator bit not exists */
    ,AC4ANALYZE_DOLBY_ATMOS_INDICATOR_FALSE =  0     /**< No b_dolby_atmos_indicator bit is not set */
    ,AC4ANALYZE_DOLBY_ATMOS_INDICATOR_TRUE  =  1     /**< No b_dolby_atmos_indicator bit is set */
} ac4analyze_dolby_atmos_indicator_t;
    
/**
 * @brief Presence of top channels
 */
typedef enum
{
     AC4ANALYZE_PRES_TOP_CHANNEL_PAIRS_NONE = 0
    ,AC4ANALYZE_PRES_TOP_CHANNEL_PAIRS_ONE  = 1
    ,AC4ANALYZE_PRES_TOP_CHANNEL_PAIRS_TWO  = 2
} pres_top_channel_pairs_t;

/**
 * @brief AC-4 frame data status.
 */
typedef enum
{
     AC4ANALYZE_FRAME_STATUS_OK                             = 0x0000    /**< Frame parsed without any issue. */

     /* frame analyze OK - additional status information */
    ,AC4ANALYZE_FRAME_STATUS_SEQUENCE_COUNTER_INVALID       = 0x0001    /**< Invalid sequence counter. */
    ,AC4ANALYZE_FRAME_STATUS_SOURCE_CHANGE_DETECTED         = 0x0002    /**< Source change detected. */
    ,AC4ANALYZE_FRAME_STATUS_UNSUPPORTED_PRESENTATIONS      = 0x0004    /**< Number of presentations > AC4ANALYZE_MAX_NUM_SUPPORTED_PRESENTATIONS.
                                                                            Only AC4ANALYZE_MAX_NUM_SUPPORTED_PRESENTATIONS are reported. */
    ,AC4ANALYZE_FRAME_STATUS_UNSUPPORTED_EHFR               = 0x0008    /**< EHFR mode is unsupported. Substream metadata is not evaluated. */
    ,AC4ANALYZE_FRAME_STATUS_UNSUPPORTED_EMDF_INFO          = 0x0010    /**< Number of EMDF info entries > AC4ANALYZE_MAX_NUM_SUPPORTED_EMDF_INFO.
                                                                            No EMDF info is reported. */
    ,AC4ANALYZE_FRAME_STATUS_UNSUPPORTED_EMDF_PAYLOADS      = 0x0020    /**< Number of EMDF payloads > AC4ANALYZE_MAX_NUM_SUPPORTED_EMDF_PAYLOADS.
                                                                            Only AC4ANALYZE_MAX_NUM_SUPPORTED_EMDF_PAYLOADS are reported. */
    ,AC4ANALYZE_FRAME_STATUS_UNSUPPORTED_OAMD               = 0x0040    /**< OAMD related parsing is not supported.
                                                                            Substream metadata is not completely evaluated. */
    ,AC4ANALYZE_FRAME_STATUS_UNSUPPORTED_SUBSTREAM_IDX      = 0x0080    /**< Audio substream present with substream index >=
                                                                            AC4ANALYZE_MAX_NUM_SUPPORTED_SUBSTREAMS.
                                                                            No substream_data is reported for such audio substreams. */
    ,AC4ANALYZE_FRAME_STATUS_NO_DRC_CONFIG                  = 0x0100    /**< No DRC config received. DRC data is present but cannot be parsed.
                                                                            DE substream metadata is not evaluated. */

    ,AC4ANALYZE_FRAME_STATUS_FIRST_FRAME_NOT_IFRAME         = 0x0200    /**< The first frame of the bitstream is not an iframe */
    ,AC4ANALYZE_FRAME_STATUS_INVALID_LANGUAGE_TAG           = 0x0400    /**< Invalid language tag detected */
    ,AC4ANALYZE_FRAME_STATUS_UNSUPPORTED_SUBSTREAM_GROUP    = 0x0800    /**< Audio substream group present with substream group index >=
                                                                            AC4ANALYZE_MAX_NUM_SUPPORTED_SUBSTREAM_GROUPS.
                                                                            No substream_group_data is reported. */
    /* frame analyze ERROR - additional status information */
    ,AC4ANALYZE_FRAME_STATUS_HUFFMAN_ERROR                  = 0x1000    /**< Huffman decoding error. */
    ,AC4ANALYZE_FRAME_STATUS_BS_BUFFER_UNDERRUN             = 0x2000    /**< Bitstream buffer underrun while parsing. Bitstream data is corrupt. */
    ,AC4ANALYZE_FRAME_STATUS_BS_PARSE_ERROR                 = 0x3000    /**< Bitstream parser entered an illegal state. Bitstream data is corrupt. */
    ,AC4ANALYZE_FRAME_STATUS_UNSUPPORTED_SUBSTREAMS         = 0x4000    /**< Number of substreams > MAX_NUM_SUPPORTED_SUBSTREAMS. */
    ,AC4ANALYZE_FRAME_STATUS_TOC_ILLEGAL_CONFIG             = 0x5000    /**< Illegal TOC configuration. */
    ,AC4ANALYZE_FRAME_STATUS_TOC_UNSUPPORTED_CONFIG         = 0x6000    /**< Unsupported TOC configuration. */
    ,AC4ANALYZE_FRAME_STATUS_TOC_ERROR                      = 0x7000    /**< TOC (parsing) error. */
} ac4analyze_frame_status_t;

/**
 * @brief Version definition for the AC-4 bitstream analyzer.
 */
typedef struct ac4analyze_version_info_s
{
    int                 v_api;                      /**< API version. */
    int                 v_fct;                      /**< Functional change. */
    int                 v_mtnc;                     /**< Maintenance release. */
    const char         *extension;                  /**< Version extension. */
    const char         *text;                       /**< Description / build number. */
} ac4analyze_version_info_t;

/**
 * @brief Information about the content type in the substream.
 */
typedef struct ac4analyze_bs_content_type_s
{
    int           content_classifier;                                       /**< Content classifier (value from @ref ac4analyze_content_classifier_t). */
    unsigned int  b_language_indicator;                                     /**< Flag indicates that programme language indication data is available. */
    unsigned int  b_language_tag_start;                                     /**< Flag indicates that programme language indication data has started. */
    unsigned int  b_serialized_language_tag;                                /**< Flag indicates that programme language indication data is serialized. */
    size_t        n_language_tag_bytes;                                     /**< Length of language tag in bytes. */
    unsigned char language_tag_bytes[AC4ANALYZE_MAX_LANGUAGE_TAG_BYTES];    /**< Language tag that conforms to the syntax and semantics
                                                                                 defined in IETF BCP 47. */
} ac4analyze_bs_content_type_t;

/**
 * @brief Information about loudness in the AC-4 bitstream.
 */
typedef struct ac4analyze_loudness_info_s
{
    /* Further Loudness Information */

    int             loudness_version;             /**< Indicates the version of the loudness payload.
                                                       - -1:      undefined
                                                       - 0 .. 18: loudness_version */
    unsigned int    loud_prac_type;               /**< Indicates which practice for programme loudness measurement has been
                                                       followed to generate the programme loudness (value from @ref ac4analyze_loud_prac_t). */
    unsigned int    b_loudcorr_dialgate_valid;    /**< Indicates whether b_loudcorr_dialgate is contained in the bitstream (0/1). */
    unsigned int    b_loudcorr_dialgate_value;    /**< Indicates whether the loudness of the programme has been corrected using
                                                       dialog gating (0/1). The bitstream value b_loudcorr_dialgate is optional. */
    int             dialgate_prac_type;           /**< Indicates which dialog gating method has been utilized for loudness
                                                       correction (value from @ref ac4analyze_dialgate_prac_t). */
    unsigned int    b_loudcorr_type;              /**< Indicates if the loudness of the programme has been corrected with:
                                                       - 0: an infinite look-ahead (file-based)
                                                       - 1: a realtime loudness measurement */
    unsigned int    b_loudrelgat;                 /**< Indicates the presence of the loudrelgat entry (0/1). */
    float           loudrelgat;                   /**< Loudness value relative gated: Indicates the integrated loudness of the
                                                       audio programme, measured according to ITU-R BS.1770-3.
                                                       - -102.4 .. 102.3 (LUFS) */
    unsigned int    b_loudspchgat;                /**< Indicates the presence of the loudspchgat entry (0/1). */
    float           loudspchgat;                  /**< Loudness value speech gated: Indicates the integrated loudnesss of the
                                                       audio programme, measured according to formula 2 of ITU-R BS.1770-3.
                                                       - -102.4 .. 102.3 (LUFS) */
    int             dialgate_prac_type_spchgat;   /**< Indicates which dialog gating method has been utilized for speech-based
                                                       loudness measurement of the programme (value from @ref ac4analyze_dialgate_prac_t). */
    unsigned int    b_loudstrm3s;                 /**< Indicates the presence of the loudstrm3s entry (0/1). */
    float           loudstrm3s;                   /**< Loudness value short term 3s: Indicates the ungated loudness of the
                                                       preceding 3 seconds of the audio programme, measured according to
                                                       ITU-R BS.1770-3.
                                                       - -102.4 .. 102.3 (LUFS) */
    unsigned int    b_max_loudstrm3s;             /**< Indicates the presence of the max_loudstrm3s entry (0/1). */
    float           max_loudstrm3s;               /**< Max loudness value short term 3s: Indicates the maximum short-term loudness
                                                       of the audio programme, measured according to ITU-R BS.1771 and without any
                                                       gain adjustments due to dialnorm and dynamic range compression being applied.
                                                       - -102.4 .. 102.3 (LUFS) */
    unsigned int    b_truepk;                     /**< Indicates the presence of the truepk entry (0/1). */
    float           truepk;                       /**< True peak value: Indicates the true-peak sample value of the programme, 
                                                       measured according to ITU-R BS.1771-1.
                                                       - -102.4 .. 102.3 (LUFS) */
    unsigned int    b_max_truepk;                 /**< Indicates the presence of the max_truepk entry (0/1). */
    float           max_truepk;                   /**< Max true peak: Indicates the maximum true peak sample value of the programme
                                                       measured according to Annex 2 of recommendation ITU-R BS.1770.
                                                       - -102.4 .. 102.3 (LUFS) */
    unsigned int    b_loudmntry;                  /**< Indicates the presence of the loudmntry entry (0/1). */
    float           loudmntry;                    /**< Momentary loudness: Indicates the momentary loudness of the programme
                                                       measured since the previous value was sent in the bitstream.
                                                       Measurement is specified in ITU-R BS.1771 (or EBU Tech 3341).
                                                       - -102.4 .. 102.3 (LUFS) */
    unsigned int    b_max_loudmntry;              /**< Indicates the presence of the max_loudmntry entry (0/1). */
    float           max_loudmntry;                /**< Max momentary loudness: Indicates the maximum momentary loudness of the
                                                       programme measured as specified in ITU-R BS.1771 (or EBU Tech 3341) without
                                                       any gain adjustments due to dialnorm and dynamic range compression applied.
                                                       - -102.4 .. 102.3 (LUFS) */
    unsigned int    prgmbndy;                     /**< Programme boundary: Indicates the number of frames between the current frame
                                                       and the frame that contains the boundary between two different programmes.
                                                       - 0:        undefined
                                                       - 2 .. 512: prgmbndy */
    unsigned int    b_end_or_start;               /**< Programme boundary end or start: Indicates if the prgmbndy field indicates
                                                       the number of frames between:
                                                       - 0: the current frame and the past frame that contained the previous
                                                            programme boundary
                                                       - 1: the current frame and the upcoming fram that contains the next
                                                            programme boundary */
    int             prgmbndy_offset;              /**< Programme boundary offset: Indicates the offset in audio samples from the
                                                       first sample of the frame indicated by the prgmbndy variable to the actual
                                                       audio sample in that frame that corresponds to the programme boundary.
                                                       - -1:        undefined
                                                       - 0 .. 2047: prgmbndy_offset */
    unsigned int    b_lra;                        /**< Indicates the presence of the lra entry (0/1). */
    float           lra;                          /**< Loudness range: Indicates the loudness range of the programme as specified
                                                       in EBU Tech Document 3342.
                                                       - 0 .. 102.3 (LU) */
    unsigned int    lra_prac_type;                /**< Loudness range measurement practice type: Indicates which method has been
                                                       utilized to compute the loudness range with the current audio substream
                                                       (value from @ref ac4analyze_lra_prac_t). */
    unsigned int    b_rtllcomp;                  /**< Indicates the presence of the rtll_comp entry (0/1). */
    float           rtll_comp_gain;              /**< Indicates the loudness correction factor due to RTLL */
    /* more info available (b_prgmbndy, which is not relevant for a bitstream_version==0 only analyzer) */

#if 0  /* The following elements are not part of this struct since their value can be derived from other elements in this struct */
    unsigned int    b_prgmbndy;                   /**< Indicates the presence of the prgmbndy entry (0/1).
                                                       = (prgmbndy != 0) */
    unsigned int    b_prgmbndy_offset;            /**< Indicates the presence of the prgmbndy_offset entry (0/1).
                                                       = (prgmbndy_offset != -1) */
#endif
} ac4analyze_loudness_info_t;

/**
 * @brief Information about stereo downmix in the AC-4 bitstream.
 */
typedef struct ac4analyze_stereo_dmx_info_s
{
    int             loro_center_mixgain;          /**< LoRo center mixgain (value from @ref ac4analyze_stereo_dmxgain_t).
                                                       - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    int             loro_surround_mixgain;        /**< LoRo surround mixgain (value from @ref ac4analyze_stereo_dmxgain_t).
                                                       - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    int             ltrt_center_mixgain;          /**< LtRt center mixgain (value from @ref ac4analyze_stereo_dmxgain_t).
                                                       - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    int             ltrt_surround_mixgain;        /**< LtRt surround mixgain (value from @ref ac4analyze_stereo_dmxgain_t).
                                                       - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    unsigned int    b_lfe_mixinfo;                /**< Indicates the presence of the lfe_mixgain entry (0/1). */
    float           lfe_mixgain;                  /**< LFE mixgain.
                                                       - -25.5 .. +5.5 dB
                                                       - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */

    int             preferred_dmx_method;         /**< Preferred downmix method (value from @ref ac4analyze_preferred_downmix_method_t).
                                                       - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */

#if 0  /* The following elements are not part of this struct since their value can be derived from other elements in this struct */
    unsigned int    b_stereo_dmx_coeff;           /**< Stereo downmix coefficients present flag (0/1).
                                                       = (loro_center_mixgain != STEREO_DMXGAIN_UNDEFINED) */
    unsigned int    b_ltrt_mixinfo;               /**< LtRt downmix info present (0/1).
                                                       = (ltrt_center_mixgain != STEREO_DMXGAIN_UNDEFINED) */
#endif
} ac4analyze_stereo_dmx_info_t;

typedef struct ac4analyze_cdmx_info_s
{
    unsigned int  target;                /**< Use ac4analyze_cdmx_target_height_channels_t */
    unsigned int  gain;                  /**< Use ac4analyze_cdmx_gain_t */
} ac4analyze_cdmx_info_t;

/**
 * @brief Information about 5.1.2 to 5.1 downmix coefficients in the AC-4 bitstream.
 */

typedef struct ac4analyze_cdmx_512_info_s
{
    ac4analyze_cdmx_info_t  top_middle;
} ac4analyze_cdmx_512_info_t;

/**
 * @brief Information about 5.1.4 to 5.1 downmix coefficients in the AC-4 bitstream.
 */

typedef struct ac4analyze_cdmx_514_info_s
{
    ac4analyze_cdmx_info_t  top_front;
    ac4analyze_cdmx_info_t  top_back;
} ac4analyze_cdmx_514_info_t;

/**
* @brief Information about 5.1.4 to 5.1.2 downmix coefficients in the AC-4 bitstream.
*/

typedef struct ac4analyze_cdmx_t4_to_t2_info_s
{
    unsigned int  gain;                  /**< Use ac4analyze_cdmx_gain_t */
} ac4analyze_cdmx_t4_to_t2_info_t;

/**
 * @brief Information about loudness correction in the AC-4 bitstream.
 */
typedef struct ac4analyze_loud_corr_info_s
{
    unsigned int    b_loro_dmx_loud_corr;       /**< Indicates the presence of the loro_dmx_loud_corr entry (0/1). */
    float           loro_dmx_loud_corr;         /**< LoRo downmix loudness correction gain.
                                                     - -7.5 .. 7.5 dB2
                                                     - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    unsigned int    b_loud_corr_core_loro;      /**< Indicates the presence of the loud_corr_core_loro entry (0/1). */
    float           loud_corr_core_loro;        /**< LoRo downmix loudness correction gain, core decoding.
                                                     - -7.5 .. 7.5 dB2
                                                     - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    unsigned int    b_ltrt_dmx_loud_corr;       /**< Indicates the presence of the ltrt_dmx_loud_corr entry (0/1). */
    float           ltrt_dmx_loud_corr;         /**< LtRt downmix loudness correction gain.
                                                     - -7.5 .. 7.5 dB2
                                                     - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    unsigned int    b_loud_corr_core_ltrt;      /**< Indicates the presence of the loud_corr_core_ltrt entry (0/1). */
    float           loud_corr_core_ltrt;        /**< LtRt downmix loudness correction gain, core decoding.
                                                     - -7.5 .. 7.5 dB2
                                                     - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    unsigned int    b_loud_corr_5_X;            /**< Indicates the presence of the loud_corr_5_X entry (0/1). */
    float           loud_corr_5_X;              /**< Loudness correction gain for 5_X downmixes.
                                                     - -7.5 .. 7.5 dB2
                                                     - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    unsigned int    b_loud_corr_5_X_2;          /**< Indicates the presence of the loud_corr_5_X_2 entry (0/1). */
    float           loud_corr_5_X_2;            /**< Loudness correction gain for 5_X_2 downmixes.
                                                     - -7.5 .. 7.5 dB2
                                                     - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
} ac4analyze_loud_corr_info_t;

/**
 * @brief Information about DRC decoder mode in the AC-4 bitstream.
 *
 * Depends on b_sticky_metadata in @ref ac4analyze_cfg_s.
 */
typedef struct ac4analyze_drc_mode_cfg_s
{
    unsigned int    drc_decoder_mode_id;        /**< DRC decoder mode ID (value from @ref ac4analyze_drc_decoder_mode_t). */
    int             drc_output_level_from;      /**< Lowest reference output level; only for custom DRC decoder modes.
                                                     - -31 .. 0 dB */
    int             drc_output_level_to;        /**< Highest reference output level; only for custom DRC decoder modes.
                                                     - -31 .. 0 dB */
    int             drc_repeat_id;              /**< DRC repeat ID.
                                                   - -1:     undefined
                                                   - 0 .. 7: drc_repeat_id */
    unsigned int    drc_default_profile_flag;   /**< DRC default_profile_flag (0/1). */
    unsigned int    drc_compression_curve_flag; /**< DRC compression curve flag (0/1). */
    unsigned int    drc_gains_config;           /**< DRC gains configuration.
                                                   - 0 .. 3: drc_gains_config */

    /* Parameterized DRC curve values */
    int             L_0_low;                    /**< L_0_low
                                                     - -15 .. 0 dB2 */
    unsigned int    L_0_high;                   /**< L_0_high
                                                     - 0 .. 15 dB2 */
    unsigned int    G_maxboost;                 /**< G_maxboost
                                                     - 0 .. 15 dB2 */
    int             L_maxboost;                 /**< L_maxboost
                                                     - -79 .. 0 dB2 */
    unsigned int    G_sectionboost;             /**< G_sectionboost
                                                     - 0: indicates (drc_nr_boost_sections == 0)
                                                     - 1 .. 16 dB2  */
    int             L_sectionboost;             /**< L_sectionboost
                                                     - -47 .. 0 dB2 */
    int             G_maxcut;                   /**< G_maxcut
                                                     - -31 .. 0 dB2 */
    unsigned int    L_maxcut;                   /**< L_maxcut
                                                     - 0 .. 111 dB2 */
    int             G_sectioncut;               /**< G_sectioncut
                                                     - -32 .. -1 dB2
                                                     - 0: indicates (drc_nr_cut_sections == 0) */
    unsigned int    L_sectioncut;               /**< L_sectioncut
                                                     - 0 .. 47 dB2 */

#if 0  /* The following elements are not part of this struct since their value can be derived from other elements in this struct */
    unsigned int    drc_repeat_profile_flag;    /**< DRC repeat profile flag (0/1).
                                                     =  (drc_repeat_id != -1) */
#endif
} ac4analyze_drc_mode_cfg_t;

/**
 * @brief Information about DRC in the AC-4 bitstream.
 */
typedef struct ac4analyze_drc_info_s
{
    unsigned int    b_drc_present;                                                              /**< Indicates the presence of DRC data (0/1). */
    int             drc_decoder_nr_modes;                                                       /**< Number of DRC decoder modes.
                                                                                                     - -1:     undefined
                                                                                                     - 0 .. 7: drc_decoder_nr_modes */
    unsigned int    drc_eac3_profile;                                                           /**< DRC EAC-3 profile (value from @ref ac4analyze_drc_profile_t). */
    ac4analyze_drc_mode_cfg_t  drc_decoder_mode_config[AC4ANALYZE_MAX_NUM_DRC_DECODER_MODES];   /**< DRC decoder mode information. */
} ac4analyze_drc_info_t;

/**
 * @brief Information about EMDF in the AC-4 bitstream.
 */
typedef struct ac4analyze_emdf_info_s
{
    unsigned int  emdf_version;                   /**< EMDF syntax version. */
    unsigned int  key_id;                         /**< Authentication ID. */
    int           substream_index;                /**< Reference to substream carrying the EMDF payload.
                                                       - -1:  no payload is referenced
                                                       - >=0: substream_index
                                                       Substream index w.r.t. ordering of the substreams
                                                       in the substream_index_table. */
    /* Protection data to verify the integrity of the EMDF substream and payload data */
    unsigned int  protection_length_primary;      /**< Length (in bytes) of mandatory protection data (0/1/4/16). */
    unsigned int  protection_length_secondary;    /**< Length (in bytes) of optional (additional) protection data (0/1/4/16). */
} ac4analyze_emdf_info_t;

/**
 * @brief Information about a single EMDF payload in the AC-4 bitstream.
 */
typedef struct ac4analyze_emdf_payload_s
{
    unsigned int emdf_payload_id;       /**< EMDF payload identification. */
    unsigned int emdf_payload_size;     /**< EMDF payload size (in bytes). */
    unsigned int b_smpoffst;            /**< Payload sample offset flag (0/1). */
    unsigned int smpoffst;              /**< Payload sample offset. */
    unsigned int b_duration;            /**< Payload duration flag (0/1). */
    unsigned int duration;              /**< Payload duration. */
    unsigned int b_groupid;             /**< Payload group ID flag (0/1). */
    unsigned int groupid;               /**< Payload group ID. */
} ac4analyze_emdf_payload_t;

/**
 * @brief Information about EMDF payloads in the AC-4 bitstream.
 */
typedef struct ac4analyze_emdf_payloads_info_s
{
    unsigned int   num_emdf_payloads;                                                   /**< Number of EMDF payloads. */
    ac4analyze_emdf_payload_t emdf_payload[AC4ANALYZE_MAX_NUM_SUPPORTED_EMDF_PAYLOADS]; /**< EMDF payload information. */
} ac4analyze_emdf_payloads_info_t;

/**
 * @brief Information about associate mixing in the AC-4 bitstream.
 */
typedef struct ac4analyze_assoc_mix_info_s
{
    unsigned int b_scale_main;          /**< Scale main flag (0/1). */
    float        scale_main;            /**< Scale main.
                                             - -76.5:      -inf dB (full mute)
                                             - -76.2 .. 0: scale_main in dB */
    unsigned int b_scale_main_center;   /**< Scale main center flag (0/1). */
    float        scale_main_center;     /**< Scale main center (same range as @ref scale_main). */
    unsigned int b_scale_main_front;    /**< Scale main front flag (0/1). */
    float        scale_main_front;      /**< Scale main front (same range as @ref scale_main). */
    unsigned int b_associate_is_mono;   /**< Flag indicating that the associated audio substream is a mono stream. */
    float        pan_associated;        /**< Associate pan data in degree.
                                             range: 0.0 .. 358.5 (valid values); 0.0 .. 382.5 (possible values) */
} ac4analyze_assoc_mix_info_t;

/**
 * @brief Information about an audio substream in the AC-4 bitstream.
 */
typedef struct ac4analyze_substr_info_s
{
    unsigned long        channel_mode;                 /**< Channel mode (value from @ref ac4analyze_channel_mode_t). */
    unsigned long        sampling_frequency;           /**< Sampling frequency. */
    unsigned int         b_bitrate_info;               /**< Indicates if a bitrate indicator is transmitted. */
    unsigned int         bitrate_indicator;            /**< Upper average bitrate limit per channel. Only valid, if b_bitrate_info == 1 */
    unsigned int         b_audio_ndot;                 /**< Indicates whether the audio substream has no dependency over time. */
    int                  audio_substream_table_index;  /**< Index into the substream_index_table for the audio substream. */
    ptrdiff_t            data_offset;                  /**< The offset in bytes of the audio substream
                                                            (from the beginning of the frame). */
    size_t               data_size;                    /**< The audio substream size in bytes. */

    unsigned int         substream_type;               /**< Substream type (value from @ref ac4analyze_substream_type_t). */
    unsigned int         substream_content;            /**< Substream content (value from @ref ac4analyze_substream_content_t).*/
    unsigned int         b_ajoc;                       /**< A-JOC flag. */
    unsigned int         add_ch_base;                  /**< Additional channels coupling base. */
    unsigned long        ajoc_umx_channel_mask;        /**< This bit-mask contains the channel assignment for the JOC upmix signals. */
    int                  isf_umx_config;
    unsigned int         sus_ver;                      /**< Substream version. */
    unsigned int         b_object_coded;               /**< Object coding */
    unsigned int         n_fullband_dmx_signals;
    unsigned int         n_fullband_umx_signals;
    unsigned int         n_umx_signals;
    unsigned int         n_input_signals;              /* Copied from TOC reader: Number of input signals incl. LFE and LFE2, 
                                                          For JOC: Number of downmix signals */
    unsigned int         b_static_dmx;
    unsigned int         b_substream_contains_bed_objects;
    unsigned int         b_substream_contains_isf_objects;
    unsigned int         b_substream_contains_dynamic_objects;

    /* Basic Metadata */
#ifdef INTERNAL_ONLY
    /* dialnorm in bitstream (on substream level) only for (sus_ver == 0) */
#endif
    float                dialnorm;                     /**< Input reference level
                                                            - -31.75 .. 0 dB_FS */
    unsigned int         b_more_basic_metadata;        /**< More basic metadata flag (0/1). */
    int                  pre_dmixtyp_2ch;              /**< Pre downmix to 2 channels type (value from @ref ac4analyze_dmx_config_2ch_t).
                                                            - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    unsigned int         phase90_info_2ch;             /**< Pre 2 channels phase 90 shift (value from @ref ac4analyze_phase90_info_t).
                                                            - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
#ifdef INTERNAL_ONLY
    /* stereo_dmx_info and loud_corr_info in bitstream (on substream level) only for (sus_ver == 0) */
#endif
    ac4analyze_stereo_dmx_info_t    stereo_dmx_info;   /**< Stereo downmix information. */
    ac4analyze_loud_corr_info_t     loud_corr_info;    /**< Loudness correction information. */
    int                  pre_dmixtyp_5ch;              /**< Pre downmix to 5 channels type (value from @ref ac4analyze_dmx_config_5ch_t).
                                                            - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    int                  pre_upmixtyp_5ch;             /**< Pre upmix to 5 channels type (value from @ref ac4analyze_umx_config_5ch_t).
                                                            - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    int                  pre_upmixtyp_7ch;             /**< Pre upmix to 7 channels type (value from @ref ac4analyze_umx_config_7ch_t).
                                                            - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    unsigned int         phase90_info_mc;              /**< Pre multi-channel phase 90 shift (value from @ref ac4analyze_phase90_info_t).
                                                            - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    unsigned int         b_surround_attenuation_known; /**< Surround attenuation known flag (0/1).
                                                            - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    unsigned int         b_lfe_attenuation_known;      /**< LFE attenuation known flag (0/1).
                                                            - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    unsigned int         dc_block_on;                  /**< DC block on (value from @ref ac4analyze_dc_blocking_info_t).
                                                            - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */

    /* Further Loudness Information */
    unsigned int         b_loudness_info;              /**< Further loudness information present flag (0/1). */
    ac4analyze_loudness_info_t      loudness_info;     /**< Further loudness information. */

    /* Extended Metadata */
    unsigned int         b_associated;                 /**< Associate substream flag (0/1). */
    unsigned int         b_dialog;                     /**< Dialog substream flag (0/1). */
    ac4analyze_assoc_mix_info_t     assoc_mix_info;               /**< Associate mixing information. */
    unsigned int         dialog_max_gain;              /**< Dialog max gain.
                                                            - 0:        undefined
                                                            - 3/6/9/12: dialog_max_gain in dB */
    unsigned int         b_pan_dialog_present;         /**< Dialog pan data present flag (0/1). */
    float                a_pan_dialog[2];              /**< Dialog pan data in degree
                                                            (same range as @ref ac4analyze_assoc_mix_info_t::pan_associated). */
    unsigned int         pan_signal_selector;          /**< Dialog pan signal selector for 3_0_channel_element
                                                            (value from @ref ac4analyze_pan_signal_selector_t). */

    unsigned int         b_4_back_channels_present;    /**< Back channels present in original content flag (0/1). */
    unsigned int         b_centre_present;             /**< Center channel present in origianl content.*/
    unsigned int         top_channels_present;         /**< Fixed length field to indicate the number and location of top channels
                                                            contained in original content */

    /* Dynamic Range Control data */
#ifdef INTERNAL_ONLY
    /* drc_info in bitstream (on substream level) only for (sus_ver == 0) */
#endif
    ac4analyze_drc_info_t           drc_info;          /**< Dynamic Range Control information. */

    /* Dialog enhancement data */
    unsigned int         b_de_data_present;            /**< Dialog enhancement data present flag (0/1). */
    unsigned int         b_de_config_flag;             /**< Dialog enhancement configuration flag (0/1). */
    int                  de_method;                    /**< Dialog enhancement method (value from @ref ac4analyze_de_method_t).
                                                            - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    unsigned int         de_max_gain;                  /**< Maximum dialog enhancement gain.
                                                            - 0:        undefined
                                                            - 3/6/9/12: de_max_gain in dB2
                                                            - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */
    unsigned int         de_channel_config;            /**< Dialog enhancement channel configuration
                                                            (value from @ref ac4analyze_de_channel_config_t).
                                                            - Depends on b_sticky_metadata in @ref ac4analyze_cfg_s. */

    /* EMDF */
    ac4analyze_emdf_payloads_info_t emdf_payloads_info;/**< EMDF payloads info. */
    ac4analyze_ims_info_t           ims_info;          /**< IMS information only valid, if EMDF_ID_IMS payload exists */

#if 0  /* The following elements are not part of this struct since their value can be derived from other elements in this struct */
    unsigned int         b_prev_dmx_info;              /**< Previous downmix to 2 channels flag (0/1).
                                                            = (pre_dmixtyp_2ch != DMX_CONFIG_2CH_UNDEFINED) */
    unsigned int         b_predmxtyp_5ch;              /**< Previous downmix to 5 channels flag (0/1).
                                                            = (pre_dmixtyp_5ch != DMX_CONFIG_5CH_UNDEFINED) */
    unsigned int         b_preupmixtyp_5ch;            /**< Previous upmix to 5 channels flag (0/1).
                                                            = (pre_upmixtyp_5ch != UMX_CONFIG_5CH_UNDEFINED) */
    unsigned int         b_dialog_max_gain;            /**< Dialog max gain flag (0/1).
                                                            = (dialog_max_gain != 0) */
#endif
} ac4analyze_substr_info_t;

/**
 * @brief Information about a substream group in the AC-4 bitstream.
 */
typedef struct ac4analyzer_sg_info_s
{
    unsigned int  b_channel_coded;
    unsigned int  b_substream_present;
    unsigned int  b_hsf_ext;
    unsigned int  n_substreams;
    unsigned int  substream_index[AC4ANALYZE_MAX_NUM_SUPPORTED_SUBSTREAMS_PER_GROUP];
    int           content_classifier;                 /**< Content classifier (value from @ref ac4analyze_content_classifier_t). */
    size_t        n_language_tag_bytes;
    unsigned char language_tag_bytes[AC4ANALYZE_MAX_LANGUAGE_TAG_BYTES];
} ac4analyzer_sg_info_t;
    
/**
 * @brief Information about a presentation in the AC-4 bitstream.
 */
typedef struct ac4analyze_pres_info_s
{
    unsigned int                    b_presentation_decodable;                               /**< Presentation Decodable flag with AC-4 Decoder Version 1.1.
                                                                                                   - 0: presentation not decodable
                                                                                                   - 1: presentation decodable */
    int                             b_presentation_filter;                                  /**< Presentation Filter flag
                                                                                                   - 0: no b_presentation_enabled present in bitstream
                                                                                                   - 1: b_presentation_enabled present in bitstream */
    unsigned int                    b_presentation_enabled;                                 /**< Presentation Enabled flag.
                                                                                                   - 0: presentation disabled
                                                                                                   - 1: presentation enabled */
    unsigned int                    b_alternative;                                          /**< Alternative presentation flag (0/1). */
    unsigned int                    presentation_config;                                    /**< Presentation configuration (value from @ref ac4analyze_presentation_config_t). */
    unsigned int                    presentation_type;                                      /**< Presentation type (value from @ref ac4analyze_presentation_type_t). */
    int                             presentation_version;                                   /**< Presentation version information.
                                                                                                   - -1: undefined
                                                                                                   - 0 .. 31: presentation_version */
    int                             presentation_level;                                     /**< Presentations minimal compatibility level.
                                                                                                   - -1: undefined
                                                                                                   - 0 .. 7: presentation_level */
    int                             presentation_id;                                        /**< Presentation ID.
                                                                                                   - -1: undefined
                                                                                                   - 0 .. 127: presentation_id */
    unsigned int                    b_hsf_ext;                                              /**< High sampling frequency extension flag */
    unsigned int                    num_audio_substreams_in_pres;                           /**< Number of audio substreams in the presentation. */
    unsigned int                    num_audio_substreams_in_pres_unclipped;                 /**< Number of audio substreams in the presentation.
                                                                                                 This value may be larger than the number of available
                                                                                                 array entries. */
    int                             audio_substream_index[AC4ANALYZE_MAX_NUM_SUPPORTED_SUBSTREAMS]; /**< Reference to substream carrying the ac4_substream().
                                                                                                - -1: audio substream not present
                                                                                                - >= 0: audio substream_index
                                                                                                - >= AC4ANALYZE_MAX_NUM_SUPPORTED_SUBSTREAMS: Not a valid index
                                                                                                       into the substream_data array in ac4analyze_frame_data_t */
    unsigned int                    num_substream_groups_in_pres;                           /**< Number of substream groups in the presentation. */
    int                             substream_group_index[AC4ANALYZE_MAX_NUM_SUPPORTED_SUBSTREAM_GROUPS]; /**< Reference to substream group.
                                                                                                - -1: substream group not present
                                                                                                - >= 0: substream group index */
    ac4analyze_bs_content_type_t    content_type[AC4ANALYZE_MAX_NUM_SUPPORTED_SUBSTREAMS];  /**< Information about the content type of the substream. */
    char                            presentation_name[AC4ANALYZE_MAX_PRESNAME_LEN_BYTES];           /**< Alternative presentation name.
                                                                                                 This information is only available in case b_alternative is set. */
    unsigned int                    presentation_name_length;                               /**< Alternative presentation name length. */
    unsigned int                    n_targets;                                              /**< Number of targets */
    unsigned int                    target_level[AC4ANALYZE_MAX_TARGETS_PER_PRESENTATION];  /**< Target level */
    unsigned int                    target_device_category[AC4ANALYZE_MAX_TARGETS_PER_PRESENTATION];   /**< Target device category */
    int                             pres_substream_table_index;                             /**< Index into the substream_index_table for the presentation substream.
                                                                                                   - -1: no presentation substream
                                                                                                   - >= 0: substream_index */
    unsigned int                    b_pres_ndot;                                            /**< Indicates whether the presentation substream has no dependency over time. */
    unsigned int                    frame_rate_factor;                                      /**<   - 0: undefined
                                                                                                   - 1, 2, 4: */
    unsigned int                    frame_rate_fraction;                                    /**< Number of packets that form one decodable frame.
                                                                                                   - 0: undefined
                                                                                                   - 1, 2 or 4: frame_rate_fraction
                                                                                                   A value > 1 indicates the Efficient High Frame Rate (EHFR) mode. */
    unsigned int                    b_pre_virtualized;                                      /**< Pre virtualized */

    /* metadata from ac4_presentation_substream() */
    float                           dialnorm;                                               /**< Input reference level
                                                                                                    - -31.75 .. 0 dB_FS */
    ac4analyze_assoc_mix_info_t     assoc_mix_info;                                         /**< Associate mixing information. */
    ac4analyze_stereo_dmx_info_t    stereo_dmx_info;                                        /**< Stereo downmix information. */
    ac4analyze_loud_corr_info_t     loud_corr_info;                                         /**< Loudness correction information. */
    
    unsigned int                    b_cdmx_512_info_present;                                /**< Flag to indicate if custom downmix info for
                                                                                                 5.1.2 height channels is present. */
    ac4analyze_cdmx_512_info_t      cdmx_512_info;                                          /**< Custom downmix info for 5.1.2 to 5.1 */
    
    unsigned int                    b_cdmx_514_info_present;                                /**< Flag to indicate if custom downmix info for
                                                                                                 5.1.4 height channels is present. */
    ac4analyze_cdmx_514_info_t      cdmx_514_info;                                          /**< Custom downmix info for 5.1.4 to 5.1 */
    
    unsigned int                    b_cdmx_t4_to_t2_info_present;                           /**< Flag to indicate if custom downmix info for
                                                                                                 5.1.4 to 5.1.2 height channels is present. */
    ac4analyze_cdmx_t4_to_t2_info_t cdmx_t4_to_t2_info;                                     /**< Custom downmix info for 5.1.4 to 5.1.2 */

    /** Further loudness information from ac4_presentation_substream(). */
    unsigned int                    b_loudness_info;
    ac4analyze_loudness_info_t      loudness_info;
    unsigned int                    b_pres_has_lfe;                                         /**< does presentation contain LFE */
    /** Dynamic Range Control information from ac4_presentation_substream(). */
    ac4analyze_drc_info_t           drc_info;

    /* EMDF */
    unsigned int                    num_emdf_info_elements;                                 /**< Total number of EMDF info elements. */
    unsigned int                    num_emdf_info_elements_unclipped;                       /**< Total number of EMDF info elements.
                                                                                                 This value may be larger than corresponding array. */
    ac4analyze_emdf_info_t          emdf_info[AC4ANALYZE_MAX_NUM_SUPPORTED_EMDF_INFO];      /**< EMDF info data. */
    ac4analyze_emdf_payloads_info_t emdf_payloads_info[AC4ANALYZE_MAX_NUM_SUPPORTED_EMDF_INFO]; /**< EMDF payloads info. */

	unsigned int                    b_multi_pid;                                            /**< Indicates if presentation is split over multiple streams */
    ac4analyze_channel_mode_t       pres_ch_mode;                                           /**< Presentation channel mode */
    ac4analyze_channel_mode_t       pres_ch_mode_core;
    unsigned int                    b_pres_4_back_channels_present;                         /**< Indicates that 4 back channels are present.
                                                                                                 This value is only valid for pres_ch_mode 7.0.4, 7.1.4, 9.0.4, 9.1.4 */
    pres_top_channel_pairs_t        pres_top_channel_pairs;                                 /**< Indicates how many top channel pairs are present.
                                                                                                 This value is only valid for pres_ch_mode 7.0.4, 7.1.4, 9.0.4, 9.1.4 */
    unsigned int                    b_pres_centre_present;                                  /**< Indicates that the center channel is present.
                                                                                                 This value is only valid for pres_ch_mode 7.0.4, 7.1.4, 9.0.4, 9.1.4 */
    ac4analyze_dolby_atmos_indicator_t  dolby_atmos_indicator;
    float                           dialog_gain;                                            /**< Dialog gain in dB, or AC4ANALYZE_GAIN_M_INF. Needed for reporting the 
                                                                                                 different gains used in the different presentations in presentation based
                                                                                                 dialog enhancehment bitstreams (aka flexstreams). Only relevant for 
                                                                                                 presentation config 0 and 3  (presentation with separate dialog). */
    float                           music_and_effect_gain;                                  /**< Music and effect gain in dB, or AC4ANALYZE_GAIN_M_INF. Only relevant for 
                                                                                                 presentation config 0 and 3  (presentation with separate dialog). See 
                                                                                                 dialog_gain above. */
#if 0  /* The following elements are not part of this struct since their value can be derived from other elements in this struct */
    unsigned int                    b_content_type;                                         /**< Content type presence flag (0/1).
                                                                                            = (content_type.content_classifier != AC4_CONTENT_UNDEFINED) */
    unsigned int                    b_single_substream;                                     /**< Single substream flag (0/1).
                                                                                            = (presentation_config == AC4_PRESENTATION_CONFIG__SINGLE_SUBSTREAM) */
    unsigned int                    b_presentation_id;                                      /**< Presentation ID presence flag (0/1).
                                                                                            = (presentation_id != -1) */
    unsigned int                    b_further_loudness_info;                                /**< Further loudness info flag (0/1).
                                                                                            = (loudness_info.loudness_version != -1) */
#endif
} ac4analyze_pres_info_t;

/**
 * @brief AC-4 Bitstream Table of Content
 *
 * The bitstream inventory.
 * Contains information from the ac4_toc() bitstream syntax element excluding detailed presentation and substream information.
 */
typedef struct ac4analyze_toc_info_s
{
    unsigned int  bitstream_version;                /**< Bitstream syntax version (0 ... ). */
    unsigned int  fs_index;                         /**< Sampling frequency index (0/1). */
    unsigned int  frame_rate_index;                 /**< Frame rate index  (0 .. 15). */
    int           wait_frames;                      /**< Indicates how many frames the decoder should wait
                                                         before decoding of the frame can commence:
                                                         - -1: undefined
                                                         - 0 .. 7: wait_frames */
    int           br_corr;                          /**< Indicates the bitrate correction code.
                                                         Only relevant when wait_frames is not undefined.
                                                         - -1: undefined
                                                         - 0 .. 2: br_code contributing to the bitrate correction factor
                                                         - 3: start-stop br_code
                                                         A sequence of br_code values is used to calculate the bitrate correction factor */
    unsigned int  br_mode;                          /**< Bitrate mode (value from @ref ac4analyze_toc_br_mode). */
    unsigned int  br_est_status;                    /**< Bitrate estimate status (value from @ref ac4analyze_toc_br_est_status).
                                                         The bitrate estimation is reset when (1) verification of br_code fails,
                                                         i.e. a different correction factor is received, (2) a splice is
                                                         detected by an invalid sequence counter value for the frame or (3) a double
                                                         start-stop br_code explicitly signals a bitrate change. A consequence of
                                                         this is, that an outdated bitrate may be reported in the first frame after the
                                                         bitrate change. Especially a channel mode change not directly triggers
                                                         a reset of the bitrate estimation. */
    unsigned long br_est_lower;                     /**< Lower bitrate estimate in bps if br_est_status is AC4_TOC_BR_EST_AVAILABLE or
                                                        AC4_TOC_BR_EST_AVAILABLE_PREVIOUS. */
    unsigned long br_est_upper;                     /**< Upper bitrate estimate in bps if br_est_status is AC4_TOC_BR_EST_AVAILABLE or
                                                        AC4_TOC_BR_EST_AVAILABLE_PREVIOUS. */
    unsigned int  b_iframe_global;                  /**< Indicates that current frames in all substreams in all presentations are coded as
                                                        independent frames (0/1). */
    unsigned int  iframe_interval;                  /**< I-frame global interval.
                                                         - 0: unknown
                                                         - 1: one frame back in history was an I-frame
                                                         - n: n frames back in history was an I-frame */
    unsigned int  num_presentations;                /**< Number of presentations in the AC-4 bitstream.
                                                         This value is clipped to AC4ANALYZE_MAX_NUM_SUPPORTED_PRESENTATIONS. */
    unsigned int  num_presentations_unclipped;      /**< Number of presentations in the AC-4 bitstream.
                                                         This value may be larger than number of entires in the presentation_data array. */
    unsigned int  sequence_counter;                 /**< Sequence counter
                                                        - 0 .. 1023 */
    unsigned int  program_id_availability;          /**< Describes which program identifiers are available
                                                        (value from @ref ac4analyze_programid_availability_t). */
    unsigned int  short_program_id;                 /**< Short program ID. */
    unsigned int  num_audio_substreams_in_frame;    /**< Total number of audio substreams in the frame */
    int           audio_substream_table_index_to_first_substream_data_index[AC4ANALYZE_MAX_NUM_SUPPORTED_SUBSTREAMS];
                                                    /**< IMS substreams share one encoded substream identified by one audio_substream_table_index.
                                                         The TOC has two different entries for that substream in two different presentations.
                                                         Each of the two TOC substream entries has its own substream_data_index. **/
    unsigned char program_uuid[AC4ANALYZE_PROGRAM_UUID_LEN_BYTES]; /**< Program universal unique ID. */

#if 0  /* The following elements are not part of this struct since their value can be derived from other elements in this struct */
    unsigned int  b_single_presentation;            /**< Single presentation flag (0/1).
                                                        = (num_presentations == 1) */
    insigned int  b_more_presentations;             /**< More presentations flag (0/1).
                                                        = (num_presentations >= 2) */
#endif
} ac4analyze_toc_info_t;

/**
 * @brief AC-4 simple transport stream information.
 */
typedef struct ac4analyze_simple_transport_s
{
    unsigned int            ac4_sync_word;       /**< AC4 sync word (value from @ref ac4analyze_ac4_sync_word_t). */
    unsigned int            b_crc_error;         /**< CRC error (0/1). */
    size_t                  raw_ac4_frame_size;  /**< Size of the raw AC-4 frame (in bytes). */
} ac4analyze_simple_transport_t;

/**
 * @brief Holds parsed and interpreted bitstream data from an AC-4 frame.
 */
typedef struct ac4analyze_frame_data_s
{
    unsigned int                   status;                  /**< Status of the frame data (value from @ref ac4analyze_frame_status_t). */    
    ac4analyze_simple_transport_t  simple_transport_data;   /** AC-4 simple transport stream information - not available when analyzing raw AC-4 frames. */
    ac4analyze_toc_info_t          toc_data;                /** AC-4 TOC data. */
    ac4analyze_pres_info_t         presentation_data[AC4ANALYZE_MAX_NUM_SUPPORTED_PRESENTATIONS];   /** AC-4 presentation data. */
    ac4analyze_substr_info_t       substream_data[AC4ANALYZE_MAX_NUM_SUPPORTED_SUBSTREAMS];          /** AC-4 substream data. */
    ac4analyzer_sg_info_t          substream_group_data[AC4ANALYZE_MAX_NUM_SUPPORTED_SUBSTREAM_GROUPS]; /** AC-4 substream group data */
} ac4analyze_frame_data_t;

/**
 * @brief The static AC-4 bitstream analyzer configuration.
 */
typedef struct ac4analyze_cfg_s
{
    unsigned int         input_bsformat;     /**< Indicates the bitstream format of the input (value from @ref ac4analyze_bsformat_t). */
    unsigned int         b_sticky_metadata;  /**< Sticky metadata flag
                                                  - 0: report only metadata values for elements present in the current frame.
                                                  - 1: report sticky/default values for certain metadata elements which are
                                                       only sent once in a while (within an iframe) but which are valid until
                                                       its next occurrence or until the occurrence of the next iframe. */
} ac4analyze_cfg_t;

/**
 * @brief AC-4 bitstream analyzer handle.
 */
typedef struct ac4analyze_s*     ac4analyze_handle;

/**
 * @brief Get version info for the AC-4 bitstream analyzer library.
 *
 * This function returns the version of the library.
 *
 * @return ac4analyze_version_info_t
 */
const ac4analyze_version_info_t *
dlb_ac4analyze_get_version(void);

/**
 * @brief Get memory requirements for an AC-4 bitstream analyzer instance.
 *
 * This function returns the size of the persistent memory (in bytes) that
 * needs to be allocated outside of the library.
 *
 * @return AC4ANALYZE_OK
 */
ac4analyze_error_code
dlb_ac4analyze_query_mem
    (size_t               max_raw_frame_size /**< [in]  Maximum raw AC-4 frame size (in bytes) */
    ,size_t              *p_mem_size         /**< [out] Pointer to the persistent memory size (in bytes). */
    );

/**
 * @brief Open an AC-4 bitstream analyzer instance.
 *
 * This function returns an AC-4 bitstream analyzer handle.
 *
 * @return AC4ANALYZE_OK
 */
ac4analyze_error_code
dlb_ac4analyze_open
    (ac4analyze_handle   *ph_ac4analyzer     /**< [out] Pointer to the AC-4 bitstream analyzer handle. */
    ,void                *p_persistent_mem   /**< [in]  Pointer to the internal persistent memory. */
    ,ac4analyze_cfg_t    *p_config           /**< [in]  The static AC-4 analyzer configuration. */
    ,size_t               max_raw_frame_size /**< [in]  Maximum raw AC-4 frame size (in bytes) */
    );

/**
 * @brief Feed an AC-4 bitstream into a bitstream analyzer instance.
 *
 * The bitstream has to be an AC-4 simple transport stream segment.
 * This function shall only be called if input_bsformat was set to AC4ANALYZE_BSFORMAT_AC4_SIMPLE_TRANSPORT
 * in the call to dlb_ac4analyze_open().
 *
 * @return AC4ANALYZE_OK
 * @return AC4ANALYZE_ERROR_USAGE if input_bsformat is AC4ANALYZE_BSFORMAT_RAW_AC4_FRAME
 */
ac4analyze_error_code
dlb_ac4analyze_feed
    (ac4analyze_handle           h_ac4analyzer       /**< [in, out] AC-4 bitstream analyzer handle. */
    ,unsigned char              *p_bitstream         /**< [in]     Pointer to AC-4 bitstream data. */
    ,size_t                      bs_size             /**< [in]     Size of the AC-4 bitstream data. */
    );

/**
 * @brief Analyze an AC-4 frame.
 *
 * Raw AC-4 frames are provided directly to this function while AC-4 simple transport stream segments
 * are fed via dlb_ac4analyze_feed().
 *
 * @return AC4ANALYZE_OK
 */
ac4analyze_error_code
dlb_ac4analyze_analyze_frame
    (ac4analyze_handle           h_ac4analyzer       /**< [in, out] AC-4 bitstream analyzer handle. */
    ,unsigned char              *p_bitstream         /**< [in]      Pointer to raw AC-4 frame or NULL. */
    ,size_t                      bs_size             /**< [in]      Size of the raw AC-4 frame or 0. */
    ,size_t                     *p_skipped_bytes     /**< [in,out]  accumulates number of skipped bytes */
    ,ac4analyze_frame_data_t    *p_ac4_frame_data    /**< [out]     Pointer to AC-4 frame data info. */
    );

/**
 * @brief Close an AC-4 bitstream analyzer instance.
 *
 * This function performs memory de-allocation.
 *
 */
void
dlb_ac4analyze_close
    (ac4analyze_handle          h_ac4analyzer       /**< [in] AC-4 bitstream analyzer handle. */
    );

#ifdef __cplusplus
}
#endif

#endif /* DLB_AC4ANALYZE_H */

/**
 *  @}
 */
