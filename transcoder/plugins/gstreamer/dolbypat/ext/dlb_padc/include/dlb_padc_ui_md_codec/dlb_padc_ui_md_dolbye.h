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
 * @file  dlb_padc_ui_md_dolbye.h
 *
 * @brief File containing structures definitions for Dolby E UI metadata
 */

#ifndef __DLB_PADC_UI_DOLBYE_MD
#define __DLB_PADC_UI_DOLBYE_MD

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @def DLB_PADC_DOLBYE_MAX_PROGRAMS_NUM
 * @brief Defines maximum program amount available in bitstream
 */
#define DLB_PADC_DOLBYE_MAX_PROGRAMS_NUM                (8)

/**
 * @def DLB_PADC_DOLBYE_MAX_DESC_TEXT
 * @brief Defines maximum length of program description
 */
#define DLB_PADC_DOLBYE_MAX_DESC_TEXT                   (32)

/**
 * @def DLB_PADC_MAX_PGM_CFG_TEXT
 * @brief Defines maximum length of program configuration text
 */
#define DLB_PADC_MAX_PGM_CFG_TEXT                       (16)


/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter Data Structures
 * @{
 */

/**
 * @brief Structure containing information about Dolby E bitstream
 */
typedef struct dlb_padc_bitstream_properties_dolbye_s
{
    uint16_t frame_rate;                                    /**< Bitstream's frame rate */
    uint8_t bit_depth;                                      /**< Bitstream's bit depth */
    uint8_t selected_program;                               /**< Currently decoded program ID */
    uint8_t total_programs_num;                             /**< Total number of programs available in bitstream */
    char program_config_text[DLB_PADC_MAX_PGM_CFG_TEXT];    /**< Description of bitstream's program configuration, e.g. 5.1+2 */
    uint8_t program_config_id;                              /**< Bitstream's program configuration */
    uint8_t crc_error_present;                              /**< CRC error presence in the bitstream */
}
dlb_padc_bitstream_properties_dolbye;


/**
 * @brief Structure stores time code
 */
typedef struct dlb_padc_SMPTE_timecode_dolbye_s
{
    uint32_t tc1;                                    /**< tc1 bytes from SMTPE in BCD format */
    uint32_t tc2;                                    /**< tc2 bytes from SMTPE in BCD format */
    uint32_t tc3;                                    /**< tc3 bytes from SMTPE in BCD format */
    uint32_t tc4;                                    /**< tc4 bytes from SMTPE in BCD format */
}
dlb_padc_SMPTE_timecode_dolbye;


/**
 * @brief Structure provides information for bitstream configuration metadata
 */
typedef struct dlb_padc_program_md_dolbye_s
{
    int32_t desc_text;          /**< Program description */
    int32_t bsmod;              /**< Bitstream mode */
    int32_t rfpremphon;         /**< Overmodulation protection */
    int32_t cmixlev;            /**< Centre mix level */
    int32_t surmixlev;          /**< Surround mix level */
    int32_t dsurmod;            /**< Dolby Surround mode */
    int32_t audprodie;          /**< Audio production info exists */
    int32_t mixlevel;           /**< Mixing level */
    int32_t roomtyp;            /**< Room type */
    int32_t copyrightb;         /**< Copyright bit */
    int32_t origbs;             /**< Original bitstream */
    int32_t xbsi_dsurexmod;     /**< Dolby Surround EX Mode */
    int32_t xbsi_adconvtyp;     /**< A/D converter type */
    int32_t dialnorm;           /**< Dialogue normalization */
    int32_t acmod;              /**< Audio coding mode */
    int32_t lfeon;              /**< Low frequency effects channel on */
    int32_t lfelpfon;           /**< Low frequency effects lowpass filter on */
    int32_t hpfon;              /**< DC filter */
    int32_t suratton;           /**< Surround attenuation on */
    int32_t sur90on;            /**< Surround phase shift on */
    int32_t dynrng1;            /**< Dynamic range gain word */
    int32_t compr1;             /**< RF mode compression */
    int32_t xbsi_dmixmod;       /**< Preferred downmix mode */
    int32_t xbsi_ltrtcmixlev;   /**< Center downmix level in LtRt mode */
    int32_t xbsi_ltrtsurmixlev; /**< Surround downmix level in LtRt mode */
    int32_t xbsi_lorocmixlev;   /**< Center downmix level in LoRo mode */
    int32_t xbsi_lorosurmixlev; /**< Surround downmix level in LoRo mode */
    int32_t bwlpfon;            /**< Bandwidth Lowpass Filter */
}
dlb_padc_program_md_dolbye;


/**
 * @brief Structure stores all necessary program information
 */
typedef struct dlb_padc_ui_md_dolbye_s
{
    dlb_padc_bitstream_properties_dolbye    stream_properties;                                           /**< Bitstream properties */
    dlb_padc_SMPTE_timecode_dolbye          timecode;                                                    /**< Timecode structure */
    dlb_padc_program_md_dolbye              program_md[DLB_PADC_DOLBYE_MAX_PROGRAMS_NUM];                /**< Bitstream configuration metadata */
}
dlb_padc_ui_md_dolbye;

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* __DLB_PADC_UI_DOLBYE_MD */
