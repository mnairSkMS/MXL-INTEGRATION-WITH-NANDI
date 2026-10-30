/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                Copyright (C) 2020-2022 by Dolby Laboratories,
 *                Copyright (C) 2020-2022 by Dolby International AB.
 *                            All rights reserved.
 ******************************************************************************/

/**
 * @file  dlb_padc_params_init.h
 *
 * @brief File containing initialization-time strucures and enum definitions
 */

#ifndef DLB_PADC_PARAMS_INIT_H
#define DLB_PADC_PARAMS_INIT_H

#include <stddef.h>
#include "dlb_padc_params_common.h"
#include "dlb_params_init_codec/dlb_padc_params_init_dolbye.h"
#include "dlb_params_init_codec/dlb_padc_params_init_ddp.h"
#include "dlb_padc_ui_md.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup STD_PARAM_ENUM Parameter Value Enumerations
 * @{
 */

/**
 * @brief Enumeration used for choosing component type
 */
typedef enum dlb_padc_type_t
{
    DLB_PADC_TYPE_DOLBYE    = 0,    /**< Dolby E decoder */
    DLB_PADC_TYPE_DDP       = 1,    /**< Dolby Digital, Dolby Digital Plus, Dolby Digital Plus with Dolby Atmos content decoder */
    DLB_PADC_TYPE_ED2       = 2,    /**< Dolby ED2 decoder */
}
dlb_padc_type;

/** @} */


/**
 * @defgroup DATA_STRUCT_HIGH_LEVEL Parameter Data Structures
 * @{
 */

/**
 * @brief Library version structure
 */
typedef struct dlb_padc_version_s
{
    unsigned int v_major;    /**< Major release */
    unsigned int v_minor;    /**< Minor release */
    unsigned int v_func;     /**< Functional release */
    unsigned int v_build;    /**< Build number */
}
dlb_padc_version;


/**
 * @brief Structure used for library initialization
 */
typedef struct dlb_padc_init_config_s
{
    dlb_padc_type type;                     /**< Component type indicator */

    union
    {
        dlb_padc_initcfg_dolbye dolbye;  /**< Dolby E component type config */
        dlb_padc_initcfg_ddp ddp;        /**< Dolby Digital Plus component type config */
    }
    config;
    
    p_md_updated_callback p_callback;       /**< Pointer to metadata updated callback procedure */
    void *p_user_data;                      /**< Pointer to user specific data structure */
}
dlb_padc_init_config;


/**
 * @brief Union containing information about components latency per codec.
 */
typedef union dlb_padc_latency_s
{
    struct {
        unsigned int fr2398;    /**< Component latency for Dolby E @ 23.98 fps */
        unsigned int fr24;      /**< Component latency for Dolby E @ 24.00 fps */
        unsigned int fr25;      /**< Component latency for Dolby E @ 25.00 fps */
        unsigned int fr2997;    /**< Component latency for Dolby E @ 29.97 fps */
        unsigned int fr30;      /**< Component latency for Dolby E @ 30.00 fps */
    } dolbye;
    unsigned int ddp;           /**< Component latency for Dolby Digital Plus bitstream */
    struct {
        unsigned int fr2398;    /**< Component latency for Dolby ED2 @ 23.98 fps */
        unsigned int fr24;      /**< Component latency for Dolby ED2 @ 24.00 fps */
        unsigned int fr25;      /**< Component latency for Dolby ED2 @ 25.00 fps */
        unsigned int fr2997;    /**< Component latency for Dolby ED2 @ 29.97 fps */
        unsigned int fr30;      /**< Component latency for Dolby ED2 @ 30.00 fps */
    } ed2;
}
dlb_padc_latency;


/**
 * @brief Memory requirements to be used in library
 */
typedef struct dlb_padc_requirements_s
{
    unsigned int     max_out_samples;           /**< Max number of samples to get from component */
    unsigned int     max_out_channels;          /**< Max number of output channels */
    dlb_padc_latency latency;                   /**< Latency per given frame rate */
    size_t           max_input_size;            /**< Max number of bytes to be pushed into component */
}
dlb_padc_requirements;

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* DLB_PADC_PARAMS_INIT_H */

