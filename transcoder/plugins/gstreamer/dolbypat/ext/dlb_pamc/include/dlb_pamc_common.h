/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                  Copyright (C) 2023-2024 by Dolby Laboratories.
 *                            All rights reserved.
 ******************************************************************************/
#ifndef DLB_PAMC_COMMON_H
#define DLB_PAMC_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @def DLB_PAMC_MD_DURATION_AUTO
 * @brief Defines whether the PAMC component should determine metadata duration from the incoming metadata.
 */
#define DLB_PAMC_MD_DURATION_AUTO                     ((int32_t)0)

/**
 * @brief Opaque Dolby PAMC state
 */
typedef struct dlb_pamc_state_s dlb_pamc_state;

/**
 * @brief Library version structure
 */
typedef struct dlb_pamc_version_s
{
    unsigned int v_major;    /**< Major release */
    unsigned int v_minor;    /**< Minor release */
    unsigned int v_func;     /**< Functional release */
    unsigned int v_build;    /**< Build number */
} dlb_pamc_version;

typedef enum
{
    DLB_PAMC_WARNING_COUNT_REACHED = 1,                         /**< More warnings reported than warnings struct can handle,
                                                                     there may be not reported warnings occuring */
    DLB_PAMC_WARNING_DURATION_INVALID,                          /**< Metadata duration is invalid */
    DLB_PAMC_WARNING_PARSE_XML,                                 /**< XML parsing failed */
    DLB_PAMC_WARNING_INGEST_XML,                                /**< XML ingestion failed */
    DLB_PAMC_WARNING_CONVERSION_ERROR,                          /**< Conversion failed */
    DLB_PAMC_WARNING_UNSUPPORTED_CONFIGURATION,                 /**< Unsupported configuration */
    DLB_PAMC_WARNING_UNSUPPORTED_CHANNELS_MAPPING,              /**< Unsupported channel mapping,
                                                                     several objects or beds use the same channels */
    DLB_PAMC_WARNING_UNSUPPORTED_NUMBER_OF_OBJECTS,             /**< Unsupported number of objects */
    DLB_PAMC_WARNING_MISSING_DIALOGUE,                          /**< Alternative Dialogue with AD must have at least one dialogue
                                                                     in same configuration*/
    DLB_PAMC_WARNING_UNSUPPORTED_CONTENT_KIND,                  /**< Only Dialogue and Audio Description is supported as object content kind */
    DLB_PAMC_WARNING_UNKNOWN_CONTENT_KIND,                      /**< Unknown content kind is used */
    DLB_PAMC_WARNING_UNSUPPORTED_NUMBER_OF_BEDS,                /**< Too many beds declared, classifiers may reduce number of allowed beds */
    DLB_PAMC_WARNING_UNSUPPORTED_BED_COMBINATION,               /**< Illegal combination of different beds classifiers */
    DLB_PAMC_WARNING_BROADCAST_MIX_MISSING_COMPLETE_MAIN,       /**< Broadcast mix without corresponding CM */
    DLB_PAMC_WARNING_UNSUPPORTED_BED_CHANNEL_CONFIG,            /**< Illegal channel config selected for bed */
    DLB_PAMC_WARNING_UNSUPPORTED_OBJECT_CHANNEL_CONFIG,         /**< Illegal channel config selected for object */
    DLB_PAMC_WARNING_UNSUPPORTED_BROADCAST_MIX_CHANNEL_CONFIG,  /**< Illegal channel config selected for broadcast mix */
    DLB_PAMC_WARNING_TOO_MANY_OBJECTS_IN_PROGRAM,               /**< Too many objects in single program */
    DLB_PAMC_WARNING_TOO_MANY_BEDS_IN_PROGRAM,                  /**< Too many beds in single program */
    DLB_PAMC_WARNING_GAIN_IGNORED,                              /**< Gain considered only for Dialogue or Alternative Dialogue with Audio Description objects, or Music and Effects */
    DLB_PAMC_WARNING_POSITION_IGNORED,                          /**< Position different from default or not cartesian, default applied */
    DLB_PAMC_WARNING_UNSUPPORTED_NUMBER_OF_CHANNELS,            /**< More than maximum number of channels reached */
    DLB_PAMC_WARNING_BED_CLASSIFIER_CHANGED,                    /**< Only M&E is valid classifier for dialogues, CM adjusted */
    DLB_PAMC_WARNING_BROADCAST_MIX_WITH_OBJECTS_UNSUPPORTED,    /**< Broadcast mix cannot be used with objects */
    DLB_PAMC_WARNING_INCORRECT_SADM_PROFILE_VALUES,             /**< SADM profile attribute is not compliant with emission profile */
    DLB_PAMC_WARNING_INVALID_SPEAKER_LABEL,                     /**< Invalid/unknown speaker label */
    DLB_PAMC_WARNING_NO_ELEMENTS_DETECTED,                      /**< No elements or no presentations detected, this might be due to entirely wrong XML structure */
    DLB_PAMC_WARNING_NO_CORRESPONDING_PRESENTATION_WITHOUT_AA,  /**< Presentation CM+AA is not accompanied by Presentation with only the same CM */
    DLB_PAMC_WARNING_UNSUPPORTED_NUMBER_OF_PRESENTATION_LABELS, /**< Unsupported number of presentation labels, only DLB_PAMC_SADM_MAX_LABEL_COUNT will be processed */
    DLB_PAMC_WARNING_GAIN_ADJUSTED,                             /**< The gain value is outside the permitted range or supported step. It has been adjusted to the nearest supported value. */
    DLB_PAMC_WARNING_NONZERO_GAIN_IN_PRESENTATION               /**< The presentation containing the first occurrence of bed and object component combination includes non-zero gain values.*/
} dlb_pamc_warning_codes;

/**
 * @brief Status return codes of the PAMC
 */
typedef enum
{
     DLB_PAMC_STATUS_OK                         = 0    /**< Function returned successfully. */
    ,DLB_PAMC_STATUS_WARNING                    = 1    /**< Function returned warning(s). */
    ,DLB_PAMC_ERROR_NULL_POINTER                = 100  /**< A Null pointer was passed as input. */
    ,DLB_PAMC_ERROR_INIT                        = 101  /**< Any parameter in p_init_config is out of range */
    ,DLB_PAMC_ERROR_INTERNAL                    = 102  /**< An internal error occurs */
    ,DLB_PAMC_ERROR_CONVERT                     = 103  /**< An error occurs during conversion */
    ,DLB_PAMC_ERROR_UNSUPPORTED_CONFIGURATION   = 104  /**< Unsupported configuration */
    ,DLB_PAMC_ERROR_MONITORING_METADATA_INVALID = 105  /**< Monitoring metadata is invalid */
    ,DLB_PAMC_ERROR_NO_EXTERNAL_MD_SETUP                 = 200  /**< dlb_pamc_get_default_external_md() was not called prior to dlb_pamc_external_md_convert(). */
    ,DLB_PAMC_ERROR_CHANNEL_CONFIG_OUT_OF_RANGE          = 201  /**< The \<channel_config\> external metadata parameter is out-of-range. */
    ,DLB_PAMC_ERROR_CONTENT_CLASSIFIER_OUT_OF_RANGE      = 202  /**< The \<content_classifier\> external metadata parameter is out-of-range. */
    ,DLB_PAMC_ERROR_DOWNMIX_MODE_OUT_OF_RANGE            = 203  /**< The \<dmixmod\> external metadata parameter is out-of-range. */
    ,DLB_PAMC_ERROR_LTRT_CENTER_DMX_LEVEL_OUT_OF_RANGE   = 204  /**< The \<ltrtcmixlev\> external metadata parameter is out-of-range. */
    ,DLB_PAMC_ERROR_LTRT_SURROUND_DMX_LEVEL_OUT_OF_RANGE = 205  /**< The \<ltrtsurmixlev\> external metadata parameter is out-of-range. */
    ,DLB_PAMC_ERROR_LORO_CENTER_DMX_LEVEL_OUT_OF_RANGE   = 206  /**< The \<lorocmixlev\> external metadata parameter is out-of-range. */
    ,DLB_PAMC_ERROR_LORO_SURROUND_DMX_LEVEL_OUT_OF_RANGE = 207  /**< The \<lorosurmixlev\> external metadata parameter is out-of-range. */
    ,DLB_PAMC_ERROR_SURROUND_PHASE_SHIFT_OUT_OF_RANGE    = 208  /**< The \<sur90on\> external metadata parameter is out-of-range. */
    ,DLB_PAMC_ERROR_DRC_HT_LINE_OUT_OF_RANGE             = 209  /**< The \<drc_ht_line\> external metadata parameter is out-of-range. */
    ,DLB_PAMC_ERROR_DRC_FP_RF_OUT_OF_RANGE               = 210  /**< The \<drc_fp_rf\> external metadata parameter is out-of-range. */
    ,DLB_PAMC_ERROR_DURATION_OUT_OF_RANGE                = 211  /**< The metadata duration passed to dlb_pamc_external_md_convert() is out-of-range. */
} dlb_pamc_status;

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PAMC_COMMON_H */
