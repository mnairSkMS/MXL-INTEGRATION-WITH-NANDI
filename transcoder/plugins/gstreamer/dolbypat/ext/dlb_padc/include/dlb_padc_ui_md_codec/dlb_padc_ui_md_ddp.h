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
 * @file  dlb_padc_ui_md_ddp.h
 *
 * @brief File containing structures definitions for Dolby Digital Plus UI metadata
 */

#ifndef __DLB_PADC_UI_DDP_MD
#define __DLB_PADC_UI_DDP_MD

#ifdef __cplusplus
extern "C" {
#endif 

#include <stdint.h>


#define DLB_PADC_DDP_MAX_MAX_NUM_OBJECTS (15)


/**
 * @defgroup STD_PARAM_ENUM Parameter Value Enumerations
 * @{
 */

/**
 * @brief Enum defining trim values
 */
typedef enum
{
    DLB_PADC_TRIM_DB_POS_06P00 = 0,
    DLB_PADC_TRIM_DB_POS_03P00 = 1,
    DLB_PADC_TRIM_DB_POS_01P50 = 2,
    DLB_PADC_TRIM_DB_POS_00P75 = 3,
    DLB_PADC_TRIM_DB_NEG_00P75 = 4,
    DLB_PADC_TRIM_DB_NEG_01P50 = 5,
    DLB_PADC_TRIM_DB_NEG_03P00 = 6,
    DLB_PADC_TRIM_DB_NEG_04P50 = 7,
    DLB_PADC_TRIM_DB_NEG_06P00 = 8,
    DLB_PADC_TRIM_DB_NEG_07P50 = 9,
    DLB_PADC_TRIM_DB_NEG_09P00 = 10,
    DLB_PADC_TRIM_DB_NEG_10P50 = 11,
    DLB_PADC_TRIM_DB_NEG_12P00 = 12,
    DLB_PADC_TRIM_DB_NEG_13P50 = 13,
    DLB_PADC_TRIM_DB_NEG_15P00 = 14,
    DLB_PADC_TRIM_DB_NEG_36P00 = 15,
    DLB_PADC_TRIM_DB_00P00     = 16,
    DLB_PADC_TRIM_AUTO         = 17,
    DLB_PADC_TRIM_DOES_NOT_EXIST = -1
}
dlb_padc_trims;


/**
 * @brief Enum defining dialogue indication
 */
typedef enum
{
    DLB_PADC_DIALOG_INDICATION_UNKNOWN = 0,    /* No determination can be made on whether the objects contain dialog */
    DLB_PADC_DIALOG_INDICATION_IS_DIALOG = 1,  /* Object contains only dialog content */
    DLB_PADC_DIALOG_INDICATION_NOT_DIALOG = 2, /* Object does not contain any dialog content */
    DLB_PADC_DIALOG_INDICATION_MIXED = 3       /* Object contains both dialog and non-dialog content */
}
dlb_padc_object_dialogue_indication;

/** @} */


/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter Data Structures
 * @{
 */

/**
 * @brief Structure containing dynamic object position
 */
typedef struct dlb_padc_object_info_ddp_s
{
    float x; /**< Object x-position, range [-1.0, 1.0]. Negative is left of the listener. */
    float y; /**< Object y-position, range [-1.0, 1.0]. Negative is towards the screen. */
    float z; /**< Object z-position, range [-1.0, 1.0]. Negative is below the listener. */
    dlb_padc_object_dialogue_indication dialogue_indication; /**< Object dialogue indication */
}
dlb_padc_object_info_ddp;


/**
 * @brief Structure containing channels present in bed object
 */
typedef struct dlb_padc_bed_channels_ddp_s
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
dlb_padc_bed_channels_ddp;


/**
 * @brief Structure containing Dolby Atmos metadata
 */
typedef struct dlb_padc_atmos_metadata_ddp_s
{
    int32_t dynamic_objects_cnt;           /**< Number of dynamic objects */
    int32_t object_info_blks_cnt;          /**< Number of object info blocks */
    int32_t complexity_index;              /**< Number of all objects */
    int32_t bed_object_channel_distr;      /**< Bed object channel distribution */
    dlb_padc_trims surround_trim_5_1;      /**> Surround Trim for 5.1 configuration */
    dlb_padc_trims height_trim_5_1;        /**> Height Trim for 5.1 configuration*/
    dlb_padc_object_info_ddp objects_info[DLB_PADC_DDP_MAX_MAX_NUM_OBJECTS]; /**< Array of dynamic objects positions */
    dlb_padc_bed_channels_ddp bed_object_configuration;             /**< Bed object configuration */
}
dlb_padc_atmos_metadata_ddp;


/**
 * @brief Structure containing information about Dolby Digital Plus bitstream
 */
typedef struct dlb_padc_bitstream_properties_ddp_s
{
    int32_t sample_rate;                /**< Signal's sample rate. Value in Hz */
    int32_t data_rate;                  /**< Aggregated data rate for processed data */
    int32_t channel_mode;               /**< Aggregated channel mode */
    int32_t atmos_presence;             /**< Bitstream with Dolby Atmos content */
    uint8_t crc_error_present;          /**< CRC error presence in the bitstream */
}
dlb_padc_bitstream_properties_ddp;


/**
 * @brief Structure holds information about intelligent loudness
 * 
 * Represented values match ETSI specification
 */
typedef struct dlb_padc_intloud_ddp_s
{
    int32_t         loud_reg_type;          /**< Loudness regulation type */
    int32_t         loud_corr_type;         /**< Loudness correction type */
    int32_t         dialogue_corrected;     /**< Dialogoue corrected */
}
dlb_padc_intloud_ddp;


/**
 * @brief Structure contains bsi metadata 
 * 
 * Represented values match ETSI specification
 */
typedef struct dlb_padc_bsi_md_s
{
    int32_t bsid;               /**< Bitstream identification */
    int32_t bsmod;              /**< Bitstream mode */
    int32_t lfeon;              /**< Flag indicating that LFE channel is enabled */
    int32_t dsurmod;            /**< Dolby surround mode */
    int32_t dialnorm;           /**< Dialogue normalization. Valid values are from -31 to -1, values in dB */
    int32_t mixlevel;           /**< Mixing level. Value in range 80 to 111. */
    int32_t roomtyp;            /**< Room type */
    int32_t copyrightb;         /**< Copyright bit. If value is 1 bitstream is protected by copyright otherwise means bitstream is not protected */
    int32_t origbs;             /**< Original bitstream flag. This is value of 1 when it is an original bitstream, 0 otherwise. */
    int32_t dmixmod;            /**< Preffered downmix code */
    int32_t ltrtcmixlev;        /**< Level of LtRt center downmix */
    int32_t ltrtsurmixlev;      /**< Level of LtRt surround downmix */
    int32_t lorocmixlev;        /**< Level of LoRo center downmix */
    int32_t lorosurmixlev;      /**< Level of LoRo surround downmix */
    int32_t dsurexmod;          /**< Surround EX flag mode */
    int32_t adconvtyp;          /**< A/D Converter type */
}
dlb_padc_bsi_md_ddp;


typedef struct dlb_padc_programme_info_ddp_s
{
    int32_t pgm_info_exists; /**< Indicates whether programme info metadata exists */
    int32_t ph90filt;        /**< 90 degree phase shift applied flag */
    int32_t dynrngprof;      /**< DRC profile for dynrng data */
    int32_t comprprof;       /**< DRC profile for compr data */
}
dlb_padc_programme_info_ddp;


/**
 * @brief Structure gathers packed metadata used for UI
 * 
 * @see dlb_padc_bitstream_properties_ddp
 * @see dlb_padc_intloud_ddp
 * @see dlb_padc_bsi_md_ddp
 * @see dlb_padc_atmos_metadata_ddp
 */
typedef struct dlb_padc_ui_md_ddp_s
{
    dlb_padc_bitstream_properties_ddp       stream_properties;  /**< Stream properties */
    dlb_padc_intloud_ddp                    intloud;            /**< Intelligent loudness metadata in first independent substream */
    dlb_padc_bsi_md_ddp                     bsi_md;             /**< BSI metadata */
    dlb_padc_atmos_metadata_ddp             atmos_md;           /**< Atmos metadata, valid info only when atmos_presence is 1 */
    dlb_padc_programme_info_ddp             pgm_info_md;        /**< Programme info metadata */
}
dlb_padc_ui_md_ddp;

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* DLB_PADC_UI_DDP_MD */
