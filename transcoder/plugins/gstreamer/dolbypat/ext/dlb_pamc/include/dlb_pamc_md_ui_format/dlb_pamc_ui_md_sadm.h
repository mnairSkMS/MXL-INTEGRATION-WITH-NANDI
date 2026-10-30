/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                Copyright (C) 2023 by Dolby Laboratories,
 *                Copyright (C) 2023 by Dolby International AB.
 *                            All rights reserved.
 ******************************************************************************/

/**
 * @file  dlb_pamc_ui_md_sadm.h
 *
 * @brief File containing structure definitions for Serialized ADM UI metadata
 */

#ifndef __DLB_PAMC_UI_SADM
#define __DLB_PAMC_UI_SADM

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>


/**
 * @def DLB_PAMC_SADM_METADATA_ID_SIZE
 * @brief Defines the buffer size for metadata id
 */
#define DLB_PAMC_SADM_METADATA_ID_SIZE                      (37)

/**
 * @def DLB_PAMC_SADM_STRING_SIZE
 * @brief Defines the buffer size for most string values
 */
#define DLB_PAMC_SADM_STRING_SIZE                           (65)

/**
 * @def DLB_PAMC_SADM_LANGUAGE_SIZE
 * @brief Defines the buffer size for language code
 */
#define DLB_PAMC_SADM_LANGUAGE_SIZE                         (4)

/**
 * @def DLB_PAMC_SADM_ID_SIZE
 * @brief Defines the buffer size for ID of audioProgramme or
 *        audioContent
 */
#define DLB_PAMC_SADM_ID_SIZE                               (21)

/**
 * @def DLB_PAMC_SADM_MAX_AUDIO_PROGRAM_COUNT
 * @brief Defines the maximum supported number of audioProgrammes
 */
#define DLB_PAMC_SADM_MAX_AUDIO_PROGRAM_COUNT               (10)

/**
 * @def DLB_PAMC_SADM_MAX_AUDIO_CONTENT_COUNT
 * @brief Defines the maximum supported number of audioContents
 */
#define DLB_PAMC_SADM_MAX_AUDIO_CONTENT_COUNT               (36)

/**
 * @def DLB_PAMC_SADM_MAX_SOURCE_CHANNEL_COUNT
 * @brief Defines the maximum supported number of source audio channels
 *        referenced by audioContent
 */
#define DLB_PAMC_SADM_MAX_SOURCE_CHANNEL_COUNT              (24)

/**
 * @def DLB_PAMC_SADM_MAX_LABEL_COUNT
 * @brief Defines the maximum supported number of labels in an audioProgramme
 */
#define DLB_PAMC_SADM_MAX_LABEL_COUNT                       (4)

/**
 * @def DLB_PAMC_SADM_MAX_AUDIO_CONTENT_IN_PROGRAMME_COUNT
 * @brief Defines the maximum supported number of audioContents in an audioProgramme
 */
#define DLB_PAMC_SADM_MAX_AUDIO_CONTENT_IN_PROGRAMME_COUNT  (2)


/**
 * @defgroup STD_PARAM_ENUM Parameter Value Enumerations
 * @{
 */

/**
 * @typedef dlb_pamc_sadm_non_dialogue_content_kind
 * @brief enum used to indicate type of nonDialogueContentKind dialogue
 */
typedef enum
{
    DLB_PAMC_SADM_NON_DIALOGUE_CONTENT_KIND_UNDEFINED = 0,
    DLB_PAMC_SADM_NON_DIALOGUE_CONTENT_KIND_MUSIC,
    DLB_PAMC_SADM_NON_DIALOGUE_CONTENT_KIND_EFFECT,
}
dlb_pamc_sadm_non_dialogue_content_kind;

/**
 * @typedef dlb_pamc_sadm_dialogue_content_kind
 * @brief enum used to indicate type of dialogueContentKind dialogue
 */
typedef enum
{
    DLB_PAMC_SADM_DIALOGUE_CONTENT_KIND_UNDEFINED = 0,
    DLB_PAMC_SADM_DIALOGUE_CONTENT_KIND_DIALOGUE,
    DLB_PAMC_SADM_DIALOGUE_CONTENT_KIND_VOICEOVER,
    DLB_PAMC_SADM_DIALOGUE_CONTENT_KIND_SPOKEN_SUBTITLE,
    DLB_PAMC_SADM_DIALOGUE_CONTENT_KIND_AUDIO_DESCRIPTION,
    DLB_PAMC_SADM_DIALOGUE_CONTENT_KIND_COMMENTARY,
    DLB_PAMC_SADM_DIALOGUE_CONTENT_KIND_EMERGENCY
}
dlb_pamc_sadm_dialogue_content_kind;

/**
 * @typedef dlb_pamc_sadm_mixed_content_kind
 * @brief enum used to indicate type of mixedContentKind dialogue
 */
typedef enum
{
    DLB_PAMC_SADM_MIXED_CONTENT_KIND_UNDEFINED = 0,
    DLB_PAMC_SADM_MIXED_CONTENT_KIND_COMPLETE_MAIN,
    DLB_PAMC_SADM_MIXED_CONTENT_KIND_MIXED,
    DLB_PAMC_SADM_MIXED_CONTENT_KIND_HEARING_IMPAIRED,
    DLB_PAMC_SADM_MIXED_CONTENT_KIND_VISUALLY_IMPAIRED
}
dlb_pamc_sadm_mixed_content_kind;

/**
 * @typedef dlb_pamc_sadm_content_kind
 * @brief enum used to indicate content kind of dialogue
 */
typedef enum
{
    DLB_PAMC_CONTENT_KIND_NON_DIALOGUE = 0,
    DLB_PAMC_CONTENT_KIND_DIALOGUE,
    DLB_PAMC_CONTENT_KIND_MIXED
}
dlb_pamc_sadm_content_kind;

/**
 * @typedef dlb_pamc_sadm_channel_configuration
 * @brief enum used to indicate channel configuration of audioObject
 */
typedef enum
{
    DLB_PAMC_SADM_CHANNEL_CONFIGURATION_1_0_POSITION_DEPENDENT = 0, /**< Position dependent */
    DLB_PAMC_SADM_CHANNEL_CONFIGURATION_1_0,                        /**< AP_00010001 */
    DLB_PAMC_SADM_CHANNEL_CONFIGURATION_2_0,                        /**< AP_00010002 */
    DLB_PAMC_SADM_CHANNEL_CONFIGURATION_3_0,                        /**< AP_0001000a */
    DLB_PAMC_SADM_CHANNEL_CONFIGURATION_5_1,                        /**< AP_00010003 */
    DLB_PAMC_SADM_CHANNEL_CONFIGURATION_5_0,                        /**< AP_0001000c */
    DLB_PAMC_SADM_CHANNEL_CONFIGURATION_7_1_BACK,                   /**< AP_0001000f */
    DLB_PAMC_SADM_CHANNEL_CONFIGURATION_7_1_TOP,                    /**< AP_00010004 */
    DLB_PAMC_SADM_CHANNEL_CONFIGURATION_5_1_4,                      /**< AP_00010005 */
    DLB_PAMC_SADM_CHANNEL_CONFIGURATION_7_1_4,                      /**< AP_00010017 */
    DLB_PAMC_SADM_CHANNEL_CONFIGURATION_22_2,                       /**< AP_00010009 */
}
dlb_pamc_sadm_channel_configuration;

/**
 * @typedef dlb_pamc_sadm_frame_rate
 * @brief enum used to indicate frame rate
 */
typedef enum
{
    DLB_PAMC_SADM_FRAME_RATE_UNKNOWN = 0, /**< Frame rate cannot be calculated */
    DLB_PAMC_SADM_FRAME_RATE_23_44,       /**< 23.44 fps */
    DLB_PAMC_SADM_FRAME_RATE_23_98,       /**< 23.98 fps */
    DLB_PAMC_SADM_FRAME_RATE_24,          /**< 24 fps */
    DLB_PAMC_SADM_FRAME_RATE_25,          /**< 25 fps */
    DLB_PAMC_SADM_FRAME_RATE_29_97,       /**< 29.97 fps */
    DLB_PAMC_SADM_FRAME_RATE_30,          /**< 30 fps */
    DLB_PAMC_SADM_FRAME_RATE_50,          /**< 50 fps */
    DLB_PAMC_SADM_FRAME_RATE_59_94,       /**< 59.94 fps */
    DLB_PAMC_SADM_FRAME_RATE_60           /**< 60 fps */
}
dlb_pamc_sadm_frame_rate;

/** @} */

/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter Data Structures
 * @{
 */

/**
 * @brief Structure containing information about serialized ADM format
 */
typedef struct dlb_pamc_sadm_metadata_properties_s
{
    dlb_pamc_sadm_frame_rate frame_rate;                        /** Frame rate */

    uint8_t metadata_flow_id_present;                           /** Flag indicating whether metadata flowID is present */
    char metadata_flow_id[DLB_PAMC_SADM_METADATA_ID_SIZE];      /** Metadata flowID */

    char sadm_frame_recommendation[DLB_PAMC_SADM_STRING_SIZE];  /** S-ADM frame recommendation */
    char adm_recommendation[DLB_PAMC_SADM_STRING_SIZE];         /** ADM recommendation */

    char metadata_profile_recommendation[DLB_PAMC_SADM_STRING_SIZE];/** Metadata profile recommendation */
    char metadata_profile_name[DLB_PAMC_SADM_STRING_SIZE];          /** Metadata profile name */
    char metadata_profile_version[DLB_PAMC_SADM_STRING_SIZE];       /** Metadata profile version */
    uint8_t metadata_profile_level;                                 /** Metadata profile level */
}
dlb_pamc_sadm_metadata_properties;

/**
 * @brief Structure containing information about serialized ADM audioContent
 */
typedef struct dlb_pamc_sadm_audio_content_s
{
    char id[DLB_PAMC_SADM_ID_SIZE];             /** ID of the audioContent */
    char name[DLB_PAMC_SADM_STRING_SIZE];       /** Name of the audioContent */
    char language[DLB_PAMC_SADM_LANGUAGE_SIZE]; /** Language of the audioContent */

    dlb_pamc_sadm_content_kind content_kind;    /** Content kind of dailogue sub-element of audioContent */
    union
    {
        dlb_pamc_sadm_non_dialogue_content_kind non_dialogue_content_kind;
        dlb_pamc_sadm_dialogue_content_kind dialogue_content_kind;
        dlb_pamc_sadm_mixed_content_kind mixed_content_kind;
    }dialogue_type;  /** Type of dialogue. Type needs to be interpreted according to field @ref content_kind */

    float gain;                     /** Gain in dB of the audioObject */
    float gain_adjusted;            /** Gain value in dB adjusted to match supported values
                                      * after processing (clamped or rounded) by the pro audio metadata component
                                      */
    float pos_x;                    /** X position of the audioObject */
    float pos_y;                    /** Y position of the audioObject */
    float pos_z;                    /** Z position of the audioObject */

    dlb_pamc_sadm_channel_configuration channel_configuration;       /** Channel configuration of the audioObject */

    uint8_t num_audio_channels;     /** Number of audio channels referenced by audioObject */
    uint8_t source_channels[DLB_PAMC_SADM_MAX_SOURCE_CHANNEL_COUNT]; /** Audio channel IDs referenced by the audioObject */
}
dlb_pamc_sadm_audio_content;

/**
 * @brief Structure containing information about serialized ADM label and
 *        associated language
 */
typedef struct dlb_pamc_sadm_label_s
{
    char text[DLB_PAMC_SADM_STRING_SIZE];
    char language[DLB_PAMC_SADM_LANGUAGE_SIZE];
}
dlb_pamc_sadm_label;

/**
 * @brief Structure containing information about audioContent referenced by audioProgramme
 */
typedef struct dlb_pamc_sadm_referenced_audio_content_s
{
    char id[DLB_PAMC_SADM_ID_SIZE];             /** ID of the audioContent referenced by audioProgramme */
    char name[DLB_PAMC_SADM_STRING_SIZE];       /** Name of the audioContent referenced by audioProgramme */
}
dlb_pamc_sadm_referenced_audio_content;

/**
 * @brief Structure containing information about serialized ADM audioProgramme
 */
typedef struct dlb_pamc_sadm_programme_s
{
    char id[DLB_PAMC_SADM_ID_SIZE];             /** ID of the audioProgramme */
    char name[DLB_PAMC_SADM_STRING_SIZE];       /** Name of the audioProgramme */
    char language[DLB_PAMC_SADM_LANGUAGE_SIZE]; /** Language of the audioProgramme */

    uint8_t num_labels;                                         /** Number of labels in the audioProgramme */
    dlb_pamc_sadm_label labels[DLB_PAMC_SADM_MAX_LABEL_COUNT];  /** Labels in the audioProgramme */

    uint8_t num_audio_contents; /** Number of audioContents referenced in the audioProgramme */
    dlb_pamc_sadm_referenced_audio_content referenced_audio_contents[DLB_PAMC_SADM_MAX_AUDIO_CONTENT_IN_PROGRAMME_COUNT]; /** AudioContents referenced by the audioProgramme */
}
dlb_pamc_sadm_programme;

/**
 * @brief Structure containing information about serialized ADM metadata properties
 */
typedef struct dlb_pamc_ui_md_sadm_s
{
    dlb_pamc_sadm_metadata_properties metadata_properties;      /**< Metadata properties */

    uint8_t num_audio_contents;  /** Number of audioContents */
    dlb_pamc_sadm_audio_content audio_contents[DLB_PAMC_SADM_MAX_AUDIO_CONTENT_COUNT];

    uint8_t num_audio_programmes; /** Number of audioProgrammes */
    dlb_pamc_sadm_programme programmes[DLB_PAMC_SADM_MAX_AUDIO_PROGRAM_COUNT];
}
dlb_pamc_ui_md_sadm;

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* __DLB_PAMC_UI_SADM */
