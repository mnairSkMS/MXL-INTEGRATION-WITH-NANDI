/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                  Copyright (C) 2024 by Dolby Laboratories.
 *                            All rights reserved.
 ******************************************************************************/
#ifndef DLB_PAMC_EXTERNAL_MD_H
#define DLB_PAMC_EXTERNAL_MD_H

#ifdef __cplusplus
extern "C" {
#endif

/**    
* @defgroup STD_PARAM_ENUM Parameter Value Enumerations    
* @{    
*/      

/**
 * @brief Defines channel configuration
 * 
 * The ordering of the channels for 16-channel PCM input:
 *
 * <pre>
 * Input channel            0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
 * 1.0 (mono)               C   x   x   x   x   x   x   x   x   x   x   x   x   x   x   x
 * 2.0 (stereo)             L   R   x   x   x   x   x   x   x   x   x   x   x   x   x   x
 * 3/1                      L   R   C   S   x   x   x   x   x   x   x   x   x   x   x   x 
 * 5.1                      L   R   C  LFE Ls  Rs   x   x   x   x   x   x   x   x   x   x
 * 7.1                      L   R   C  LFE Ls  Rs  Lb  Rb   x   x   x   x   x   x   x   x
 * 5.1.4                    L   R   C  LFE Ls  Rs  Tfl Tfr Tbl Tbr  x   x   x   x   x   x
 * </pre>
 */
typedef enum dlb_pamc_ext_md_channel_config_t
{
    DLB_PAMC_EXT_MD_CHANNEL_CFG_1_0  = 0,         /**< 1.0 (mono) */
    DLB_PAMC_EXT_MD_CHANNEL_CFG_2_0  = 1,         /**< 2.0 (stereo) */
    DLB_PAMC_EXT_MD_CHANNEL_CFG_3_1  = 2,         /**< 3/1 */
    DLB_PAMC_EXT_MD_CHANNEL_CFG_5_1  = 3,         /**< 5.1 */
    DLB_PAMC_EXT_MD_CHANNEL_CFG_7_1  = 4,         /**< 7.1 */
    DLB_PAMC_EXT_MD_CHANNEL_CFG_5_1_4  = 5,       /**< 5.1.4 */
                  
    DLB_PAMC_EXT_MD_CHANNEL_CFG_DEFAULT = DLB_PAMC_EXT_MD_CHANNEL_CFG_5_1
}
dlb_pamc_ext_md_channel_config;

/**
 * @brief Defines content classifier
 * 
 */
typedef enum dlb_pamc_ext_md_content_classifier_t
{
    DLB_PAMC_EXT_MD_CONTENT_CLASS_NOT_PRESENT        = -1,
    DLB_PAMC_EXT_MD_CONTENT_CLASS_COMPLETE_MAIN      = 0,
    DLB_PAMC_EXT_MD_CONTENT_CLASS_MUSIC_AND_EFFECT   = 1,
    DLB_PAMC_EXT_MD_CONTENT_CLASS_VISUALLY_IMPAIRED  = 2,
    DLB_PAMC_EXT_MD_CONTENT_CLASS_HEARING_IMPAIRED   = 3,
    DLB_PAMC_EXT_MD_CONTENT_CLASS_DIALOGUE           = 4,
    DLB_PAMC_EXT_MD_CONTENT_CLASS_COMMENTARY         = 5,
    DLB_PAMC_EXT_MD_CONTENT_CLASS_EMERGENCY          = 6,
    DLB_PAMC_EXT_MD_CONTENT_CLASS_VOICE_OVER         = 7,  
                  
    DLB_PAMC_EXT_MD_CONTENT_CLASS_DEFAULT = DLB_PAMC_EXT_MD_CONTENT_CLASS_NOT_PRESENT
}
dlb_pamc_ext_md_content_classifier;

/**
 * @brief Defines preferred stereo downmix mode.
 * 
 */
typedef enum dlb_pamc_ext_md_downmix_mode_t
{
    DLB_PAMC_EXT_MD_DOWNMIX_MODE_NOT_PRESENT       = -1,
    DLB_PAMC_EXT_MD_DOWNMIX_MODE_NOT_INDICATED     = 0,    
    DLB_PAMC_EXT_MD_DOWNMIX_MODE_LT_RT_PREFERRED   = 1,
    DLB_PAMC_EXT_MD_DOWNMIX_MODE_LO_RO_PREFERRED   = 2,
    DLB_PAMC_EXT_MD_DOWNMIX_MODE_PROLOGIC2         = 3,

    DLB_PAMC_EXT_MD_DOWNMIX_MODE_DEFAULT = DLB_PAMC_EXT_MD_DOWNMIX_MODE_NOT_PRESENT
}
dlb_pamc_ext_md_downmix_mode;

/**
 * @brief Defines Lt/Rt and Lo/Ro center and surround downmix levels
 *
 *  Indicates gain applied to either the center channel or
 *  surround channels as a result of downmixing to an Lt/Rt or
 *  Lo/Ro output.
 */
typedef enum dlb_pamc_ext_md_lxrx_downmix_level_t
{
     DLB_PAMC_EXT_MD_LXRX_DOWNMIX_LEVEL_NOT_PRESENT   = -1
    ,DLB_PAMC_EXT_MD_LXRX_DOWNMIX_LEVEL_3_0DB  = 0     /**< +3.0 dB; center only */
    ,DLB_PAMC_EXT_MD_LXRX_DOWNMIX_LEVEL_1_5DB  = 1     /**< +1.5 dB; center only */
    ,DLB_PAMC_EXT_MD_LXRX_DOWNMIX_LEVEL_0DB    = 2     /**<    0 dB; center only */
    ,DLB_PAMC_EXT_MD_LXRX_DOWNMIX_LEVEL_M1_5DB = 3     /**< -1.5 dB */
    ,DLB_PAMC_EXT_MD_LXRX_DOWNMIX_LEVEL_M3_0DB = 4     /**< -3.0 dB */
    ,DLB_PAMC_EXT_MD_LXRX_DOWNMIX_LEVEL_M4_5DB = 5     /**< -4.5 dB */
    ,DLB_PAMC_EXT_MD_LXRX_DOWNMIX_LEVEL_M6_0DB = 6     /**< -6.0 dB */
    ,DLB_PAMC_EXT_MD_LXRX_DOWNMIX_LEVEL_M_INF  = 7     /**< -infinity dB */

    ,DLB_PAMC_EXT_MD_LXRX_DOWNMIX_LEVEL_DEFAULT = DLB_PAMC_EXT_MD_LXRX_DOWNMIX_LEVEL_NOT_PRESENT
}
dlb_pamc_ext_md_lxrx_downmix_level;

/**
 * @brief Defines surround phase shift
 *
 * This parameter causes the Dolby Digital Plus encoder to apply a 90-degree phase shift
 * to the surround channels.
 */
typedef enum dlb_pamc_ext_md_sur90on_t
{
     DLB_PAMC_EXT_MD_SUR90ON_NOT_PRESENT   = -1
    ,DLB_PAMC_EXT_MD_SUR90ON_DISABLE       = 0     /**< Surround phase shift disabled */
    ,DLB_PAMC_EXT_MD_SUR90ON_ENABLE        = 1     /**< Surround phase shift enabled */

    ,DLB_PAMC_EXT_MD_SUR90ON_DEFAULT = DLB_PAMC_EXT_MD_SUR90ON_NOT_PRESENT
}
dlb_pamc_ext_md_sur90on;

/**
 * @brief Defines dynamic range control profiles
 */
typedef enum dlb_pamc_ext_md_drc_profile_t
{
     DLB_PAMC_EXT_MD_DRC_PROFILE_NOT_PRESENT       = -1
    ,DLB_PAMC_EXT_MD_DRC_PROFILE_NONE              = 0     /**< No specified profile */
    ,DLB_PAMC_EXT_MD_DRC_PROFILE_FILM_STANDARD     = 1     /**< Film Standard */
    ,DLB_PAMC_EXT_MD_DRC_PROFILE_FILM_LIGHT        = 2     /**< Film Light */
    ,DLB_PAMC_EXT_MD_DRC_PROFILE_MUSIC_STANDARD    = 3     /**< Music Standard */
    ,DLB_PAMC_EXT_MD_DRC_PROFILE_MUSIC_LIGHT       = 4     /**< Music Light */
    ,DLB_PAMC_EXT_MD_DRC_PROFILE_SPEECH            = 5     /**< Speech */

    ,DLB_PAMC_EXT_MD_DRC_PROFILE_DEFAULT = DLB_PAMC_EXT_MD_DRC_PROFILE_NOT_PRESENT
}
dlb_pamc_ext_md_drc_profile;

/** @} */


/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter Data Structures
 * @{
 */

/**
 * @brief Structure stores information of external metadata parameters
 */
typedef struct dlb_pamc_external_md_s
{
    unsigned int channel_config;    /**< Channel configuration.
                                     *   -  @ref dlb_pamc_ext_md_channel_config
                                     *   -  default: DLB_PAMC_EXT_MD_CHANNEL_DEFAULT
                                     */
    int content_classifier;         /**< Content classifier.
                                     *   -  @ref dlb_pamc_ext_md_content_classifier
                                     *   -  default: DLB_PAMC_EXT_MD_CONTENT_CLASS_DEFAULT
                                     */
    int dmixmod;                    /**< Preferred stereo downmix mode.
                                     *   -  @ref dlb_pamc_ext_md_downmix_mode
                                     *   -  default: DLB_PAMC_EXT_MD_DOWNMIX_MODE_DEFAULT
                                     */
    int ltrtcmixlev;                /**< Lt/Rt center downmix Level.
                                     *   -  @ref dlb_pamc_ext_md_lxrx_downmix_level
                                     *   -  default: DLB_PAMC_EXT_MD_LXRX_DOWNMIX_LEVEL_DEFAULT
                                     */
    int ltrtsurmixlev;              /**< Lt/Rt surround downmix Level.
                                     *   -  @ref dlb_pamc_ext_md_lxrx_downmix_level
                                     *   -  default: DLB_PAMC_EXT_MD_LXRX_DOWNMIX_LEVEL_DEFAULT
                                     */
    int lorocmixlev;                /**< Lo/Ro center downmix Level.
                                     *   -  @ref dlb_pamc_ext_md_lxrx_downmix_level
                                     *   -  default: DLB_PAMC_EXT_MD_LXRX_DOWNMIX_LEVEL_DEFAULT
                                     */
    int lorosurmixlev;              /**< Lo/Ro surround downmix Level.
                                     *   -  @ref dlb_pamc_ext_md_lxrx_downmix_level
                                     *   -  default: DLB_PAMC_EXT_MD_LXRX_DOWNMIX_LEVEL_DEFAULT
                                     */
    int sur90on;                    /**< Surround phase shift.
                                     *   -  @ref dlb_pamc_ext_md_sur90on
                                     *   -  default: DLB_PAMC_EXT_MD_SUR90ON_DEFAULT
                                     */
    int drc_ht_line;                /**< Dynamic range control Home Theater/Line mode.
                                     *   -  @ref dlb_pamc_ext_md_drc_profile
                                     *   -  default: DLB_PAMC_EXT_MD_DRC_PROFILE_DEFAULT
                                     */                                    
    int drc_fp_rf;                  /**< Dynamic range control Flat Panel/RF mode.
                                     *   -  @ref dlb_pamc_ext_md_drc_profile
                                     *   -  default: DLB_PAMC_EXT_MD_DRC_PROFILE_DEFAULT
                                     */
    int internal_data;              /**< Internal library use only, do not edit */                                  
}
dlb_pamc_external_md;

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PAMC_EXTERNAL_MD_H */
