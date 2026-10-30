#include "dlbpatdec.h"

#include "common.h"
#include "common/plugin.h"
#include "common/utils.h"
#include "common/metadata.h"

GST_DEBUG_CATEGORY_STATIC (gst_dlbpatdec_debug);
#define GST_CAT_DEFAULT gst_dlbpatdec_debug


// Samples of an AC-3 frame, used when the parser gives no frame duration
#define DLBPATDEC_FRAME_SAMPLES 1536

#define gst_dlbpatdec_parent_class parent_class
G_DEFINE_TYPE (GstDlbPatDec, gst_dlbpatdec, GST_TYPE_AUDIO_DECODER);

GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (
    dlbpatdec, 
    "dlbpatdec",
    GST_RANK_PRIMARY, 
    GST_DLBPATDEC_TYPE,
    GST_DEBUG_CATEGORY_INIT (
        gst_dlbpatdec_debug, "dlbpatdec", 0, "Dolby Pro decoding element"
    );
);


static const GstAudioChannelPosition channelLayoutDefault[DLB_PAIO_PCM_NUM_CHANNELS] = {
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE
};


static GstStaticPadTemplate _sinkTemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        DLBPAT_MIMETYPE_AC3 ", "
        "rate = (int) " G_STRINGIFY(DLBPAT_DEFAULT_SAMPLE_RATE) ", "
        "channels = [ 1, 16 ];"

        DLBPAT_MIMETYPE_EAC3  ", "
        "rate = (int) " G_STRINGIFY(DLBPAT_DEFAULT_SAMPLE_RATE) ", "
        "channels = [ 1, 16 ];"
    )
);


static GstStaticPadTemplate _srcTemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        DLBPAT_MIMETYPE_ES "(" DLBPAT_CAPS_FEATURE_METADATA "), "
        "format = (string) " GST_AUDIO_NE (S32) ", "
        "layout = (string) interleaved, "
        "rate = (int) " G_STRINGIFY(DLBPAT_DEFAULT_SAMPLE_RATE) ", "
        "channels = (int) 16, "
        "channel-mask = (bitmask) 0x0;"
    )
);


static void dlbpat_cleanup(GstDlbPatDec *self) {
    GST_TRACE_OBJECT(self, "CALL");

    if (NOT_NULL(self->padcState)) {

        DLBPAT_CALL_CHECK(dlb_padc_close, (self->padcState));

        self->padcState = NULL;
    }

    if (NOT_NULL(self->padcMd)) {
        DLBPAT_CALL_CHECK(dlb_pamif_md_segment_destroy, (&self->padcMd));

        self->padcMd = NULL;
    }

    self->padcInputSize = 0;
}


static gboolean dlbpat_prepare(GstDlbPatDec *self, gsize *outputLatencyInSamples) {
    int dlbRet = DLBPAT_SUCCESS;

    do {
        dlb_padc_requirements requirements;

        {
            dlb_padc_init_config config;

            DLBPAT_CALL_BREAK(dlbRet, dlb_padc_setup, (DLB_PADC_TYPE_DDP, &config));

            config.config.ddp.drc_setting       = DLB_PADC_DRC_DEFAULT;
            config.config.ddp.dmx_mode          = DLB_PADC_DOWNMIX_AUTO;
            config.config.ddp.error_concealment = DLB_PADC_ERROR_CONCEAL_DEFAULT;

            config.config.dolbye.program = DLB_PADC_PROG_1ST;

            config.p_callback            = NULL;
            config.p_user_data           = NULL;

            DLBPAT_CALL_BREAK(dlbRet, dlb_padc_query, (&config, &requirements));

            DLBPAT_CALL_BREAK(dlbRet, dlb_padc_open, (&self->padcState, &config));
        }

        DLBPAT_CALL_BREAK(dlbRet, dlb_pamif_md_segment_create, (&self->padcMd));

        self->padcInputSize = requirements.max_input_size;

        *outputLatencyInSamples = requirements.latency.ddp;
    } while (0);

    return dlbRet == DLBPAT_SUCCESS;
}


// Samples of the oldest frame still waiting for its audio (AC-3: 1536; E-AC-3 frames may be shorter)
static guint _dlbpatdec_frame_samples(GstDlbPatDec *self) {
    gpointer head = g_queue_peek_head(&self->pendingFrames);

    return head ? GPOINTER_TO_UINT(head) : DLBPATDEC_FRAME_SAMPLES;
}


// Hands the collected audio of the oldest pending frame to the base class, which stamps it with that frame's timestamp
static void _dlbpatdec_finish_frame(GstDlbPatDec *self) {
    GstBuffer *buffer = self->frameBuffer;

    self->frameBuffer = NULL;

    if (NOT_NULL(buffer)) {
        gst_buffer_set_size(buffer, DLBPAT_SAMPLES_SIZE(self->frameFilled));

        if (! self->frameFilled) {
            gst_buffer_unref(buffer);
            buffer = NULL;
        }
    }

    self->frameFilled = 0;

    if (g_queue_is_empty(&self->pendingFrames)) {
        if (buffer) {
            GST_WARNING_OBJECT(self, "Dropping %" G_GSIZE_FORMAT " bytes of output without an input frame", gst_buffer_get_size(buffer));

            gst_buffer_unref(buffer);
        }

        return;
    }

    g_queue_pop_head(&self->pendingFrames);

    gst_audio_decoder_finish_frame(GST_AUDIO_DECODER(self), buffer, 1);
}


static int dlbpat_process(GstDlbPatDec *self, uint8_t *buffer, gsize bufferSize) {
    int dlbRet = DLBPAT_SUCCESS;

    do {
        int goFurther = 1;
        unsigned int samplesAvailable = 0;

        if (NOT_NULL(buffer)) {
            dlb_paio_bitstream_block block;

            block.num_bitstreams  = 1;
            block.num_bytes[0]    = bufferSize;
            block.p_bitstreams[0] = buffer;
            
            DLBPAT_CALL_BREAK(dlbRet, dlb_padc_add_bytes, (self->padcState, &block, &goFurther, &samplesAvailable));
        }

        if (goFurther) {
            dlb_paio_pcm_block output;

            unsigned int cont = 0;
            do {
                if (NOT_NULL(buffer)) {
                    DLBPAT_CALL_BREAK(dlbRet, dlb_padc_process, (self->padcState, &cont, &samplesAvailable));

                } else {
                    GST_DEBUG_OBJECT(self, "Flushing audio decoder");

                    dlbRet = dlb_padc_flush(self->padcState, &cont, &samplesAvailable);
                    if (dlbRet != DLBPAT_SUCCESS && dlbRet != DLB_PADC_ERROR_FLUSHED) {
                        GST_ERROR_OBJECT(self, "Error flushing audio decoder!");
                        break;
                    }

                    GST_DEBUG_OBJECT(self, "Decoder is flushed: %d, ret: %d, samples: %d", cont, dlbRet, samplesAvailable);

                    cont = !cont;
                }

                while (samplesAvailable > 0) {
                    GstMapInfo outBufferMap;
                    guint toRead;
                    gsize offset;

                    if (self->primingSamples) {
                        // The priming is read on its own, so no output block mixes it with audio
                        toRead = MIN(samplesAvailable, self->primingSamples);

                        if (IS_NULL(self->frameBuffer)) {
                            self->frameBuffer = gst_buffer_new_allocate(NULL, DLBPAT_SAMPLES_SIZE(toRead), NULL);
                        }

                    } else {
                        guint frameSamples = _dlbpatdec_frame_samples(self);

                        if (IS_NULL(self->frameBuffer)) {
                            self->frameBuffer = gst_buffer_new_allocate(NULL, DLBPAT_SAMPLES_SIZE(frameSamples), NULL);
                            self->frameFilled = 0;
                        }

                        toRead = MIN(samplesAvailable, frameSamples - self->frameFilled);
                    }

                    offset = self->primingSamples ? 0 : DLBPAT_SAMPLES_SIZE(self->frameFilled);

                    gst_buffer_map(self->frameBuffer, &outBufferMap, GST_MAP_WRITE);
                    {
                        dlbpat_utils_paioPcmBlockInit(&output);

                        output.p_pcm       = (int32_t *) (outBufferMap.data + offset);
                        output.p_md        = self->padcMd;
                        output.num_samples = 0;

                        dlbRet = dlb_padc_get_samples(self->padcState, toRead, &output, &samplesAvailable);
                    }
                    gst_buffer_unmap(self->frameBuffer, &outBufferMap);

                    if (! output.num_samples) {
                        break;
                    }

                    if (self->primingSamples) {
                        self->primingSamples -= MIN(self->primingSamples, output.num_samples);

                        gst_buffer_replace(&self->frameBuffer, NULL);

                    } else {
                        dlbpat_metadata_add(self->frameBuffer, self->padcMd, DLBPAT_SAMPLES_SIZE(output.num_samples), self->sourceChannels);

                        self->frameFilled += output.num_samples;

                        if (self->frameFilled == _dlbpatdec_frame_samples(self)) {
                            _dlbpatdec_finish_frame(self);
                        }
                    }

                    if (dlbRet != DLBPAT_SUCCESS) {
                        break;
                    }
                }

                if (dlbRet != DLBPAT_SUCCESS) {
                    break;
                }
            } while (cont);
        }
    } while (0);

    return dlbRet;
}

static int _flush(GstDlbPatDec *self) {
    int ret = DLBPAT_SUCCESS;

    if (! self->isDrained) {
        ret = dlbpat_process(self, NULL, 0);

        // PADC does not return the last `latency` samples on flush. The partly filled frame is completed with
        // silence, so the stream keeps the length of the frames received and the next format starts on time.
        if (NOT_NULL(self->frameBuffer) && self->frameFilled && NOT_NULL(self->padcMd)) {
            guint frameSamples = _dlbpatdec_frame_samples(self);

            if (self->frameFilled < frameSamples) {
                gsize offset = DLBPAT_SAMPLES_SIZE(self->frameFilled);
                gsize size   = DLBPAT_SAMPLES_SIZE(frameSamples - self->frameFilled);

                gst_buffer_memset(self->frameBuffer, offset, 0, size);

                dlbpat_metadata_add(self->frameBuffer, self->padcMd, size, self->sourceChannels);

                self->frameFilled = frameSamples;
            }
        }

        dlbpat_cleanup(self);

        self->isDrained = TRUE;
    }

    // A partly filled frame and frames PADC gave no audio for
    while (NOT_NULL(self->frameBuffer) || ! g_queue_is_empty(&self->pendingFrames)) {
        _dlbpatdec_finish_frame(self);
    }

    return ret;
}

static int _prepare(GstDlbPatDec *self) {
    int ret = DLBPAT_SUCCESS;

    if (self->isDrained) {
        gsize outputLatencyInSamples;

        if (dlbpat_prepare(self, &outputLatencyInSamples)) {
            gint64 latency = gst_util_uint64_scale(outputLatencyInSamples, GST_SECOND, DLBPAT_DEFAULT_SAMPLE_RATE);

            GST_DEBUG_OBJECT(self, "Transcoder latency is %" GST_TIME_FORMAT, GST_TIME_ARGS (latency));

            gst_audio_decoder_set_latency(GST_AUDIO_DECODER(self), latency, latency);

            self->primingSamples = outputLatencyInSamples;

            self->isDrained = FALSE;

        } else {
            ret = DLBPAT_FAILURE;
        }
    }

    return ret;
}


static gboolean _dlbpatdec_start(GstAudioDecoder *decoder) {
    GstDlbPatDec *self = GST_DLBPATDEC_DEC(decoder);

    GST_TRACE_OBJECT(decoder, "CALL");

    do {
        self->padcInputSize = 0;

        self->padcState = NULL;
        self->padcMd    = NULL;
        self->isDrained = TRUE;

        self->sourceChannels = 0;
        self->primingSamples = 0;

        g_queue_init(&self->pendingFrames);

        self->frameBuffer = NULL;
        self->frameFilled = 0;
    } while (0);

    return TRUE;
}


static gboolean _dlbpatdec_stop(GstAudioDecoder *decoder) {
    GstDlbPatDec *self = GST_DLBPATDEC_DEC(decoder);

    GST_TRACE_OBJECT(decoder, "CALL");

    do {
        dlbpat_cleanup(self);

        g_queue_clear(&self->pendingFrames);

        gst_buffer_replace(&self->frameBuffer, NULL);
        self->frameFilled = 0;
    } while (0);

    return TRUE;
}


static GstFlowReturn _dlbpatdec_handle_frame(GstAudioDecoder *decoder, GstBuffer *inBuffer) {
    int dlbRet = DLBPAT_SUCCESS;
    
    GST_TRACE_OBJECT(decoder, "CALL for buffer %p", inBuffer);

    GstDlbPatDec *self = GST_DLBPATDEC_DEC(decoder);

    if (IS_NULL(inBuffer)) {
        _flush(self);
        
    } else {
        dlbRet = _prepare(self);
        if (dlbRet == DLBPAT_SUCCESS) {
            GstMapInfo inBufferMap;

            {
                guint frameSamples = GST_BUFFER_DURATION_IS_VALID(inBuffer)
                    ? (guint) gst_util_uint64_scale_round(GST_BUFFER_DURATION(inBuffer), DLBPAT_DEFAULT_SAMPLE_RATE, GST_SECOND)
                    : DLBPATDEC_FRAME_SAMPLES;

                g_queue_push_tail(&self->pendingFrames, GUINT_TO_POINTER(frameSamples));
            }

            gst_buffer_map(inBuffer, &inBufferMap, GST_MAP_READ);
            {
                gsize processed = 0;

                self->isDrained = FALSE;

                while (processed != inBufferMap.size) {
                    gsize toProcess = MIN(inBufferMap.size - processed, self->padcInputSize);

                    DLBPAT_CALL_BREAK(dlbRet, dlbpat_process, (self, inBufferMap.data + processed, toProcess));

                    processed += toProcess;
                }
            }
            gst_buffer_unmap(inBuffer, &inBufferMap);
        }
    }

    return dlbRet == DLBPAT_SUCCESS ? GST_FLOW_OK : GST_FLOW_ERROR;
}


static void _dlbpatdec_flush (GstAudioDecoder *decoder, gboolean hard) {
    GstDlbPatDec *self = GST_DLBPATDEC_DEC(decoder);

    GST_TRACE_OBJECT(self, "CALL");

    if (hard) {
        // Seek or flush: the held audio and frames are discarded with the rest of the stream
        dlbpat_cleanup(self);

        self->isDrained = TRUE;

        g_queue_clear(&self->pendingFrames);

        gst_buffer_replace(&self->frameBuffer, NULL);
        self->frameFilled = 0;

    } else {
        _flush(self);
    }
}


static gboolean _dlbpatdec_set_format(GstAudioDecoder *decoder, GstCaps *caps) {
    GstDlbPatDec *self = GST_DLBPATDEC_DEC(decoder);
    do {
        GST_TRACE_OBJECT(decoder, "CALL");

        // Drains the frames of the previous format, so they keep its source channel count
        _flush(self);

        {
            GstStructure *structure = gst_caps_get_structure(caps, 0);
            gint channels = 0;

            if (! gst_structure_get_int(structure, "channels", &channels) || channels < 0) {
                channels = 0;
            }

            self->sourceChannels = (guint) channels;

            GST_INFO_OBJECT(self, "Source channels: %u", self->sourceChannels);
        }

        {
            self->outputChannels = DLB_PAIO_PCM_NUM_CHANNELS;

            {
                GstAudioInfo audioInfo;

                gst_audio_info_init(&audioInfo);

                gst_audio_info_set_format(&audioInfo, GST_AUDIO_FORMAT_S32, DLBPAT_DEFAULT_SAMPLE_RATE, self->outputChannels, channelLayoutDefault);

                GstCaps *srcCaps = gst_audio_info_to_caps(&audioInfo);
                {
                    gst_caps_set_features_simple(srcCaps, gst_caps_features_from_string (DLBPAT_CAPS_FEATURE_METADATA));

                    gst_caps_set_simple(srcCaps, "channel-mask", GST_TYPE_BITMASK, 0, NULL);

                    gst_audio_decoder_set_output_caps(decoder, srcCaps);
                }
                gst_caps_unref(srcCaps);
            }
        }
    } while (0);

    return TRUE;
}


static void gst_dlbpatdec_init(GstDlbPatDec *decoder) {
    GST_TRACE_OBJECT(decoder, "CALL");

    do {
        gst_audio_decoder_set_use_default_pad_acceptcaps (GST_AUDIO_DECODER_CAST(decoder), TRUE);
        
        GST_PAD_SET_ACCEPT_TEMPLATE(GST_AUDIO_DECODER_SINK_PAD (decoder));
    } while (0);
}


static void gst_dlbpatdec_class_init (GstDlbPatDecClass * klass) {
    GObjectClass         *gobjectClass = G_OBJECT_CLASS (klass);
    GstElementClass      *elementClass = GST_ELEMENT_CLASS (klass);
    GstAudioDecoderClass *baseClass    = GST_AUDIO_DECODER_CLASS (klass);

    dlbpat_common_initialize();

    gst_element_class_add_static_pad_template(elementClass, &_srcTemplate);
    gst_element_class_add_static_pad_template(elementClass, &_sinkTemplate);

    gst_element_class_set_static_metadata(
        elementClass,
        "Dolby PAT audio decoder", 
        "Codec/Decoder/Audio",
        "Decodes Dolby DD/DD+/DE/DED2 to raw PCM", 
        GST_AUTHORS
    );

    baseClass->start        = GST_DEBUG_FUNCPTR(_dlbpatdec_start);
    baseClass->stop         = GST_DEBUG_FUNCPTR(_dlbpatdec_stop);
    baseClass->set_format   = GST_DEBUG_FUNCPTR(_dlbpatdec_set_format);
    baseClass->handle_frame = GST_DEBUG_FUNCPTR(_dlbpatdec_handle_frame);
    baseClass->flush        = GST_DEBUG_FUNCPTR(_dlbpatdec_flush);
}
