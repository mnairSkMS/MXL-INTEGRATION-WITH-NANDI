/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                Copyright (C) 2022 by Dolby Laboratories,
 *                Copyright (C) 2022 by Dolby International AB.
 *                            All rights reserved.
 ******************************************************************************/

/**
 * @file  dlb_padc_ui_md_ed2.h
 *
 * @brief File containing structure definitions for Dolby ED2 UI metadata
 */

#ifndef __DLB_PADC_UI_DOLBY_ED_2MD
#define __DLB_PADC_UI_DOLBY_ED_2MD

#ifdef __cplusplus
extern "C" {
#endif
 
#include <stdint.h>


/**
 * @def DLB_PADC_MAX_SUBSTREAMS_NUM_ED2
 * @brief Defines the maximum amount of substreams available in bitstream
 */
#define DLB_PADC_MAX_SUBSTREAMS_NUM_ED2                     (2)

/**
 * @def DLB_PADC_MAX_PGM_CFG_TEXT_ED2
 * @brief Defines the maximum length of program configuration text
 */
#define DLB_PADC_MAX_PGM_CFG_TEXT_ED2                       (16)

/**
 * @def DLB_PADC_MAX_PRES_DESC_TEXT_LEN
 * @brief Defines the maximum length of program configuration text
 */
#define DLB_PADC_MAX_PRES_DESC_TEXT_LEN_ED2                 (64)

/**
 * @def DLB_PADC_MAX_BEDS_COUNT_ED2
 * @brief Defines the maximum number of Dolby ED2 bed objects
 */
#define DLB_PADC_MAX_BEDS_COUNT_ED2                         (9)

/**
 * @def DLB_PADC_BED_MAX_CHANNEL_COUNT
 * @brief Defines the maximum amount of channels for the bed object
 */
#define DLB_PADC_BED_MAX_CHANNEL_COUNT                      (16)

/**
 * @def DLB_PADC_MAX_DYNAMIC_OBJECTS_COUNT_ED2
 * @brief Defines the maximum number of Dolby ED2 dynamic objects
 */
#define DLB_PADC_MAX_DYNAMIC_OBJECTS_COUNT_ED2              (16)

/**
 * @def DLB_PADC_MAX_AUDIO_CFG_NUM_ED2
 * @brief Defines the maximum channel configurations count for audio objects
 */
#define DLB_PADC_MAX_AUDIO_CFG_NUM_ED2                      (3)

/**
 * @def DLB_PADC_MAX_PRES_COUNT_ED2
 * @brief Defines maximum number of presentations available for Dolby ED2
 */
#define DLB_PADC_MAX_PRES_COUNT_ED2                         (16)

/**
 * @def DLB_PADC_MAX_CONFIGS_COUNT_ED2
 * @brief Defines maximum number of bed and dynamic object configurations
 */
#define DLB_PADC_MAX_CONFIGS_COUNT_ED2                      (DLB_PADC_MAX_PRES_COUNT_ED2 * DLB_PADC_MAX_AUDIO_CFG_NUM_ED2)


/**
 * @addtogroup STD_PARAM_ENUM Parameter Value Enumerations
 * @{
 */

/**
 * @typedef dlb_padc_bed_channel_ed2
 * @brief enum used to indicate channel type of the bed object
 */
typedef enum
{
    DLB_PADC_BED_CHANNEL_L      = 0,
    DLB_PADC_BED_CHANNEL_R,
    DLB_PADC_BED_CHANNEL_C,
    DLB_PADC_BED_CHANNEL_LFE,
    DLB_PADC_BED_CHANNEL_LS,
    DLB_PADC_BED_CHANNEL_RS,
    DLB_PADC_BED_CHANNEL_LRS,
    DLB_PADC_BED_CHANNEL_RRS,
    DLB_PADC_BED_CHANNEL_LFH,
    DLB_PADC_BED_CHANNEL_RFH,
    DLB_PADC_BED_CHANNEL_LTM,
    DLB_PADC_BED_CHANNEL_RTM,
    DLB_PADC_BED_CHANNEL_LRH,
    DLB_PADC_BED_CHANNEL_RRH,
    DLB_PADC_BED_CHANNEL_LFW,
    DLB_PADC_BED_CHANNEL_RFW,
    DLB_PADC_BED_CHANNEL_LFE2
}
dlb_padc_bed_channel_ed2;

/**
 * @typedef dlb_padc_obj_sound_category
 * @brief enum used to indicate object category
 */
typedef enum
{
    DLB_PADC_OBJ_SOUND_CATEGORY_NOT_INDICATED,
    DLB_PADC_OBJ_SOUND_CATEGORY_DIALOGUE
}
dlb_padc_obj_sound_category_ed2;

/** @} */

/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter Data Structures
 * @{
 */

/**
 * @brief Structure containing information about Dolby ED2 bitstream
 */
typedef struct dlb_padc_bitstream_properties_ed2_s
{
    uint16_t frame_rate;                                    /**< Bitstream's frame rate */
    uint16_t atmos_presence;                                /**< Indicates if stream contains Object Audio Metadata */
    uint16_t metadata_id;                                   /**< Dolby ED2 metadata identifier */
}
dlb_padc_bitstream_properties_ed2;

/**
 * @brief Structure containing channels present in bed object
 */
typedef struct dlb_padc_bed_channels_ed2_s
{
    uint8_t L;    /**< Left */
    uint8_t R;    /**< Right */
    uint8_t C;    /**< Center */
    uint8_t LFE;  /**< Low-Frequency Effects */
    uint8_t LS;   /**< Left Surround */
    uint8_t RS;   /**< Right Surround */
    uint8_t LRS;  /**< Left Rear Surround */
    uint8_t RRS;  /**< Right Rear Surround */
    uint8_t LFH;  /**< Left Front Height */
    uint8_t RFH;  /**< Right Front Height */
    uint8_t LTM;  /**< Left Top Middle */
    uint8_t RTM;  /**< Right Top Middle */
    uint8_t LRH;  /**< Left Rear Height */
    uint8_t RRH;  /**< Right Rear Height */
    uint8_t LW;   /**< Left Wide */
    uint8_t RW;   /**< Right Wide */
    uint8_t LFE2; /**< Low-Frequency Effects 2 */
}
dlb_padc_bed_channels_ed2;

/**
 * @brief Structure containing information about Dolby ED2 audio bed objects
 */
typedef struct dlb_padc_bed_objects_ed2_s
{
    int16_t configs_cnt;                        /** Configuration count */
    int16_t channel_id;                         /** Source channel identification */
    dlb_padc_bed_channels_ed2 bed_channels;     /** Channels present in audio bed object */
    float gain[DLB_PADC_MAX_CONFIGS_COUNT_ED2]; /** Gains in decibels for a specific configuration */
    int32_t bed_channels_cnt;                   /** Number of channels present in an audio bed object */
    dlb_padc_bed_channel_ed2 channel_assignment[DLB_PADC_BED_MAX_CHANNEL_COUNT];     /** Bed object channel identification */
}
dlb_padc_bed_objects_ed2;

typedef struct dlb_padc_dyn_obj_position_ed2_s
{
    float x;                                     /** X position of the object in the presentaion */
    float y;                                     /** Y position of the object in the presentaion */
    float z;                                     /** Z position of the object in the presentaion */
}
dlb_padc_dyn_obj_position_ed2;

/**
 * @brief Structure containing information about Dolby ED2 audio dynamic objects
 */
typedef struct dlb_padc_dyn_objects_ed2_s
{
    int16_t configs_cnt;                            /** Configuration count */
    int16_t channel_id;                             /** Channel identification */
    dlb_padc_obj_sound_category_ed2 obj_category;   /** Dynamic object category */
    float gain[DLB_PADC_MAX_CONFIGS_COUNT_ED2];     /** Gains in decibels for a specific configuration */
    dlb_padc_dyn_obj_position_ed2 position[DLB_PADC_MAX_CONFIGS_COUNT_ED2]; /** Positions for a specific configuration */
}
dlb_padc_dyn_objects_ed2;

/**
 * @brief Structure containing information about Dolby ED2 audio objects
 */
typedef struct dlb_padc_audio_objects_ed2_s
{
    int16_t bed_objects_cnt;
    int16_t dyn_objects_cnt;
    dlb_padc_bed_objects_ed2 bed_objects[DLB_PADC_MAX_BEDS_COUNT_ED2];
    dlb_padc_dyn_objects_ed2 dynamic_objects[DLB_PADC_MAX_DYNAMIC_OBJECTS_COUNT_ED2];
}
dlb_padc_audio_objects_ed2;


/**
 * @brief Structure containing information about Dolby ED2 substrams
 */
typedef struct dlb_padc_substream_properties_ed2_s
{
    char program_config_text[DLB_PADC_MAX_PGM_CFG_TEXT_ED2];
    int32_t frame_cnt;
    uint8_t crc_error_present;
}
dlb_padc_substream_properties_ed2;

/**
 * @brief Structure containing information about Dolby ED2 presentations
 */
typedef struct dlb_padc_pres_info_ed2_s
{
    char description[DLB_PADC_MAX_PRES_DESC_TEXT_LEN_ED2];                  /** Presentation description */
    uint8_t enabled_bed[DLB_PADC_MAX_BEDS_COUNT_ED2];                       /** Enabled bed object for a presentation */
    uint8_t enabled_bed_cfg[DLB_PADC_MAX_BEDS_COUNT_ED2];                   /** Enabled bed object configuration for a presentation */
    uint8_t enabled_dyn_object[DLB_PADC_MAX_DYNAMIC_OBJECTS_COUNT_ED2];     /** Enabled dynamic objects for a presentation */
    uint8_t enabled_dyn_object_cfg[DLB_PADC_MAX_DYNAMIC_OBJECTS_COUNT_ED2]; /** Enabled dynamic objects configuration for a presentation */
}
dlb_padc_pres_info_ed2;

/**
 * @brief Structure containing information about Dolby ED2 bitstream
 */
typedef struct dlb_padc_ui_md_ed2_s
{
    dlb_padc_bitstream_properties_ed2 stream_properties;                                     /**< Bitstream properties */
    dlb_padc_substream_properties_ed2 substream_properties[DLB_PADC_MAX_SUBSTREAMS_NUM_ED2]; /**< Substream properties */
    dlb_padc_audio_objects_ed2        audio_objects;                                         /**< Audio objects properties */
    uint16_t                          audio_presentations_cnt;                               /** Audio presentations count */
    dlb_padc_pres_info_ed2            audio_presentations[DLB_PADC_MAX_PRES_COUNT_ED2];      /** Audio presentations properties */
}
dlb_padc_ui_md_ed2;

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* __DLB_PADC_UI_DOLBY_ED_2MD */
