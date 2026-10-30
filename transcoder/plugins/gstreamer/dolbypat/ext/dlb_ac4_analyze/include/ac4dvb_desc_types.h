/**
 * @brief  Dolby ac4_descriptor_generator
 * @note   This program is protected under international and U.S. copyright laws
 *         as an unpublished work. This program is confidential and proprietary to
 *         Dolby Laboratories. Reproduction or disclosure, in whole or in part, or
 *         the production of derivative works without the express
 *         permission of Dolby Laboratories is prohibited.
 * 
 *         Copyright (C) 2016 Dolby Laboratories Inc.
 *         Copyright (C) 2018-2023 by Dolby International AB.
 *                     All rights reserved.
 *
 */

/* ETSI EN 300468 v1.16.1 (DVD BlueBook A038 2017Jan) */

/**
 *  @file
 *
 *  @brief AC-4 Descriptor Generator for DVB - Types
 */

/**
 * @ingroup dvb_desc_gen_api
 * @{
 */

#ifndef __AC4DVBDESC_DEF_H__
#define __AC4DVBDESC_DEF_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief warning bit mask codes
 */
#define AC4DVB_DESC_NO_WARNING                0 
#define AC4DVB_DESC_INVALID_ISO639_2_LANGCODE 1
#define AC4DVB_DESC_INVALID_ISO639_1_LANGCODE 2
#define AC4DVB_DESC_INVALID_LANGCODE_LENGTH   4
#define AC4DVB_DESC_UNKNOWN_LANGCODE          8

#define AC4DVB_DESC_MAX_NUM_PRESENTATIONS     32 /*  the DVB spec allows for a maximum number of 32 presentations (or preselections in DVB terminology) */
#define MAX_AUX_COMPONENTS                    3
#define AC4DVB_DESC_MAX_NUM_UI_LANGUAGES      4
#define AC4DVB_DESC_MAX_NUM_MESSAGES         (AC4DVB_DESC_MAX_NUM_PRESENTATIONS * AC4DVB_DESC_MAX_NUM_UI_LANGUAGES)

/* Common descriptor defines */
#define AC4DVB_MAX_DESC_LEN		     255 + 2   /* The one byte length field does not include descriptor_tag and descriptor_length */
#define AC4DVB_ISO639_2_CODE_LEN     3
#define AC4DVB_ISO639_1_CODE_LEN     2
#define AC4DVB_N_ISO639_CONVERSIONS  209
#define AC4DVB_DESC_TAG		         0x7F
#define AC4DVB_UNUSED_FIELD          0x00

/* Static AC-4 descriptor values */
#define AC4DVB_AC4_DESC_TAG_EXT 	 0x15
#define AC4DVB_AC4_DESC_LEN  	     0x02

/* AC-4 Audio Preselection values */
#define AC4DVB_APD_DESC_TAG_EXT	     0x19

/* Component Descriptor values */
#define AC4DVB_COMPONENT_DESC_TAG    0x50
#define AC4DVB_STREAM_CONTENT_EXT    0x0E /* NGA component type feature flags */
#define AC4DVB_STREAM_CONTENT        0x0B

/* Message Descriptor values */
#define AC4DVB_MESSAGE_DESC_TAG              0x7F
#define AC4DVB_MESSAGE_DESC_TAG_EXT          0x08
#define AC4DVB_MESSAGE_DESC_MAX_TEXT_LENGTH  250

/* audio_rendering_indication types (Section 6.4.1 Table 111) */
typedef enum
{
    AC4DVB_APD_AUDIO_RENDER_NO_PREF  = 0x00,       
    AC4DVB_APD_AUDIO_RENDER_STEREO   = 0x01,       
    AC4DVB_APD_AUDIO_RENDER_2D       = 0x02,
    AC4DVB_APD_AUDIO_RENDER_3D       = 0x03,
    AC4DVB_APD_AUDIO_RENDER_HP       = 0x04,
    AC4DVB_APD_AUDIO_RENDER_RESERVED = 0x07 /* last reserved value */
} audio_rendering_indication_t;

/* ISO_639 language code container */
typedef struct language_code_data_s
{
	unsigned char lang_code[AC4DVB_ISO639_2_CODE_LEN];                      /* 24 bits */
} language_code_data_t;

/* Component type bits container (Section 6.2.8 Table 27) */
typedef struct component_type_data_s
{
    unsigned char        reserved;                 /* 1 bit */
    unsigned char        b_pre_rendered_headphone; /* 1 bit */
    unsigned char        b_interactivity;          /* 1 bit */
    unsigned char        b_dialog_enhancement;     /* 1 bit */
    unsigned char        b_spoken_subtitles;       /* 1 bit */
    unsigned char        b_audio_description;      /* 1 bit */
    unsigned char        preferred_channel_layout; /* 2 bits */
} component_type_data_t;

/* Component tag container (Section 6.2.8 Table 25) */
typedef struct component_descriptor_data_s
{
	unsigned char         descriptor_tag;              /* 8 bits */ 
	unsigned char         descriptor_length;           /* 8 bits */ 
    unsigned char         stream_content_ext;          /* 4 bits */
    unsigned char         stream_content;              /* 4 bits */
    component_type_data_t component_type;              /* 8 bits */
    unsigned char         component_tag;               /* 8 bits */
    language_code_data_t  ISO_639_language_code;
    unsigned char         text_char;                   /* undefined */
} component_descriptor_data_t;

/* Mulit Stream Info container */
typedef struct multi_stream_info_data_s
{
	unsigned char        num_aux_components;                /* 3 bits */
	unsigned char        reserved_zero_future_use;          /* 5 bits */
	unsigned char        component_tag[MAX_AUX_COMPONENTS]; /* 24 bits */        
} multi_stream_info_data_t;

/* Preselection container (Section 6.4.1 Table 110) */
typedef struct preselection_data_s
{
	unsigned char            preselection_id;             /* 5 bits */
	unsigned char            audio_rendering_indication;  /* 3 bits */
	unsigned char            audio_description;           /* 1 bit */
	unsigned char            spoken_subtitles;            /* 1 bit */
	unsigned char            dialogue_enhancement;        /* 1 bit */
	unsigned char            interactivity_enabled;       /* 1 bit */
	unsigned char            language_code_present;       /* 1 bit */
	unsigned char            text_label_present;          /* 1 bit */
	unsigned char            multi_stream_info_present;   /* 1 bit */
	unsigned char            future_extension;            /* 1 bit */
	language_code_data_t     ISO_639_language_code;       /* 24 bit */
	unsigned char            message_id;                  /* 8 bits */
	multi_stream_info_data_t multi_stream_info;
} preselection_data_t;

/* Audio Preselection Descriptor container (Section 6.4.1 Table 110) */
typedef struct audio_preselection_descriptor_data_s
{
	unsigned char       descriptor_tag;              /* 8 bits */ 
	unsigned char       descriptor_length;           /* 8 bits */ 
	unsigned char       descriptor_tag_extension;    /* 8 bits */ 
	unsigned char       num_preselections;           /* 5 bits */
	preselection_data_t preselections[AC4DVB_DESC_MAX_NUM_PRESENTATIONS];
	unsigned char       reserved_zero_future_use;    /* 3 bits */
} audio_preselection_descriptor_data_t;

/* AC4 Descriptor container (Annex D Table D.11) */
typedef struct ac4_descriptor_data_s
{
	unsigned char     descriptor_tag;              /* 8 bits */ 
	unsigned char     descriptor_length;           /* 8 bits */ 
	unsigned char     descriptor_tag_extension;    /* 8 bits */ 
	unsigned char     ac4_config_flag;             /* 1 bit (always set to 0) */ 
    unsigned char     ac4_config;                  /* 8 bits (always set to 0) */
	unsigned char     ac4_toc_flag;                /* 1 bit (always set to 0) */   
	unsigned char     reserved_future_use;         /* 6 bits (always set to 0) */  
} ac4_descriptor_data_t;

/* "legacy" Mix Type - not used in this version */
typedef enum 
{
	AC4DVB_DESC_MIX_TYPE_DEPENDENT   = 0x00,
	AC4DVB_DESC_MIX_TYPE_INDEPENDENT = 0x01
} mix_type_t;

/* "legacy" Editorial Classification  - not used in this version*/
typedef enum 
{
	AC4DVB_CLASSIFICATION_MAIN                  = 0x00,
	AC4DVB_CLASSIFICATION_VISUALLY_IMPARED      = 0x01,
	AC4DVB_CLASSIFICATION_HEARING_IMPARED       = 0x02,
	AC4DVB_CLASSIFICATION_SPOKEN_SUBTITLES      = 0x03,
	AC4DVB_CLASSIFICATION_DEPENDENT_PARAMETERIC = 0x04,
	AC4DVB_CLASSIFICATION_UNSPECIFIC            = 0x17
} editorial_classification_t;

/* "legacy" Supplementary Audio Descriptor container - not used in this version */
typedef struct supplementary_audio_descriptor_data_s
{
	unsigned char        descriptor_tag;              /* 8 bits */ 
	unsigned char        descriptor_length;           /* 8 bits */ 
	unsigned char        descriptor_tag_extension;    /* 8 bits */
	unsigned char 	     mix_type;                    /* 1 bit */
	unsigned char        editorial_classification;    /* 5 bits */
	unsigned char        reserved_future_use;         /* 1 bit */
	unsigned char	     language_code_present;       /* 1 bit */
	language_code_data_t ISO_639_language_code;
} supplementary_audio_descriptor_data_t;

/* Message Descriptor (Section 6.4.8 Table 148)*/
typedef struct message_descriptor_data_s
{
	unsigned char        descriptor_tag;              /* 8 bits */ 
	unsigned char        descriptor_length;           /* 8 bits */ 
	unsigned char        descriptor_tag_extension;    /* 8 bits */
    unsigned char        message_id;                  /* 8 bits */
	language_code_data_t ISO_639_language_code;       /* 24 bits */
    char                 text_bytes[AC4DVB_MESSAGE_DESC_MAX_TEXT_LENGTH+1];
    unsigned int         text_length;
} message_descriptor_data_t;

/**
 * @brief The AC4 DVB descriptor data containers 
 */
typedef struct ac4dvb_descriptor_data_s
{
	audio_preselection_descriptor_data_t  audio_preselection_descriptor_data;
    component_descriptor_data_t           component_descriptor_data[AC4DVB_DESC_MAX_NUM_PRESENTATIONS];
	unsigned char                         num_message_descriptors;
	message_descriptor_data_t             message_descriptor_data[AC4DVB_DESC_MAX_NUM_MESSAGES];
	ac4_descriptor_data_t                 ac4_descriptor_data;
} ac4dvb_descriptor_data_t;

/**
 * @brief Generic container for a AC4 DVB serialized descriptor 
 */
typedef struct descriptor_s
{
    unsigned char byte_string[AC4DVB_MAX_DESC_LEN];
    unsigned int  length;
} descriptor_t; 

/**
 * @brief Container for all AC4 DVB serialized descriptors 
 */
typedef struct ac4dvb_descriptors_s
{
    descriptor_t audio_preselection_descriptor;
    descriptor_t component_descriptors[AC4DVB_DESC_MAX_NUM_PRESENTATIONS]; 
	descriptor_t message_descriptors[AC4DVB_DESC_MAX_NUM_MESSAGES];
	descriptor_t ac4_descriptor;
} ac4dvb_descriptors_t;

#ifdef __cplusplus
}
#endif

#endif  /* __AC4DVBDESC_DEF_H__ */

/**
 *  @}
 */
