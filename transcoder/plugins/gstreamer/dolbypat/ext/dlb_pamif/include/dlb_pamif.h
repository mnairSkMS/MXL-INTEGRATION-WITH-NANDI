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
 *  @file dlb_pamif.h
 *
 *  @brief Dolby pro audio metadata interchange format component (PAMIF).
 *
 *  Top level functions for the PAMIF.
 */

#ifndef DLB_PAMIF_H
#define DLB_PAMIF_H


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup PAMIF_FUNCTIONS API Functions
 * @brief PAMIF API Functions
 * @{
 */

/*
 * Example calling sequence:
 *
 *  // Create a new empty metadata segment
 *  dlb_pamif_md_segment    *h_md_segment = NULL;
 *  dlb_pamif_md_segment_create(&h_md_segment);
 *
 *  // Create a new metadata segment by copying an exiting one
 *  dlb_pamif_md_segment    *h_md_segment_copy = NULL;
 *  dlb_pamif_md_segment_create_copy(&h_md_segment_copy, h_md_segment);
 *
 *  // Copy the original metadata segment into the existing copy
 *  dlb_pamif_md_segment_copy(h_md_segment_copy, h_md_segment);
 *
 *  // Destroy the metadata segments
 *  dlb_pamif_md_segment_destroy(&h_md_segment_copy);
 *  dlb_pamif_md_segment_destroy(&h_md_segment);
 *
 */


/**
 * @brief Opaque Dolby pro audio metadata interchange format component segment
 *
 * A metadata segment is a container corresponding to a block of PCM samples.
 */
typedef struct dlb_pamif_md_segment dlb_pamif_md_segment;

/**
 * @brief Library version structure
 */
typedef struct dlb_pamif_version_s
{
    unsigned int v_major;    /**< Major release */
    unsigned int v_minor;    /**< Minor release */
    unsigned int v_func;     /**< Functional release */
    unsigned int v_build;    /**< Build number */
} dlb_pamif_version;

/**
 * @brief PAMIF requirements structure
 */
typedef struct dlb_pamif_requirements_s
{
    unsigned long serial_buffer_size;    /**< Minimal required buffer size for serialization */
} dlb_pamif_requirements;

/**
 * @brief Status return codes of the pro audio metadata interchange format component
 */
enum dlb_pamif_status
{
     DLB_PAMIF_STATUS_OK               = 0  /**< Function returned successfully. */

    ,DLB_PAMIF_ERROR_NULL_POINTER      = 1  /**< A Null pointer was passed as input. */
    ,DLB_PAMIF_ERROR_MEMORY_ALLOCATION = 2  /**< Internal memory allocation failed. System heap is
                                             *   either out of memory or fragmented.
                                             */
    ,DLB_PAMIF_ERROR_CORRUPT_STATE     = 3  /**< Either the metadata segment has become corrupt or
                                             *   dlb_pamif_md_segment_create() has not been called.
                                             *   If corrupt, the segment must be destroyed and
                                             *   recreated.
                                             */
    ,DLB_PAMIF_ERROR_SERIALIZED_MD     = 10 /**< The input to dlb_pamif_md_segment_deserialize()
                                             *   is invalid. Either the serialized metadata is invalid or the
                                             *   length of the serialized metadata is invalid.
                                             */
};

/**
 * @brief Get requirements of the PAMIF
 *
 * @return dlb_pamif_status
 * - DLB_PAMIF_ERROR_NULL_POINTER:      if \<p_requirements\> is NULL
 * - DLB_PAMIF_STATUS_OK:               otherwise
 */
int
dlb_pamif_query
    (dlb_pamif_requirements   *p_requirements    /**< [out] Requirements of the PAMIF */
    );


/**
 * @brief Create a metadata segment
 *
 * Create a metadata segment container.
 *
 * @return dlb_pamif_status
 * - DLB_PAMIF_ERROR_NULL_POINTER:      if \<ph_segment\> is NULL
 * - DLB_PAMIF_ERROR_MEMORY_ALLOCATION: if any internal memory allocation fails
 * - DLB_PAMIF_STATUS_OK:               otherwise
 */
int
dlb_pamif_md_segment_create
    (dlb_pamif_md_segment   **ph_segment    /**< [out] Pointer to metadata segment handle */
    );


/**
 * @brief Create a metadata segment from a copy of another
 *
 * Create a copy of a metadata segment container \<ph_segment_new\>
 * from another metadata segment container \<h_segment_orig\>.
 *
 * @return dlb_pamif_status
 * - DLB_PAMIF_ERROR_NULL_POINTER:      if \<h_segment_new\> or \<ph_segment_orig\> is NULL
 * - DLB_PAMIF_ERROR_MEMORY_ALLOCATION: if any internal memory allocation fails
 * - DLB_PAMIF_ERROR_CORRUPT_STATE:     if \<h_segment_orig\> does not point to a valid dlb_pamif_md_segment
 * - DLB_PAMIF_STATUS_OK:               otherwise
 */
int
dlb_pamif_md_segment_create_copy
    (dlb_pamif_md_segment   **ph_segment_new    /**< [out] Pointer to new metadata segment handle */
    ,dlb_pamif_md_segment    *h_segment_orig    /**< [in]  Original metadata segment handle */
    );


/**
 * @brief Copy a metadata segment
 *
 * Copy one PAMIF metadata segment container \<h_segment_src\>
 * to another metadata segment container \<h_segment_dest\>.
 *
 * NOTE: h_segment_dest must be created beforehand.
 *
 * @return dlb_pamif_status
 * - DLB_PAMIF_ERROR_NULL_POINTER:      if \<h_segment_src\> or \<h_segment_dest\> is NULL
 * - DLB_PAMIF_ERROR_CORRUPT_STATE:     if \<h_segment_dest\> or \<h_segment_dest\> does not point to a
 *                                      valid dlb_pamif_md_segment
 * - DLB_PAMIF_STATUS_OK:               otherwise
 */
int
dlb_pamif_md_segment_copy
    (dlb_pamif_md_segment   *h_segment_dest     /**< [in,out] Destination metadata segment handle */
    ,dlb_pamif_md_segment   *h_segment_src      /**< [in]     Source metadata segment handle */
    );


/**
 * @brief This function is depracated and shall not be used
 */
int
dlb_pamif_md_segment_serialize
    (dlb_pamif_md_segment    *h_segment
    ,const char             **pp_serialized_md
    ,unsigned int            *p_length_in_bytes
    );


/**
 * @brief Serialize a metadata segment to the buffer provided by the user
 *
 * Serialize a metadata segment container \<h_segment\> to a char buffer
 * pointed to by \<p_serialized_md\>. The length (in bytes) of serialized metadata
 * is written to \<p_length_in_bytes\>.
 *
 * NOTE: The size of the buffer can be obtained from a call to @ref dlb_pamif_query function.
 * 
 * @return dlb_pamif_status
 * - DLB_PAMIF_ERROR_NULL_POINTER:      if \<h_segment\>, \<p_serialized_md\>, or \<p_length_in_bytes\> is NULL
 * - DLB_PAMIF_ERROR_CORRUPT_STATE:     if \<h_segment\> does not point to a valid dlb_pamif_md_segment
 * - DLB_PAMIF_STATUS_OK:               otherwise
 */
int
dlb_pamif_md_segment_serialize_to_buffer
    (dlb_pamif_md_segment    *h_segment             /**< [in]  metadata segment handle */
    ,char                    *p_serialized_md       /**< [out] pointer to buffer for serialized metadata */
    ,unsigned int            *p_length_in_bytes     /**< [out] length (in bytes) of serialized metadata */
    );


/**
 * @brief Deserialize from serialized metadata into a metadata segment
 *
 * Deserialize _already_ serialized metadata \<p_serialized_md\> of length (in bytes)
 * \<length_in_bytes\> into a metadata segment container \<h_segment\>.
 *
 * NOTE: The serialized metadata \<p_serialized_md\> must be derived from
 *       a previous call to dlb_pamif_md_segment_serialize_to_buffer().
 *
 * @return dlb_pamif_status
 * - DLB_PAMIF_ERROR_NULL_POINTER:      if \<h_segment\> or \<p_serialized_md\> is NULL
 * - DLB_PAMIF_ERROR_CORRUPT_STATE:     if \<h_segment\> does not point to a valid dlb_pamif_md_segment
 * - DLB_PAMIF_ERROR_SERIALIZED_MD:     if \<p_serialized_md\> does not point to valid serialized metadata
 *                                      or the \<length\> is incorrect.
 * - DLB_PAMIF_STATUS_OK:               otherwise
 */
int
dlb_pamif_md_segment_deserialize
    (dlb_pamif_md_segment   *h_segment              /**< [in,out] metadata segment handle */
    ,const char             *p_serialized_md        /**< [in]     pointer to serialized metadata */
    ,unsigned int            length_in_bytes        /**< [in]     length (in bytes) of serialized metadata */
    );


/**
 * @brief Destroy a metadata segment
 *
 * @return dlb_pamif_status
 * - TBD
 * - DLB_PAMIF_ERROR_NULL_POINTER:      if \<h_segment\> is NULL
 * - DLB_PAMIF_ERROR_CORRUPT_STATE:     if \<*ph_segment\> does not point to a valid dlb_pamif_md_segment
 * - DLB_PAMIF_STATUS_OK:               otherwise
 */
int
dlb_pamif_md_segment_destroy
    (dlb_pamif_md_segment   **ph_segment    /**< [in] Pointer to metadata segment handle */
    );


/**
 * @brief Clear/reset a metadata segment
 *
 * Clear a metadata segment container for reuse.
 *
 * @return dlb_pamif_status
 * - DLB_PAMIF_ERROR_NULL_POINTER:      if \<ph_segment\> is NULL
 * - DLB_PAMIF_ERROR_CORRUPT_STATE:     if \<h_segment\> does not point to a valid dlb_pamif_md_segment
 * - DLB_PAMIF_STATUS_OK:               otherwise
 */
int
dlb_pamif_md_segment_clear
    (dlb_pamif_md_segment   *h_segment  /**< [in/out] metadata segment handle */
    );

/**
 * @brief Fills version structure with library's current version
 *
 * @return dlb_pamif_status
 * - DLB_PAMIF_ERROR_NULL_POINTER: if p_version is NULL
 * - DLB_PAMIF_STATUS_OK:          otherwise
 */
int dlb_pamif_version_get
    (dlb_pamif_version *p_version
    );


/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* DLB_PAMIF_H */

