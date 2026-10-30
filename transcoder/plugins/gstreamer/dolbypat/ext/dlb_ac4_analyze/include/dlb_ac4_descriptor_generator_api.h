/**
 * @brief  Dolby ac4_descriptor_generator
 * @note   This program is protected under international and U.S. copyright laws
 *         as an unpublished work. This program is confidential and proprietary to
 *         Dolby Laboratories. Reproduction or disclosure, in whole or in part, or
 *         the production of derivative works without the express
 *         permission of Dolby Laboratories is prohibited.
 * 
 *         Copyright (C) 2017 Dolby Laboratories Inc.
 *         Copyright (C) 2018-2023 by Dolby International AB.
 *                      All rights reserved.
 * 
 */


/**
 *  @file
 *
 *  @brief AC-4 Descriptor Generator for DVB
 */

/**
 * @ingroup dvb_desc_gen_api
 * @{
 */

#ifndef __AC4_DESCRIPTOR_GENERATOR_API_H__
#define __AC4_DESCRIPTOR_GENERATOR_API_H__

#include <stddef.h>

#include "dlb_ac4analyze.h"
#include "ac4dvb_desc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Error codes */
typedef enum
{
	AC4_DESC_ERR_NONE                     = 0x00,
	AC4_DESC_ERR_NULL_PTR                 = 0x01,
	AC4_DESC_ERR_INVALID_HANDLE           = 0x02,
    AC4_DESC_ERR_DVB_MODULE               = 0X03,   /* Fatal error in the DVB sub-module */
    AC4_DESC_ERR_INVALID_FRAMEDATA        = 0x04,   /* An error caused by invalid ac4analyze_frame_data_t input */
    AC4_DESC_ERR_GET_BEFORE_ADD           = 0x05,   /* A get function was called before calling the add_frame_data function */
    AC4_DESC_ERR_INVALID_PRES_ID          = 0x06,
    AC4_DESC_ERR_INVALID_MSG_ID           = 0x07,
    AC4_DESC_ERR_ALREADY_EXISTS           = 0x08,
    AC4_DESC_WARN_TEXT_TRUNCATED          = 0x09,
    AC4_DESC_ERR_NO_ENTRY                 = 0x0A,
    AC4_DESC_ERR_MSG_ID_ALREADY_ASSIGNED  = 0x0B,
} ac4_descriptor_error_code;

/** @brief Warning codes for sub modules */
typedef struct ac4_descriptor_warnings_s
{
    unsigned int dvb_warnings;               /* A bit mask of warning codes defined in ac4dvb_desc_types.h */
} ac4_descriptor_warnings_t;

/** @brief ac4_descriptor_generator configuration 
 * This configuration struct is currently just a place holder.
 * It doesn't actually do anything at the moment. 
 */
typedef struct ac4_descriptor_generator_config_s
{
	int param;                     
} ac4_descriptor_generator_config_t;

/**
 * @brief ac4_descriptor_generator handle.
 */
typedef struct ac4_descriptor_generator_s*  ac4_descriptor_generator_handle;

/** @brief ac4_descriptor_generator version information.
    The version should be read as "v_api.v_fct.v_mtnc".
*/
typedef struct ac4_descriptor_generator_version_s
{
    int                    v_api;                      /**< API version. */
    int                    v_fct;                      /**< Functional change. */
    int                    v_mtnc;                     /**< Maintenance release. */
} ac4_descriptor_generator_version_t;

/** @brief Query the size of a ac4_descriptor_generator instance
 *  @return ac4_descriptor_generator status code
 */
ac4_descriptor_error_code
dlb_ac4_descriptor_generator_query_mem_size
    (ac4_descriptor_generator_config_t        *p_cfg          /**< [in] Configuration */
    ,size_t                                   *p_mem_sz       /**< [out] Instance size */
    );

/** @brief Open an instance of the ac4_descriptor_generator
 *  @return ac4_descriptor_generator status code
 */
ac4_descriptor_error_code
dlb_ac4_descriptor_generator_open
    (ac4_descriptor_generator_handle          *ph_ac4_desc    /**< [out] Pointer to handle */
    ,void                                     *p_mem          /**< [in] Allocated memory */
    ,ac4_descriptor_generator_config_t        *p_cfg          /**< [in] Configuration */
    );

/** @brief Add ac4 analyzer frame data to descriptor generator module
 *  @return ac4_descriptor_generator status code
 */
ac4_descriptor_error_code
dlb_ac4_descriptor_generator_add_frame_data
    (ac4_descriptor_generator_handle          h_ac4_desc       /**< [in, out] Descriptor Generator handle */
    ,ac4analyze_frame_data_t                  *p_ac4frame_data /**< [in] Pointer AC4 frame metadata */
    );

/** @brief Get the DVB descriptor data structures
 *  @return ac4_descriptor_generator status code
 */
ac4_descriptor_error_code
dlb_ac4_descriptor_generator_get_dvb_data
    (ac4_descriptor_generator_handle          h_ac4_desc       /**< [in, out] Descriptor Generator handle */
    ,ac4dvb_descriptor_data_t                 *p_dvb_data      /**< [out] Pointer to descriptor data structs */
    );

/**
 * @brief Add information for one message descriptor
 *
 * This function is provided for convenience only. All information is directly accessible in ac4dvb_descriptor_data_t. This
 * function adds information required to write a single message descriptor and links it in audio_preselection_descriptor_data_t.
 * The message_descriptor_data array in ac4dvb_descriptor_data_t can hold up to AC4DVB_DESC_MAX_NUM_MESSAGES messages.
 *
 * @return AC4ANALYZE_OK
 * @return AC4_DESC_ERR_INVALID_PRES_ID: The function failed to add a message descriptor ; preselection_id is not defined in
 *                                       audio_preselection_descriptor_data_t.
 * @return AC4_DESC_ERR_INVALID_MSG_ID: The function failed to add a message descriptor ; a different message_id was previously
 *                                      assigned to the preselection_id.
 * @return AC4_DESC_ERR_ALREADY_EXISTS: The function failed to add a message descriptor ; the (preselection_id, message_id,
 *                                      ISO_639_language_code) tuple already exists.
 * @return AC4_DESC_WARN_TEXT_TRUNCATED: A message descriptor was added, but the text was truncated.
 * @return AC4_DESC_ERR_NO_ENTRY: The function failed to add a message descriptor ; all array entries are already in use.
 * @return AC4_DESC_ERR_MSG_ID_ALREADY_ASSIGNED: The message identifier is already assigned to another preselection.
 */
ac4_descriptor_error_code
dlb_ac4_descriptor_generator_add_message_descriptor
    (ac4dvb_descriptor_data_t          *p_dvb_data             /**< [in, out] Pointer to descriptor data struct */
    ,unsigned char                      preselection_id        /**< [in] Preselection identifier */
    ,unsigned char                      message_id             /**< [in] Message identifier */
    ,language_code_data_t               ISO_639_language_code  /**< [in] ISO639 language code for the message text */
    ,const char*                        text_bytes             /**< [in] Message text with up to AC4DVB_MESSAGE_DESC_MAX_TEXT_LENGTH
                                                                         bytes. Longer text is truncated.
                                                                         See Annex A of DVB BlueBook A038r16 – (April 2023)
                                                                         for character encoding and control codes. */
    ,unsigned int                       text_length            /**< [in] Length of message text in bytes. A length field is used to
                                                                         support multi-byte character encodings that may contain
                                                                         zero bytes. */
    );

/** @brief Get the serialized DVB descriptor as a string of bytes
 *  @return ac4_descriptor_generator status code
 */
ac4_descriptor_error_code
dlb_ac4_descriptor_generator_get_dvb_descriptors
    (ac4_descriptor_generator_handle    h_ac4_desc             /**< [in, out] Descriptor Generator handle */
    ,ac4dvb_descriptor_data_t          *p_dvb_data             /**< [in] Pointer to descriptor data structs. Used to overwrite auto
                                                                         generated desc based on ac-4 metadata. May be NULL */
    ,ac4dvb_descriptors_t              *p_dvb_descriptors      /**< [out] Pointer to descriptor byte strings */
    );

/** @brief Get the module warning codes
 *  @return pointer to ac4_descriptor_warnings_t container for all sub-module bitmask, 
 *          currently only DVB. Null is returned if Null handle is passed in.
 */
ac4_descriptor_warnings_t*
dlb_ac4_descriptor_generator_get_warnings
    (ac4_descriptor_generator_handle          h_ac4_desc     /**< [in, out]  Descriptor Generator handle */
    );

/** @brief Close an instance of the ac4_descriptor_generator
 */
void
dlb_ac4_descriptor_generator_close
    (ac4_descriptor_generator_handle          h_ac4_desc     /**< [in, out]  Descriptor Generator handle */
    );

#ifdef __cplusplus
}
#endif

#endif /* __DLB_AC4_DESCRIPTOR_GENERATOR_API_H__ */

/**
 *  @}
 */
