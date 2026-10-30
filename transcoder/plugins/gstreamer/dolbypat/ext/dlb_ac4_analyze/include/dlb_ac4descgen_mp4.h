/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                Copyright (C) 2018-2023 by Dolby International AB.
 *                            All rights reserved.
 ******************************************************************************/

/**
 *  @file
 *
 *  @brief AC-4 Descriptor Generator for MP4.
 */

/**
 * @ingroup mp4_desc_gen_api
 * @{
 */

#ifndef DLB_AC4DESCGEN_MP4_H
#define DLB_AC4DESCGEN_MP4_H

#include "dlb_ac4analyze.h"
#include <inttypes.h>
#include <stdio.h>

#define AC4DESCGEN_MAX_NUM_PRESENTATIONS (AC4ANALYZE_MAX_NUM_SUPPORTED_PRESENTATIONS)
#define AC4DESCGEN_MAX_NUM_SUBSTREAMS_PER_PRESENTATION (3)
#define AC4DESCGEN_MAX_NUM_EMDF_SUBSTREAMS_PER_PRESENTATION (128)
#define AC4DESCGEN_MAX_LANGUAGE_TAG_BYTES (63)
#define AC4DESCGEN_MAX_PRESNAME_LEN_BYTES (32+1)

#define AC4DESCGEN_BITRATE_MODE_NOT_SPECIFIED      (0)
#define AC4DESCGEN_BITRATE_MODE_CONSTANT_BIT_RATE  (1)
#define AC4DESCGEN_BITRATE_MODE_AVERAGE_BIT_RATE   (2)
#define AC4DESCGEN_BITRATE_MODE_VARIABLE_BIT_RATE  (3)
#define AC4DESCGEN_BITRATE_UNKNOWN            (0)
#define AC4DESCGEN_BITRATE_PRECISION_UNKNOWN  (0xFFFFFFFF)

#define AC4DESCGEN_ERROR_CODE_OK                                              (0)
#define AC4DESCGEN_ERROR_CODE_INVALID_CHANNEL_MODE                            (1)
#define AC4DESCGEN_ERROR_CODE_DSI_V0_NOT_SUPPORTED                            (2)
#define AC4DESCGEN_ERROR_CODE_INVALID_PROGRAM_ID_AVAILABILITY                 (3)
#define AC4DESCGEN_ERROR_CODE_UNSUPPORTED_PRESENTATION_VERSION                (4)
#define AC4DESCGEN_ERROR_CODE_INVALID_PRESENTATION_CONFIG                     (5)
#define AC4DESCGEN_ERROR_CODE_SUBSTREAM_INDEX_OUT_OF_RANGE                    (6)
#define AC4DESCGEN_ERROR_CODE_SUBSTREAM_GROUP_INDEX_OUT_OF_RANGE              (7)
#define AC4DESCGEN_ERROR_CODE_PRESENTATION_INDEX_OUT_OF_RANGE                 (8)
#define AC4DESCGEN_ERROR_CODE_TOO_MANY_EMDF_SUBSTREAMS                        (9)
#define AC4DESCGEN_ERROR_CODE_LANGUAGE_TAG_TOO_LONG                          (10)
#define AC4DESCGEN_ERROR_CODE_NOT_ENOUGH_SUBSTREAMS_IN_PRESENTATION          (11)
#define AC4DESCGEN_ERROR_CODE_TOO_MANY_TARGETS                               (12)
#define AC4DESCGEN_ERROR_CODE_INVALID_PRESENTATION_ID                        (13)
#define AC4DESCGEN_ERROR_CODE_INVALID_FRAME_RATE_FRACTION                    (14)
#define AC4DESCGEN_ERROR_CODE_CANNEL_MODE_NOT_VALID_FOR_PRESENTATION_VERSION (15)
#define AC4DESCGEN_ERROR_CODE_UNSUPPORTED_BITSTREAM_VERSION                  (16)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * ETSI TS 103 270 190-1 V1.3.1 (2017-06), Table E.5
 * ETSI TS 103 270 190-2 V1.2.1 (2017-10), E.9.1
 */
typedef struct dlb_ac4descgen_mp4_ac4_substream_dsi_s
{
    uint8_t  channel_mode;                  /**< 5 bit */
    uint8_t  dsi_sf_multiplier;             /**< 2 bit */
#ifdef UNTESTED_CODE
    uint8_t  b_substream_bitrate_indicator; /**< 1 bit */
    uint8_t  substream_bitrate_indicator;   /**< 5 bit */
#endif
    uint8_t  add_ch_base;                   /**< 1 bit */
    uint8_t  b_content_type;                /**< 1 bit */
    uint8_t  content_classifier;            /**< 3 bit */
    uint8_t  b_language_indicator;          /**< 1 bit */
    uint8_t  n_language_tag_bytes;          /**< 6 bit */
    unsigned char language_tag_byte[AC4DESCGEN_MAX_LANGUAGE_TAG_BYTES];
} dlb_ac4descgen_mp4_ac4_substream_dsi_t;

/**
 * ETSI TS 103 270 190-2 V1.2.1 (2017-10), E.12.1
 */
typedef struct dlb_ac4descgen_mp4_alternative_info_target_s
{
    uint8_t target_md_compat;            /**< 3 bit */
    uint8_t target_device_category;      /**< 8 bit */
} dlb_ac4descgen_mp4_alternative_info_target_t;

/**
 * ETSI TS 103 270 190-2 V1.2.1 (2017-10), E.12.1
 */
typedef struct dlb_ac4descgen_mp4_alternative_info_s
{
    uint16_t name_len;                   /**< 16 bit */
    uint8_t presentation_name[AC4DESCGEN_MAX_PRESNAME_LEN_BYTES];    /**< 8 * name_len bit */
    uint8_t n_targets;                   /**< 5 bit */
    dlb_ac4descgen_mp4_alternative_info_target_t  target[31];
} dlb_ac4descgen_mp4_alternative_info_t;

/**
 * ETSI TS 103 270 190-2 V1.2.1 (2017-10), E.11.1
 */
typedef struct dlb_ac4descgen_mp4_ac4_substream_group_dsi_substream_s
{
    uint8_t dsi_sf_multiplier;                     /**< 2 bit */
#ifdef UNTESTED_CODE
    uint8_t b_substream_bitrate_indicator;         /**< 1 bit */
    uint8_t substream_bitrate_indicator;           /**< 5 bit */
#endif
    uint32_t dsi_substream_channel_mask;           /**< 24 bit */
    uint8_t b_ajoc;                                /**< 1 bit */
    uint8_t b_static_dmx;                          /**< 1 bit */
    uint8_t n_dmx_objects_minus1;                  /**< 4 bit */
    uint8_t n_umx_objects_minux1;                  /**< 6 bit */
    uint8_t b_substream_contains_bed_objects;      /**< 1 bit */
    uint8_t b_substream_contains_dynamic_objects;  /**< 1 bit */
    uint8_t b_substream_contains_ISF_objects;      /**< 1 bit */
} dlb_ac4descgen_mp4_ac4_substream_group_dsi_substream_t;

/**
 * ETSI TS 103 270 190-2 V1.2.1 (2017-10), E.11.1
 */
typedef struct dlb_ac4descgen_mp4_ac4_substream_group_dsi_t
{
    uint8_t b_substreams_present;    /**< 1 bit */
    uint8_t b_hsf_ext;               /**< 1 bit */
    uint8_t b_channel_coded;         /**< 1 bit */
    uint8_t n_substreams;            /**< 8 bit */
    dlb_ac4descgen_mp4_ac4_substream_group_dsi_substream_t substream[255];
    uint8_t b_content_type;          /**< 1 bit */
    uint8_t content_classifier;      /**< 3 bit */
    uint8_t b_language_indicator;    /**< 1 bit */
    uint8_t n_language_tag_bytes;    /**< 6 bit */
    uint8_t language_tag_bytes[63];  /**< n_language_tag_bytes * 8 bit */
} dlb_ac4descgen_mp4_ac4_substream_group_dsi_t;

/**
 * ETSI TS 103 270 190-2 V1.2.1 (2017-10), E.7.1
 */
typedef struct dlb_ac4descgen_mp4_ac4_bitrate_dsi_s
{
    uint8_t    bit_rate_mode;         /**< 2 bit */
    uint32_t   bit_rate;              /**< 32 bit */
    uint32_t   bit_rate_precision;    /**< 32 bit */
    int        bit_position_of_estimate_start;
    int        bit_position_of_estimate_end;
} dlb_ac4descgen_mp4_ac4_bitrate_dsi_t;

#ifdef UNTESTED_CODE
/**
 * ETSI TS 103 270 190-1 V1.3.1 (2017-06)
 * Table E.4
 */
typedef struct dlb_ac4descgen_mp4_ac4_dsi_presentation_v0_s
{
    uint8_t   b_single_substream;           /**< 1 bit */
    uint8_t   presentation_config;          /**< 5 bit */
    uint8_t   presentation_version;         /**< 5 bit */
    uint8_t   mdcompat;                     /**< 3 bit */
    uint8_t   b_belongs_to_presentation_id; /**< 1 bit */
    int8_t    presentation_id;              /**< 5 bit, -1: invalid */
    uint8_t   dsi_frame_rate_multiply_info; /**< 2 bit */
    uint8_t   b_hsf_ext;                    /**< 1 bit */
    dlb_ac4descgen_mp4_ac4_substream_dsi_t substream[AC4DESCGEN_MAX_NUM_SUBSTREAMS_PER_PRESENTATION];
    uint8_t   n_skip_bytes;                 /**< 7 bit */
    uint8_t   b_pre_virtualized;            /**< 1 bit */
    uint8_t   b_add_emdf_substream;         /**< 1 bit */
    uint8_t   n_add_emdf_substreams;        /**< 7 bit */
    uint8_t   emdf_version[AC4DESCGEN_MAX_NUM_EMDF_SUBSTREAMS_PER_PRESENTATION]; /**<  5 bit */
    uint8_t   key_id[AC4DESCGEN_MAX_NUM_EMDF_SUBSTREAMS_PER_PRESENTATION];       /**< 10 bit */
} dlb_ac4descgen_mp4_ac4_dsi_presentation_part1_t;
#endif

/**
 * ETSI TS 103 270 190-2 V1.2.1 (2017-10), E.8.1
 */
typedef struct dlb_ac4descgen_mp4_ac4_presentation_v0_dsi_s
{
    uint8_t   presentation_config;          /**< 5 bit */
    uint8_t   mdcompat;                     /**< 3 bit */
    uint8_t   b_presentation_id;            /**< 1 bit */
    uint8_t   presentation_id;              /**< 5 bit */
    uint8_t   dsi_frame_rate_multiply_info; /**< 2 bit */
    uint8_t   presentation_emdf_version;    /**< 5 bit */
    uint16_t  presentation_key_id;          /**< 10 bit */
    uint32_t  presentation_channel_mask;    /**< 24 bit */
    uint8_t   b_single_substream;           /**< none */
    dlb_ac4descgen_mp4_ac4_substream_dsi_t substream[3];
    uint8_t   b_hsf_ext;                    /**< 1 bit */
    uint8_t   n_skip_bytes;                 /**< 7 bit */
    uint8_t   b_pre_virtualized;            /**< 1 bit */
    uint8_t   b_add_emdf_substream;         /**< 1 bit */
    uint8_t   n_add_emdf_substreams;        /**< 7 bit */
    uint8_t   substream_emdf_version[AC4DESCGEN_MAX_NUM_EMDF_SUBSTREAMS_PER_PRESENTATION]; /**<  5 bit */
    uint8_t   substream_key_id[AC4DESCGEN_MAX_NUM_EMDF_SUBSTREAMS_PER_PRESENTATION];       /**< 10 bit */
} dlb_ac4descgen_mp4_ac4_presentation_v0_dsi_t;

/**
 * ETSI TS 103 270 190-2 V1.2.1 (2017-10), E.10.1
 */
typedef struct dlb_ac4descgen_mp4_ac4_presentation_v1_dsi_s
{
    uint8_t   presentation_config_v1;             /**< 5 bit */
    uint8_t   mdcompat;                           /**< 3 bit */
    uint8_t   b_presentation_id;                  /**< 1 bit */
    uint8_t   presentation_id;                    /**< 5 bit */
    uint8_t   dsi_frame_rate_multiply_info;       /**< 2 bit */
    uint8_t   dsi_frame_rate_fraction_info;       /**< 2 bit */
    uint8_t   presentation_emdf_version;          /**< 5 bit */
    uint16_t  presentation_key_id;                /**< 10 bit */
    uint8_t   b_presentation_channel_coded;       /**< 1 bit */
    uint8_t   dsi_presentation_ch_mode;           /**< 5 bit */
    uint8_t   pres_b_4_back_channels_present;     /**< 1 bit */
    uint8_t   pres_top_channel_pairs;             /**< 2 bit */
    uint32_t  presentation_channel_mask_v1;       /**< 24 bit */
    uint8_t   b_presentation_core_differs;        /**< 1 bit */
    uint8_t   b_presentation_core_channel_coded;  /**< 1 bit */
    uint8_t   dsi_presentation_channel_mode_core; /**< 2 bit */
    uint8_t   b_presentation_filter;              /**< 1 bit */
    uint8_t   b_enable_presentation;              /**< 1 bit */
    uint8_t   n_filter_bytes;                     /**< 8 bit */
    uint8_t   filter_data[256];                   /**< 8 * n_filter_bytes bit */
    uint8_t   b_multi_pid;                        /**< 1 bit */
    uint8_t   n_substream_groups_minus2;          /**< 3 bit */
    dlb_ac4descgen_mp4_ac4_substream_group_dsi_t  substream_group[9];
    uint8_t   n_skip_bytes;                       /**< 7 bit */
    uint8_t   b_pre_virtualized;                  /**< 1 bit */
    uint8_t   b_add_emdf_substream;               /**< 1 bit */
    uint8_t   n_add_emdf_substreams;              /**< 7 bit */
    uint8_t   substream_emdf_version[AC4DESCGEN_MAX_NUM_EMDF_SUBSTREAMS_PER_PRESENTATION]; /**<  5 bit */
    uint8_t   substream_key_id[AC4DESCGEN_MAX_NUM_EMDF_SUBSTREAMS_PER_PRESENTATION];       /**< 10 bit */
    uint8_t   b_presentation_bitrate_info;        /**< 1 bit */
    dlb_ac4descgen_mp4_ac4_bitrate_dsi_t  ac4_bitrate_dsi;
#ifdef UNTESTED_CODE
    uint8_t   b_alternative;                      /**< 1 bit */
    dlb_ac4descgen_mp4_alternative_info_t  alternative_info;
#endif
    uint8_t   de_indicator;                       /**< 1 bit */
    uint8_t   dolby_atmos_indicator;              /**< 1 bit */
    uint8_t   b_extended_presentation_id;         /**< 1 bit */
    uint16_t  extended_presentation_id;           /**< 9 bit */
} dlb_ac4descgen_mp4_ac4_presentation_v1_dsi_t;


/**
 * ETSI TS 103 270 190-2 V1.2.1 (2017-10), E.5.1
 */
typedef struct dlb_ac4descgen_mp4_ac4_dsi_presentation_v1_s
{
    uint8_t   presentation_version;   /**< 8 bit */
    uint8_t   pres_bytes;             /**< 8 bit */
    uint16_t  add_pres_bytes;         /**< 16 bit */
    dlb_ac4descgen_mp4_ac4_presentation_v0_dsi_t v0_dsi;
    dlb_ac4descgen_mp4_ac4_presentation_v1_dsi_t v1_dsi;
} dlb_ac4descgen_mp4_ac4_dsi_presentation_v1_t;

#ifdef UNTESTED_CODE
/**
 * ETSI TS 103 270 190-1 V1.3.1 (2017-06)
 * Table E.4
 */
typedef struct dlb_ac4descgen_mp4_ac4_dsi_part1_s
{
    uint8_t   ac4_dsi_version;       /**< 3 bit */
    uint8_t   bitstream_version;     /**< 7 bit */
    uint8_t   fs_index;              /**< 1 bit */
    uint8_t   frame_rate_index;      /**< 4 bit */
    uint16_t  n_presentations;       /**< 9 bit */
    dlb_ac4descgen_mp4_ac4_dsi_presentation_part1_t presentation[AC4DESCGEN_MAX_NUM_PRESENTATIONS];
} dlb_ac4descgen_mp4_ac4_dsi_part1_t;
#endif

/**
 * ETSI TS 103 270 190-2 V1.2.1 (2017-10), E.6.1
 */
typedef struct dlb_ac4descgen_mp4_ac4_dsi_v1_s
{
    uint8_t   ac4_dsi_version;       /**< 3 bit */
    uint8_t   bitstream_version;     /**< 7 bit */
    uint8_t   fs_index;              /**< 1 bit */
    uint8_t   frame_rate_index;      /**< 4 bit */
    uint16_t  n_presentations;       /**< 9 bit */
    uint8_t   b_program_id;          /**< 1 bit */
    uint16_t  short_program_id;      /**< 16 bit */
    uint8_t   b_uuid;                /**< 1 bit */
    uint8_t   program_uuid[16];      /**< 16 * 8 bit */
    dlb_ac4descgen_mp4_ac4_bitrate_dsi_t  ac4_bitrate_dsi;
    dlb_ac4descgen_mp4_ac4_dsi_presentation_v1_t presentation[AC4DESCGEN_MAX_NUM_PRESENTATIONS];
} dlb_ac4descgen_mp4_ac4_dsi_v1_t;

/**
 * ETSI TS 103 270 190-1 V1.3.1 (2017-06), Table E.3
 * ETSI TS 103 270 190-2 V1.2.1 (2017-10), E.5.1
 */
typedef union dlb_ac4descgen_mp4_Ac4SpecificBox_s
{
#ifdef UNTESTED_CODE
    dlb_ac4descgen_mp4_ac4_dsi_part1_t         ac4_dsi_part1;
#endif
    dlb_ac4descgen_mp4_ac4_dsi_v1_t            ac4_dsi_v1;
} dlb_ac4descgen_mp4_Ac4SpecificBox_t;

/**
 * ETSI TS 103 270 190-2 V1.2.1 (2017-10), E.5a.1
 */
typedef struct dlb_ac4descgen_mp4_Ac4PresentationLabelBoxElem_s
{
    uint16_t  presentation_id;        /**< 9 bit */
    char*     presentation_label;     /**< zero terminated string */
} dlb_ac4descgen_mp4_Ac4PresentationLabelBoxELem_t;

/**
 * ETSI TS 103 270 190-2 V1.2.1 (2017-10), E.5a.1
 */
typedef struct dlb_ac4descgen_mp4_Ac4PresentationLabelBox_s
{
    uint8_t   Version;            /**< 8 bit */
    uint32_t  Flags;              /**< 24 bit */
    uint16_t  num_presentation_labels;  /**< 9 bit */
    dlb_ac4descgen_mp4_Ac4PresentationLabelBoxELem_t presentation[AC4DESCGEN_MAX_NUM_PRESENTATIONS];
} dlb_ac4descgen_mp4_Ac4PresentationLabelBox_t;

/**
 * @brief Struct that discribes the AC-4 Sample Entry box, i.e. the MP4 descriptor for an AC-4 elementary stream.
 *
 * ETSI TS 103 270 190-1 V1.3.1 (2017-06), Table E.2
 * ETSI TS 103 270 190-2 V1.2.1 (2017-10), E.4.1
 */
typedef struct dlb_ac4descgeb_mp4_AC4SampleEntry_s
{
    uint16_t   DataReferenceIndex;   /**< 16 bit */
    uint16_t   ChannelCount;         /**< 16 bit */
    uint16_t   SamplingFrequency;    /**< 16 bit */
    dlb_ac4descgen_mp4_Ac4SpecificBox_t           Ac4SpecificBox;
    dlb_ac4descgen_mp4_Ac4PresentationLabelBox_t  Ac4PresentationLabelBox;   /**< Part 2 only */
} dlb_ac4descgen_mp4_AC4SampleEntry_t;

/**
 * @brief Context information to get additional information needed to write the AC-4 Sample Entry box.
 */
typedef struct dlb_ac4descgen_mp4_context_s
{
    char* (*get_presentation_label)(int presentation_id);        /**< Callback funtion providing a presentation label.
                                                                  *   The AC-4 descriptor generator library does not copy the
                                                                  *   string.
                                                                  */
    dlb_ac4descgen_mp4_ac4_bitrate_dsi_t *p_ac4_bitrate_dsi_cfg; /**< AC-4 bitrate information, i.e. encoder configuration. */
} dlb_ac4descgen_mp4_context_t;

/**
 * @brief Initialize AC-4 Sample Entry data structure.
 *
 * This function initializes the dlb_ac4descgen_mp4_AC4SampleEntry_t data structure.
 *
 */
void
dlb_ac4descgen_init_mp4_descriptor
    (dlb_ac4descgen_mp4_AC4SampleEntry_t    *AC4SampleEntry    /**< [out] AC-4 Sample Entry data structure. */
    );
    
/**
 * @brief Update AC-4 Sample Entry data structure.
 *
 * This function updates the dlb_ac4descgen_mp4_AC4SampleEntry_t data structure with AC-4 frame data as extracted from the
 * AC-4 bitstream by the AC-4 Analyzer. The user optionally can provide additional information: The actual bitrate the encoder
 * was configured with. Zero terminated UTF-8 strings with presentation labels.
 *
 * @return error code
 */
int
dlb_ac4descgen_update_AC4SampleEntry
    (dlb_ac4descgen_mp4_AC4SampleEntry_t  *AC4SampleEntry   /**< [in,out] AC-4 Sample Entry data structure. */
    ,ac4analyze_frame_data_t              *ac4_frame        /**< [in]     AC-4 frame data as extracted by the AC-4 analyzer. */
    ,dlb_ac4descgen_mp4_context_t         *context          /**< [in]     Provides additional information not in AC-4 bitstream. */
    );

/**
 * @brief Write AC4SampleEntry box into the provided buffer.
 *
 * The function expects that the allocated buffer is large enough. A return value of 0 indicates a failure, i.e. buffer
 * size was too small.
 *
 * @return Number of bytes written.
 */
unsigned int
dlb_ac4descgen_write_mp4_ac4_sample_entry
    (void                                 *p_mem           /**< [out]    Serialized descriptor is written into this buffer */
    ,unsigned int                          mem_size        /**< [in]     Size of the provided buffer. */
    ,dlb_ac4descgen_mp4_AC4SampleEntry_t  *AC4SampleEntry  /**< [in]     AC-4 Sample Entry data structure. */
    );
    
#define AC4DESCGEN_CMP_BIT_EXACT_IDENTICAL                1
#define AC4DESCGEN_CMP_ONLY_ESTIMATED_VALUES_DIFFERENT    2
#define AC4DESCGEN_CMP_NOT_ESTIMATED_VALUES_DIFFERENT     3

/**
 * @brief Compares two AC-4 sample entry boxes.
 *
 * The function compares 2 encoded AC-4 Sample Entry boxes with each other. The function determines, if (1) both boxes
 * are bit exact identical, (2) only estimated bitrate values are different, or (3) not estimated values are different.
 * The dlb_ac4descgen_mp4_AC4SampleEntry_t structure contains the position of the estimated bitrate values. This
 * position information is written by the dlb_ac4descgen_write_mp4_ac4_sample_entry function.
 *
 * @return compare_result (1,2,3)
 */

int
dlb_ac4descgen_compare_mp4_ac4_sample_entry
    (dlb_ac4descgen_mp4_AC4SampleEntry_t  *p_ac4_sample_entry
    ,unsigned char                        *p_desc_last
    ,unsigned int                          desc_size_last
    ,unsigned char                        *p_desc_prev
    ,unsigned int                          desc_size_prev
    );

#ifdef __cplusplus
}
#endif

#endif

/**
 *  @}
 */
