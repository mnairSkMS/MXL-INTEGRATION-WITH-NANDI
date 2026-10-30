/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                  Copyright (C) 2020-2024 by Dolby Laboratories.
 *                            All rights reserved.
 ******************************************************************************/

/**
 *  @file dlb_paec_params_runtime_ddp.h
 *
 *  @brief Dolby pro audio encoder component (PAEC)
 *         Dolby Digital, Dolby Digital Plus, and
 *         Dolby Digital Plus with Dolby Atmos content run-time parameters.
 *
 *  Dolby Digital, Dolby Digital Plus, and
 *  Dolby Digital Plus with Dolby Atmos content run-time parameters for PAEC.
 */

#ifndef DLB_PAEC_PARAMS_RUNTIME_DDP_H
#define DLB_PAEC_PARAMS_RUNTIME_DDP_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PAEC encodes PCM to Dolby Digital, Dolby Digital Plus, and
 * Dolby Digital Plus with Dolby Atmos content. It is used to
 * encode PCM input and to transcode from one encoded bitstream to Dolby
 * Digital, Dolby Digital Plus, or Dolby Digital Plus with Dolby Atmos content,
 * when PCM is an intermediate format in the signal chain.
 *
 * The dlb_paec_runtime_config_ddp structure specifies the values for each
 * run-time parameter. Some or all of these parameters can be displayed on a product UI
 * and changed asynchronously by the end user. In a PCM encoding use case, these
 * parameters specify the static values applied to the encoder. In a transcoding
 * use case, the encoder can follow (or conform to) the dynamic metadata
 * extracted from the decoded bitstream. To enable the end user to override this
 * dynamic external metadata, a number of run-time parameters provide an enumerated
 * value of DLB_PAEC_DDP_<PARAM>_AUTO that enables automatic mode to set this
 * parameter. When the application sets the parameter to its automatic mode (for
 * example, DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_AUTO) the encoder follows the dynamic
 * external metadata. When the application sets a parameter to a specific value
 * (for example, DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M4_5DB) the encoder overrides
 * the dynamic external metadata for the parameter with the specific value.
 *
 * All parameters that support automatic mode have it enabled by default.
 */


/**
 * @defgroup STD_PARAM_ENUM Parameter value enumerations
 * @{
 */


/**
 * @brief Channel configurations for Dolby Digital, Dolby Digital Plus, and
 * Dolby Digital Plus with Dolby Atmos content
 *
 * The expected ordering of input channels depends on the \<channel_config\>
 * run-time parameter. The following table illustrates the assignments for a
 * 16-channel PCM input:
 *
 * <pre>
 * Input channel            0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
 * 1.0 (mono)               L   x   x   x   x   x   x   x   x   x   x   x   x   x   x   x
 * 1.0 + 1.0 (dual mono)    L   R   x   x   x   x   x   x   x   x   x   x   x   x   x   x
 * 2.0 (stereo)             L   R   x   x   x   x   x   x   x   x   x   x   x   x   x   x
 * 5.1                      L   R   C  LFE Ls  Rs   x   x   x   x   x   x   x   x   x   x
 * 5.1.4                    L   R   C  LFE Ls  Rs  Tfl Tfr Tbl Tbr  x   x   x   x   x   x
 * 7.1.4                    L   R   C  LFE Ls  Rs  Lb  Rb  Tfl Tfr Tbl Tbr  x   x   x   x
 * 9.1.6                    L   R   C  LFE Ls  Rs  Lb  Rb  Tfl Tfr Tsl Tsr Tbl Tbr Lw  Rw
 *
 * x   = Unused channel
 * </pre>
 */
enum dlb_paec_ddp_channel_config
{
     DLB_PAEC_DDP_CHANNEL_CONFIG_10         = 0     /**< 1.0 (mono) */
    ,DLB_PAEC_DDP_CHANNEL_CONFIG_1_1        = 1     /**< 1.0 + 1.0 (dual mono) */
    ,DLB_PAEC_DDP_CHANNEL_CONFIG_20         = 2     /**< 2.0 (stereo) */
    ,DLB_PAEC_DDP_CHANNEL_CONFIG_51         = 3     /**< 5.1 */
    ,DLB_PAEC_DDP_CHANNEL_CONFIG_514        = 4     /**< 5.1.4 (only for Dolby Digital Plus with Dolby Atmos content) */
    ,DLB_PAEC_DDP_CHANNEL_CONFIG_714        = 5     /**< 7.1.4 (only for Dolby Digital Plus with Dolby Atmos content) */
    ,DLB_PAEC_DDP_CHANNEL_CONFIG_916        = 6     /**< 9.1.6 (only for Dolby Digital Plus with Dolby Atmos content) */

    ,DLB_PAEC_DDP_CHANNEL_CONFIG_DEFAULT    = DLB_PAEC_DDP_CHANNEL_CONFIG_514
};

/**
 * @brief Bitstream mode code
 */
enum dlb_paec_ddp_bsmod
{
     DLB_PAEC_DDP_BSMOD_COMPLETE_MAIN       = 0 /**< Complete Main (CM) */
    ,DLB_PAEC_DDP_BSMOD_MUSIC_AND_EFFECTS   = 1 /**< Music and Effects (ME) */
    ,DLB_PAEC_DDP_BSMOD_VISUALLY_IMPAIRED   = 2 /**< Visually Impaired (VI) */
    ,DLB_PAEC_DDP_BSMOD_HEARING_IMPAIRED    = 3 /**< Hearing Impaired (HI) */
    ,DLB_PAEC_DDP_BSMOD_DIALOGUE            = 4 /**< Dialogue (D) */
    ,DLB_PAEC_DDP_BSMOD_COMMENTARY          = 5 /**< Commentary (C) */
    ,DLB_PAEC_DDP_BSMOD_EMERGENCY           = 6 /**< Emergency (E) */
    ,DLB_PAEC_DDP_BSMOD_VOICE_OVER          = 7 /**< Voice Over (VO) */
    ,DLB_PAEC_DDP_BSMOD_DEFAULT             = DLB_PAEC_DDP_BSMOD_COMPLETE_MAIN
};
/**
 * @brief Bit rates for Dolby Digital, Dolby Digital Plus, and
 * Dolby Digital Plus with Dolby Atmos content
 *
 * Indicates bit rates at which the input signal can be encoded.
 *
 * The following bit rates are valid for each channel configuration:
 *
 * <pre>
*     32K                           1.0
 *    56K   DD 1.0
 *    64K   DD 1.0                  1.0
 *    96K   DD 1.0  DD 1+1  DD 2.0          1.0     1+1   2.0
 *   128K           DD 1+1  DD 2.0                  1+1   2.0
 *   192K   DD 1.0  DD 1+1  DD 2.0          1.0     1+1   2.0   5.1
 *   224K           DD 1+1  DD 2.0  DD 5.1          1+1   2.0   5.1
 *   256K           DD 1+1  DD 2.0  DD 5.1          1+1   2.0   5.1
 *   384K           DD 1+1  DD 2.0  DD 5.1          1+1   2.0   5.1   5.1.4   7.1.4
 *   448K           DD 1+1  DD 2.0  DD 5.1          1+1   2.0   5.1   5.1.4   7.1.4   9.1.6
 *   640K           DD 1+1  DD 2.0  DD 5.1          1+1   2.0   5.1   5.1.4   7.1.4   9.1.6
 *   768K                                           1+1   2.0   5.1   5.1.4   7.1.4   9.1.6
 * </pre>
 *
 */
enum dlb_paec_ddp_datarate
{
     DLB_PAEC_DDP_DATARATE_32K      = 0         /**< 32 kbps */
    ,DLB_PAEC_DDP_DATARATE_56K      = 1         /**< 56 kbps */
    ,DLB_PAEC_DDP_DATARATE_64K      = 2         /**< 64 kbps */
    ,DLB_PAEC_DDP_DATARATE_96K      = 3         /**< 96 kbps */
    ,DLB_PAEC_DDP_DATARATE_128K     = 4         /**< 128 kbps */
    ,DLB_PAEC_DDP_DATARATE_192K     = 5         /**< 192 kbps */
    ,DLB_PAEC_DDP_DATARATE_224K     = 6         /**< 224 kbps */
    ,DLB_PAEC_DDP_DATARATE_256K     = 7         /**< 256 kbps */
    ,DLB_PAEC_DDP_DATARATE_384K     = 8         /**< 384 kbps */
    ,DLB_PAEC_DDP_DATARATE_448K     = 9         /**< 448 kbps */
    ,DLB_PAEC_DDP_DATARATE_640K     = 10        /**< 640 kbps */
    ,DLB_PAEC_DDP_DATARATE_768K     = 11        /**< 768 kbps */

    ,DLB_PAEC_DDP_DATARATE_DEFAULT  = DLB_PAEC_DDP_DATARATE_448K
};


/**
 * @brief Preferred stereo downmix mode
 *
 * Indicates the type of stereo downmix preferred by the mastering engineer.
 *
 * NOTE: The meaning of this field is defined only as described if the channel
 *       configuration is set to 5.1. For 2.0, the meaning of this field is reserved.
 */
enum dlb_paec_ddp_dmixmod
{
    /** Automatic mode:
     *  -  when transcoding, follows external metadata, if available (dlb_paec_input.p_md)
     *  -  otherwise, DLB_PAEC_DDP_DMIXMOD_AUTO_DEFAULT is selected
     */
     DLB_PAEC_DDP_DMIXMOD_AUTO          = -1

    ,DLB_PAEC_DDP_DMIXMOD_NOT_INDICATED = 0     /**< Not indicated */
    ,DLB_PAEC_DDP_DMIXMOD_LTRT          = 1     /**< Lt/Rt (Dolby Pro Logic) downmix is preferred */
    ,DLB_PAEC_DDP_DMIXMOD_LORO          = 2     /**< Lo/Ro (simple stereo representation) downmix is preferred */
    ,DLB_PAEC_DDP_DMIXMOD_PROLOGIC2     = 3     /**< Dolby Pro Logic II downmix is preferred */

    ,DLB_PAEC_DDP_DMIXMOD_DEFAULT       = DLB_PAEC_DDP_DMIXMOD_AUTO
    ,DLB_PAEC_DDP_DMIXMOD_AUTO_DEFAULT  = DLB_PAEC_DDP_DMIXMOD_NOT_INDICATED
};


/**
 * @brief Lt/Rt and Lo/Ro center and surround downmix levels
 *
 *  Indicates gain applied to either the center channel or
 *  surround channels as a result of downmixing to an Lt/Rt or
 *  Lo/Ro output.
 */
enum dlb_paec_ddp_lxrx_downmix_level
{
    /** Automatic mode:
     *  -  when transcoding, follows external metadata, if available (dlb_paec_input.p_md)
     *  -  otherwise, DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_AUTO_DEFAULT is selected
     */
     DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_AUTO   = -1

    ,DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_3_0DB  = 0     /**< +3.0 dB; center only */
    ,DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_1_5DB  = 1     /**< +1.5 dB; center only */
    ,DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_0DB    = 2     /**<    0 dB; center only */
    ,DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M1_5DB = 3     /**< -1.5 dB */
    ,DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M3_0DB = 4     /**< -3.0 dB */
    ,DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M4_5DB = 5     /**< -4.5 dB */
    ,DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M6_0DB = 6     /**< -6.0 dB */
    ,DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M_INF  = 7     /**< -infinity dB */

    ,DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_DEFAULT        = DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_AUTO
    ,DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_AUTO_DEFAULT   = DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M3_0DB
};


/**
 * @brief Surround phase shift
 *
 * This parameter causes the Dolby Digital Plus encoder to apply a 90-degree phase shift
 * to the surround channels.
 *
 * This allows a Dolby Digital Plus decoder to create an Lt/Rt downmix. For most audio
 * content, phase shifting has minimal effect when a Dolby Digital Plus program is
 * decoded to 5.1 channels, but it provides an Lt/Rt output that can be decoded with
 * Dolby Pro Logic to L, C, R, Ls, and Rs, if desired.
 *
 * However, for some phase-critical material (such as music) this phase shift is
 * audible when listening in a 5.1-channel format.
 *
 * Likewise, some material downmixes to a satisfactory Lt/Rt signal without a need
 * to shift phase. It is therefore important to balance the needs of the 5.1 mix
 * and the Lt/Rt downmix for each program.
 */
enum dlb_paec_ddp_sur90on
{
    /** Automatic mode:
     *  -  when transcoding, follows external metadata, if available (dlb_paec_input.p_md)
     *  -  otherwise, DLB_PAEC_DDP_SUR90ON_AUTO_DEFAULT is selected
     */
     DLB_PAEC_DDP_SUR90ON_AUTO          = -1

    ,DLB_PAEC_DDP_SUR90ON_DISABLE       = 0     /**< Surround phase shift disabled */
    ,DLB_PAEC_DDP_SUR90ON_ENABLE        = 1     /**< Surround phase shift enabled */

    ,DLB_PAEC_DDP_SUR90ON_DEFAULT       = DLB_PAEC_DDP_SUR90ON_AUTO
    ,DLB_PAEC_DDP_SUR90ON_AUTO_DEFAULT  = DLB_PAEC_DDP_SUR90ON_ENABLE
};


/**
 * @brief Surround and height trims
 *
 * Surround and height trim value (in decibels) to be applied at the metadata level
 */
enum dlb_paec_ddp_trim
{
    /** Automatic mode:
     *  -  when transcoding, follows external metadata, if available (dlb_paec_input.p_md)
     *  -  otherwise, DLB_PAEC_DDP_SURROUND_TRIM_AUTO_DEFAULT and
     *     DLB_PAEC_DDP_HEIGHT_TRIM_AUTO_DEFAULT is selected
     */
     DLB_PAEC_DDP_TRIM_AUTO             = -1

    ,DLB_PAEC_DDP_TRIM_0DB              = 0     /**<  0 dB; surround trim only */
    ,DLB_PAEC_DDP_TRIM_M3DB             = 1     /**< -3 dB */
    ,DLB_PAEC_DDP_TRIM_M6DB             = 2     /**< -6 dB */
    ,DLB_PAEC_DDP_TRIM_M9DB             = 3     /**< -9 dB */
    ,DLB_PAEC_DDP_TRIM_M12DB            = 4     /**< -12 dB; height trim only */

    ,DLB_PAEC_DDP_TRIM_DEFAULT                   = DLB_PAEC_DDP_TRIM_AUTO
    ,DLB_PAEC_DDP_SURROUND_TRIM_AUTO_DEFAULT     = DLB_PAEC_DDP_TRIM_0DB
    ,DLB_PAEC_DDP_HEIGHT_TRIM_AUTO_DEFAULT       = DLB_PAEC_DDP_TRIM_M6DB
};


/**
 * @brief Personalized audio presentation range
 */
enum dlb_paec_ddp_pa_presentation_range
{
     DLB_PAEC_DDP_PA_PRES_MIN           = 0     /**< Minimum personalized audio presentation */
    ,DLB_PAEC_DDP_PA_PRES_DEFAULT       = 0     /**< Default personalized audio presentation */
    ,DLB_PAEC_DDP_PA_PRES_MAX           = 15    /**< Maximum personalized audio presentation */
};

/**
 * @brief Follow-the-input behavior
 *
 * This parameter determines the encoder output channel configuration.
 *
 * If dlb_paec_ddp_follow_the_input is set to DLB_PAEC_DDP_FOLLOW_THE_INPUT_ON,
 * the encoder output channel configuration
 * is driven by incoming metadata. The channel_config parameter sets the maximum allowed
 * channel configuration in this case.
 * If the incoming channel configuration metadata is greater than channel_config,
 * the channel_config value is used and the warning
 * DLB_DDP_WARNING_INCOMING_MD_CHANNEL_CONFIG_IGNORED is raised.
 *
 * If dlb_paec_ddp_follow_the_input is set to DLB_PAEC_DDP_FOLLOW_THE_INPUT_OFF,
 * the channel_config parameter determines the encoder
 * output channel configuration regardless of the channel configuration signalled over input metadata.
 */
enum dlb_paec_ddp_follow_the_input
{
     DLB_PAEC_DDP_FOLLOW_THE_INPUT_ON   = 0     /**< The channel configuration is determined
                                                     based on input metadata.
                                                     If input metadata is not present,
                                                     then the channel_config parameter is used instead */
    ,DLB_PAEC_DDP_FOLLOW_THE_INPUT_OFF  = 1     /**< The channel configuration is set to the
                                                     channel_config parameter value */
    ,DLB_PAEC_DDP_FOLLOW_THE_INPUT_DEFAULT = DLB_PAEC_DDP_FOLLOW_THE_INPUT_ON
};


/** @} */


/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter data structures (high level)
 * @{
 */


/**
 * @brief Encoding configuration for Dolby Digital, Dolby Digital Plus, and
 * Dolby Digital Plus with Dolby Atmos content.
 */
typedef struct
{
    unsigned int    channel_config; /**< PCM channel configuration.
                                     *   -  @ref dlb_paec_ddp_channel_config
                                     *   -  default: DLB_PAEC_DDP_CHANNEL_CONFIG_DEFAULT (5.1.4)
                                     */
    unsigned int    datarate;       /**< Bit rate (varies by channel configuration).
                                     *   -  @ref dlb_paec_ddp_datarate
                                     *   -  default: DLB_PAEC_DDP_DATARATE_DEFAULT (448 kbps)
                                     */
    int             dialnorm;       /**< Dialogue normalization value (in dB).
                                     *   -  @ref dlb_paec_dialnorm_range
                                     *   -  DLB_PAEC_DIALNORM_MIN (-1 dB) to DLB_PAEC_DIALNORM_MAX (-31 dB)
                                     *   -  default: DLB_PAEC_DIALNORM_DEFAULT
                                     *      - follows external metadata
                                     *      - otherwise: -24 dB
                                     */
    unsigned int    bsmod;         /**< Bitstream mode
                                     *  indicates the type of program service that is being carried
                                     *   -  @ref dlb_paec_ddp_bsmod
                                     *   -  default: DLB_PAEC_DDP_BSMOD_DEFAULT
                                     *   -  follows external metadata
                                     */
    int             drc_line;       /**< Dynamic range control Line mode.
                                     *   -  @ref dlb_paec_drc_profile
                                     *   -  default: DLB_PAEC_DRC_PROFILE_DEFAULT (auto mode)
                                     *      - follows external metadata
                                     *      - otherwise: Film Standard
                                     */
    int             drc_rf;         /**< Dynamic range control RF mode.
                                     *   -  @ref dlb_paec_drc_profile
                                     *   -  default: DLB_PAEC_DRC_PROFILE_DEFAULT (auto mode)
                                     *      - follows external metadata
                                     *      - otherwise: Film Standard
                                     */
    int             dmixmod;        /**< Preferred stereo downmix mode.
                                     *   -  @ref dlb_paec_ddp_dmixmod
                                     *   -  default: DLB_PAEC_DDP_DMIXMOD_DEFAULT (auto mode)
                                     *      - follows external metadata
                                     *      - otherwise: Not Indicated
                                     */
    int             ltrtcmixlev;    /**< Lt/Rt center downmix Level.
                                     *   -  @ref dlb_paec_ddp_lxrx_downmix_level
                                     *   -  DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_AUTO
                                     *   -  DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_3_0DB (+3.0 dB)
                                     *      to DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M_INF (-infinity)
                                     *   -  default: DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_DEFAULT (auto mode)
                                     *      - follows external metadata
                                     *      - otherwise: -3.0 dB
                                     */
    int             ltrtsurmixlev;  /**< Lt/Rt surround downmix Level.
                                     *   -  @ref dlb_paec_ddp_lxrx_downmix_level
                                     *   -  DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_AUTO
                                     *   -  DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M1_5DB (-1.5 dB)
                                     *      to DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M_INF (-infinity)
                                     *   -  default: DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_DEFAULT (auto mode)
                                     *      - follows external metadata
                                     *      - otherwise: -3.0 dB
                                     */
    int             lorocmixlev;    /**< Lo/Ro center downmix Level.
                                     *   -  @ref dlb_paec_ddp_lxrx_downmix_level
                                     *   -  DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_3_0DB (+3.0 dB)
                                     *      to DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M_INF (-infinity)
                                     *   -  default: DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_DEFAULT (auto mode)
                                     *      - follows external metadata
                                     *      - otherwise: -3.0 dB
                                     */
    int             lorosurmixlev;  /**< Lo/Ro surround downmix Level.
                                     *   -  @ref dlb_paec_ddp_lxrx_downmix_level
                                     *   -  DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_AUTO
                                     *   -  DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M1_5DB (-1.5 dB)
                                     *      to DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M_INF (-infinity)
                                     *   -  default: DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_DEFAULT (auto mode)
                                     *      - follows external metadata
                                     *      - otherwise: -3.0 dB
                                     */
    int             sur90on;        /**< Surround phase shift.
                                     *   -  @ref dlb_paec_ddp_sur90on
                                     *   -  DLB_PAEC_DDP_SUR90ON_AUTO
                                     *   -  DLB_PAEC_DDP_SUR90ON_DISABLE
                                     *   -  DLB_PAEC_DDP_SUR90ON_ENABLE
                                     *   -  default: DLB_PAEC_DDP_SUR90ON_DEFAULT (auto mode)
                                     *      - follows external metadata
                                     *      - otherwise: enabled
                                     */

    /* The trims are only used for Dolby Atmos channel configurations (5.1.4, 7.1.4, 9.1.6). */
    int             surround_trim;    /**< Surround trim.
                                      *   -  @ref dlb_paec_ddp_trim
                                      *   -  DLB_PAEC_DDP_TRIM_AUTO
                                      *   -  DLB_PAEC_DDP_TRIM_0DB (0 dB) to DLB_PAEC_DDP_TRIM_M9DB (-9 dB)
                                      *   -  default: DLB_PAEC_DDP_TRIM_DEFAULT (auto mode)
                                      *      - follows external metadata
                                      *      - otherwise: 0 dB
                                      */
    int             height_trim;      /**< Height trim.
                                      *   -  @ref dlb_paec_ddp_trim
                                      *   -  DLB_PAEC_DDP_TRIM_AUTO
                                      *   -  DLB_PAEC_DDP_TRIM_M3DB (-3 dB) to DLB_PAEC_DDP_TRIM_M12DB (-12 dB)
                                      *   -  default: DLB_PAEC_DDP_TRIM_DEFAULT (auto mode)
                                      *      - follows external metadata
                                      *      - otherwise: -6 dB
                                      */
    unsigned int    pa_pres;          /**< When personalized audio (PA) metadata is provided, this parameter selects
                                      *   which presentation is present in the output bitstream.
                                      *   -  @ref dlb_paec_ddp_pa_presentation_range
                                      *   -  DLB_PAEC_DDP_PA_PRES_MIN (0) to DLB_PAEC_DDP_PA_PRES_MAX (15)
                                      *   -  default: DLB_PAEC_DDP_PA_PRES_DEFAULT (0)
                                      */
    unsigned int    follow_the_input; /**< When follow-the-input behavior is turned on, the channel configuration is determined
                                      *   based on incoming metadata. Otherwise, channel_config is used.
                                      *   -  @ref dlb_paec_ddp_follow_the_input
                                      *   -  DLB_PAEC_DDP_FOLLOW_THE_INPUT_ON
                                      *   -  DLB_PAEC_DDP_FOLLOW_THE_INPUT_OFF
                                      *   -  default: DLB_PAEC_DDP_FOLLOW_THE_INPUT_DEFAULT(on)
                                      */
} dlb_paec_runtime_config_ddp;

/** @} */


/*********************************************/
/*** Error and Warning Code Values ***********/
/*********************************************/

/**
 * @defgroup ERROR_CODES Error and warning code values
 * @{
 */

/**
 * @brief Encoding error codes for Dolby Digital, Dolby Digital Plus,
 * and Dolby Digital Plus with Dolby Atmos content
 */
enum dlb_paec_status_ddp
{
     DLB_PAEC_ERROR_DDP_CHANNEL_CONFIG   = 2000  /**< The \<channel_config\> run-time parameter is out-of-range. */
    ,DLB_PAEC_ERROR_DDP_DATARATE         = 2001  /**< The \<datarate\> run-time parameter is out-of-range. */
    ,DLB_PAEC_ERROR_DDP_DIALNORM         = 2002  /**< The \<dialnorm\> run-time parameter is out-of-range. */
    ,DLB_PAEC_ERROR_DDP_DRC_LINE         = 2003  /**< The \<drc_line\> run-time parameter is out-of-range. */
    ,DLB_PAEC_ERROR_DDP_DRC_RF           = 2004  /**< The \<drc_rf\> run-time parameter is out-of-range. */
    ,DLB_PAEC_ERROR_DDP_DMIXMOD          = 2005  /**< The \<dmixmod\> run-time parameter is out-of-range. */
    ,DLB_PAEC_ERROR_DDP_LTRTCMIXLEV      = 2006  /**< The \<ltrtcmixlev\> run-time parameter is out-of-range. */
    ,DLB_PAEC_ERROR_DDP_LTRTSURMIXLEV    = 2007  /**< The \<ltrtsurmixlev\> run-time parameter is out-of-range. */
    ,DLB_PAEC_ERROR_DDP_LOROCMIXLEV      = 2008  /**< The \<lorocmixlev\> run-time parameter is out-of-range. */
    ,DLB_PAEC_ERROR_DDP_LOROSURMIXLEV    = 2009  /**< The \<lorosurmixlev\> run-time parameter is out-of-range. */
    ,DLB_PAEC_ERROR_DDP_SURROUND_TRIM    = 2010  /**< The \<surround_trim\> run-time parameter is out-of-range. */
    ,DLB_PAEC_ERROR_DDP_HEIGHT_TRIM      = 2011  /**< The \<height_trim\> run-time parameter is out-of-range. */
    ,DLB_PAEC_ERROR_DDP_PA_PRES          = 2012  /**< The \<pa_pres\> run-time parameter is out-of-range. */
    ,DLB_PAEC_ERROR_DDP_FOLLOW_THE_INPUT = 2013  /**< The \<follow_the_input\> run-time parameter is out-of-range. */
};

typedef enum
{
    DLB_DDP_WARNING_UNSUPPORTED_TRANSCODING_CHANNEL_MODE = 1000, /**< DEPRECATED Unsupported channel mode (such as 7.1, 5.1.2) received from transcoded content, 5.1 encoding applied */
    DLB_DDP_WARNING_PALOUD_AUTHOR_MODE_INVALID,                  /**< Incorrect authoring mode detected */
    DLB_DDP_WARNING_UNEXPECTED_MIF_VERSION,                      /**< DEPRECATED Incoming metadata interchange format segment created with unexpected version */
    DLB_DDP_WARNING_INCOMING_MD_CHANNEL_CONFIG_IGNORED,          /**< Channel configuration received from incoming metadata is greater than maximum channel configuration.
                                                                 *    Channel configuration received from incoming metadata is ignored.
                                                                 */
    DLB_DDP_WARNING_INCOMING_MD_CHANNEL_CONFIG_UNSUPPORTED       /**< Unsupported channel mode (such as 7.1, 5.1.2) received from incoming metadata, 5.1 encoding applied */

} dlb_paec_warning_codes_ddp;

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PAEC_PARAMS_RUNTIME_DDP_H */
