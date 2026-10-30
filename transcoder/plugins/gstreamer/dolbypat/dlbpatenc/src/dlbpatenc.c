#include "common.h"
#include "common/plugin.h"
#include "common/metadata.h"

#include "dlbpatenc.h"
#include "utils.h"
#include "types.h"


GST_DEBUG_CATEGORY (gst_dlbpatenc_debug);
#define GST_CAT_DEFAULT gst_dlbpatenc_debug


#define gst_dlbpatdec_parent_class parent_class
G_DEFINE_TYPE (GstDlbPatEnc, gst_dlbpatenc, GST_TYPE_AUDIO_ENCODER);

GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (
    dlbpatenc, 
    "dlbpatenc",
    GST_RANK_PRIMARY, 
    GST_DLBPATENC_TYPE,
    GST_DEBUG_CATEGORY_INIT (
        gst_dlbpatenc_debug, "dlbpatenc", 0, "Dolby Pro encoding element"
    );
);

#define DEFAULT_DATARATE      GST_DLBPAT_ENC_DATARATE_AUTO
#define DEFAULT_DIALNORM      DLB_PAEC_DIALNORM_DEFAULT
#define DEFAULT_DMIXMOD       DLB_PAEC_DDP_DMIXMOD_DEFAULT
#define DEFAULT_DRCLINE       DLB_PAEC_DRC_PROFILE_AUTO_DEFAULT
#define DEFAULT_DRCRF         DLB_PAEC_DRC_PROFILE_AUTO_DEFAULT
#define DEFAULT_SURROUND_TRIM DLB_PAEC_DDP_SURROUND_TRIM_AUTO_DEFAULT
#define DEFAULT_HEIGHT_TRIM   DLB_PAEC_DDP_HEIGHT_TRIM_AUTO_DEFAULT
#define DEFAULT_DOWNMIX_LEVEL DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_AUTO_DEFAULT


static GstStaticPadTemplate _sinkTemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
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


static GstStaticPadTemplate _srcTemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        DLBPAT_MIMETYPE_AC3 ", "
        "rate = (int) " G_STRINGIFY(DLBPAT_DEFAULT_SAMPLE_RATE) ", "
        "channels = (int) { 1, 2, 6, 10, 12, 16 }; "

        DLBPAT_MIMETYPE_EAC3  ", "
        "rate = (int) " G_STRINGIFY(DLBPAT_DEFAULT_SAMPLE_RATE) ", "
        "channels = (int) { 1, 2, 6, 10, 12, 16 }; "
    )
);


static void dlbpat_resetTimestamps(GstDlbPatEnc *self) {
    self->pendingInSamples  = 0;
    self->pendingOutSamples = 0;
    self->outSamplesKnown   = TRUE;
    self->lagSamples        = 0;
    self->pushLagSamples    = 0;
    self->pushOutSamples    = 0;
}


// Samples encoded in one AC-3 / E-AC-3 frame, from its header (0 when unknown, e.g. AC-4)
static guint dlbpat_frameSamples(const guint8 *data, gsize size) {
    static const guint blocks[] = { 1, 2, 3, 6 };

    if (size < 6 || data[0] != 0x0B || data[1] != 0x77) {
        return 0;
    }

    // bsid up to 10 is AC-3 (always 6 blocks); 11-16 is E-AC-3 with numblkscod in the fscod byte
    if ((data[5] >> 3) <= 10) {
        return 1536;
    }

    return 256 * (((data[4] >> 6) == 3) ? 6 : blocks[(data[4] >> 4) & 3]);
}


static void dlbpat_cleanup(GstDlbPatEnc *self) {
    GST_TRACE_OBJECT(self, "CALL");

    if (NOT_NULL(self->paecState)) {
        DLBPAT_CALL_CHECK(dlb_paec_close, (self->paecState));

        self->paecState = NULL;
    }

    self->paecInputSize   = 0;
    self->paecOutputSize  = 0;
    self->processedFrames = 0;
    self->isDrained       = TRUE;

    dlbpat_resetTimestamps(self);
}


static gboolean dlbpat_prepare(GstDlbPatEnc *self, gsize *outputLatencyInSamples) {
    int dlbRet = DLBPAT_SUCCESS;

    do {
        dlb_paec_requirements requirements;

        dlb_paec_type paecCodec;

        if (! dlbpat_utils_gstCodec2paecCodec(self->outputCodec, &paecCodec)) {
            GST_ERROR("The requested codec (%d) is not supported by Dolby PAEC", self->outputCodec);

            dlbRet = DLBPAT_FAILURE;
            break;
        }

        {
            dlb_paec_init_config config;

            DLBPAT_CALL_BREAK(dlbRet, dlb_paec_setup, (&config, paecCodec));
            DLBPAT_CALL_BREAK(dlbRet, dlb_paec_query, (&config, &requirements));
            DLBPAT_CALL_BREAK(dlbRet, dlb_paec_open,  (&self->paecState, &config));
        }

        // Set rutime configuration
        {
            dlb_paec_runtime_config runtimeConfig;

            DLBPAT_CALL_BREAK(dlbRet, dlb_paec_runtime_defaults_get, (self->paecState, &runtimeConfig));

            switch (self->outputCodec) {
                case DLBPAT_CODEC_AC3:
                case DLBPAT_CODEC_EAC3:
                    {
                        dlb_paec_runtime_config_ddp *ddp = &runtimeConfig.codec.ddp;

                        ddp->dialnorm      = self->dialnorm;
                        ddp->drc_line      = self->drcline;
                        ddp->drc_rf        = self->drcrf;
                        ddp->surround_trim = self->surroundTrim;
                        ddp->height_trim   = self->heightTrim;
                        ddp->dmixmod       = self->dmixmod;
                        ddp->ltrtcmixlev   = self->ltrtcmixlev;
                        ddp->ltrtsurmixlev = self->ltrtsurmixlev;
                        ddp->lorocmixlev   = self->lorocmixlev;
                        ddp->lorosurmixlev = self->lorosurmixlev;

                        if (! dlbpat_utils_gstDataRateToDdpDataRate(self->outputDataRate, &ddp->datarate)) {
                            GST_WARNING("Output datarate (%d) is not supported by Dolby PAEC DDP module - skipping creation", self->outputDataRate);

                            dlbRet = DLBPAT_FAILURE;
                            break;
                        }

                        if (! dlbpat_utils_gstChannels2peacDdpChannels(self->outputChannels, &ddp->channel_config)) {
                            GST_WARNING("Output channels number (%d) is not supported by Dolby PAEC DDP module - skipping creation", self->outputChannels);

                            dlbRet = DLBPAT_FAILURE;
                            break;
                        }
                    }
                    break;

                case DLBPAT_CODEC_AC4:
                    {
                        dlb_paec_runtime_config_ac4 *ac4 = &runtimeConfig.codec.ac4;

                        if (! dlbpat_utils_gstDataRateToAc4DataRate(self->outputDataRate, &ac4->apc.bitrate)) {
                            GST_ERROR("Output datarate (%d) is not supported by Dolby PAEC AC4 module", self->outputDataRate);
                            
                            dlbRet = DLBPAT_FAILURE;
                            break;
                        }

                        if (! dlbpat_utils_gstChannels2paecAc4Channels(self->outputChannels, &ac4->apc.bed_components[0].channel_mode)) {
                            GST_ERROR("Output channels number (%d) is not supported by Dolby PAEC AC4 module", self->outputChannels);

                            dlbRet = DLBPAT_FAILURE;
                        }
                    }
                    break;
            }

            if (dlbRet != DLBPAT_SUCCESS) {
                break;
            }

            DLBPAT_CALL_BREAK(dlbRet, dlb_paec_runtime_config_set, (self->paecState, &runtimeConfig));
        }

        self->paecInputSize  = DLBPAT_SAMPLES_SIZE(requirements.max_input_samples);
        self->paecOutputSize = requirements.max_output_size;

        *outputLatencyInSamples = requirements.latency;
    } while (0);

    return dlbRet == DLBPAT_SUCCESS;
}


static GstFlowReturn dlbpat_process(GstDlbPatEnc *self, uint8_t *buffer, gsize bufferSize, dlb_pamif_md_segment *md) {
    int dlbRet = DLBPAT_SUCCESS;

    gsize processedSize = 0;
    int goFurther;
    unsigned int samplesAvailable;
    
    do {
        gsize toProcess = MIN(bufferSize - processedSize, self->paecInputSize);

        goFurther = 1;

        if (toProcess) {
            dlb_paio_pcm_block block;

            dlbpat_utils_paioPcmBlockInit(&block);

            block.num_samples  = DLBPAT_SAMPLES_COUNT(toProcess);
            block.p_pcm        = (int32_t *) (buffer + processedSize);
            block.p_md         = md;

            DLBPAT_CALL_BREAK(dlbRet, dlb_paec_add_samples, (self->paecState, &block, &goFurther, &samplesAvailable));
        }

        if (goFurther) {
            dlb_paio_bitstream_block block;

            block.num_bitstreams = 1;

            unsigned int cont = 0;
            do {
                if (toProcess) {
                    DLBPAT_CALL_BREAK(dlbRet, dlb_paec_process, (self->paecState, &cont, &samplesAvailable));

                } else {
                    DLBPAT_CALL_BREAK(dlbRet, dlb_paec_flush, (self->paecState, &cont, &samplesAvailable));

                    cont = !cont;
                }

                if (samplesAvailable > 0) {
                    GstBuffer *outBuffer = gst_audio_encoder_allocate_output_buffer(GST_AUDIO_ENCODER(self), self->paecOutputSize);
                    GstMapInfo outBufferMap;

                    gst_buffer_map(outBuffer, &outBufferMap, GST_MAP_WRITE);
                    {
                        block.num_bytes[0] = 0;
                        block.p_bitstreams[0] = outBufferMap.data;

                        dlbRet = dlb_paec_get_frame(self->paecState, &block);
                    }
                    gst_buffer_unmap(outBuffer, &outBufferMap);

                    if (block.num_bytes[0] > 0) {
                        guint frameSamples = dlbpat_frameSamples(block.p_bitstreams[0], block.num_bytes[0]);

                        if (frameSamples) {
                            self->pendingOutSamples += frameSamples;

                        } else {
                            self->outSamplesKnown = FALSE;
                        }

                        gst_buffer_set_size(outBuffer, block.num_bytes[0]);

                        gst_adapter_push(self->outputAdapter, outBuffer);

                    } else {
                        gst_buffer_unref(outBuffer);
                    }

                    if (dlbRet != DLBPAT_SUCCESS) {
                        break;
                    }
                }
            } while (cont);

            if (dlbRet != DLBPAT_SUCCESS) {
                break;
            }
        }

        processedSize += toProcess;
    } while (processedSize != bufferSize);

    return (dlbRet != DLBPAT_SUCCESS) ? GST_FLOW_ERROR : GST_FLOW_OK;
}


static GstFlowReturn dlbpat_flushOutput(GstDlbPatEnc *self, gsize frames, gboolean discard) {
    gsize outputAvailable = gst_adapter_available(self->outputAdapter);

    if (outputAvailable == 0) {
        self->processedFrames += frames;

    } else {
        if (discard) {
           gst_adapter_clear(self->outputAdapter);

        } else {
            GstBuffer *outBufer = gst_adapter_take_buffer(self->outputAdapter, outputAvailable);

            // Consumed below: all pending input (-1). The buffer carries the encoding of input that started
            // lagSamples before the first pending sample (see _dlbpatenc_pre_push).
            self->pushLagSamples = self->outSamplesKnown ? self->lagSamples : 0;
            self->pushOutSamples = self->outSamplesKnown ? self->pendingOutSamples : 0;

            gst_audio_encoder_finish_frame(GST_AUDIO_ENCODER(self), outBufer, -1);

            if (self->outSamplesKnown) {
                self->lagSamples += (gint64) self->pendingInSamples - (gint64) self->pendingOutSamples;
            }

            self->pendingInSamples  = 0;
            self->pendingOutSamples = 0;
            self->outSamplesKnown   = TRUE;
            self->pushOutSamples    = 0;

            self->processedFrames = 0;
        }
    }

    return GST_FLOW_OK;
}


static GstFlowReturn dlbpat_flush(GstDlbPatEnc *self) {
    GstFlowReturn ret = dlbpat_process(self, NULL, 0, NULL);

    if (ret == GST_FLOW_OK) {
        ret = dlbpat_flushOutput(self, 0, FALSE);

    } else {
        dlbpat_flushOutput(self, 0, TRUE);
    }

    return ret;
}


static gboolean _dlbpatenc_start(GstAudioEncoder *encoder) {
    GstDlbPatEnc *self = GST_DLBPATENC_ENC(encoder);

    GST_TRACE_OBJECT(encoder, "CALL");

    do {
        self->paecInputSize  = 0;
        self->paecOutputSize = 0;

        self->paecState       = NULL;
        self->isDrained       = TRUE;
        self->processedFrames = 0;

        dlbpat_resetTimestamps(self);

        self->outputChannels = DLBPAT_CHANNELS_NOT_SET;
        self->outputCodec    = DLBPAT_DEFAULT_OUTPUT_CODEC;
    } while (0);

    return TRUE;
}


static gboolean _dlbpatenc_stop(GstAudioEncoder *encoder) {
    GstDlbPatEnc *self = GST_DLBPATENC_ENC(encoder);

    GST_TRACE_OBJECT(encoder, "CALL");

    do {
        dlbpat_flushOutput(self, 0, TRUE);
        dlbpat_cleanup(self);
    } while (0);

    return TRUE;
}


static GstFlowReturn _dlbpatenc_handle_frame(GstAudioEncoder *encoder, GstBuffer *inBuffer) {
    GstFlowReturn ret = GST_FLOW_OK;
  
    {
        GstDlbPatEnc *self = GST_DLBPATENC_ENC(encoder);

        if (IS_NULL(inBuffer)) {
            if (! self->isDrained) {
                dlbpat_flush(self);

                self->isDrained = TRUE;
            }
            
        } else {
            gsize metaCount = dlbpat_metadata_count(inBuffer);

            GstMapInfo inBufferMap;

            gst_buffer_map(inBuffer, &inBufferMap, GST_MAP_READ);
            {
                if (metaCount > 0) {
                    gsize inBufferOffset = 0;

                    GST_DEBUG("PCM blocks in buffer: %" G_GSIZE_FORMAT ", processedFrames: %" G_GSIZE_FORMAT, metaCount, self->processedFrames);

                    // GstAudioEncoder uses the GstAdapter class to keep input buffers. It is physically flushed on each call
                    // to the gst_audio_encoder_finish_frame() method.
                    // Since the Dolby codec introduces some latency during the first few calls to the handle_frame method,
                    // GstBuffers accumulate in the adapter, resulting in cumulative metadata in the input buffer.
                    for (gsize blockIdx = self->processedFrames; blockIdx < metaCount; blockIdx++) {
                        DlbPatMetadata *metadata = dlbpat_metadata_at(inBuffer, blockIdx);

                        ret = dlbpat_process(self, inBufferMap.data + inBufferOffset, metadata->blockSize, metadata->md);
                        if (ret != GST_FLOW_OK) break;

                        inBufferOffset += metadata->blockSize;
                    }

                } else {
                    ret = dlbpat_process(self, inBufferMap.data, inBufferMap.size, NULL);
                }
            }
            // The buffer holds only the input not handed over before (its metadata covers earlier input too)
            self->pendingInSamples += DLBPAT_SAMPLES_COUNT(inBufferMap.size);

            gst_buffer_unmap(inBuffer, &inBufferMap);

            if (ret == GST_FLOW_OK) {
                ret = dlbpat_flushOutput(self, metaCount - self->processedFrames, FALSE);

            } else {
                dlbpat_flushOutput(self, metaCount - self->processedFrames, TRUE);
            }
        }
    }

    return ret;
}


static void _dlbpatenc_flush (GstAudioEncoder *encoder) {
    GstDlbPatEnc *self = GST_DLBPATENC_ENC(encoder);
    
    {
        uint8_t dummyBuffer[self->paecOutputSize];
        int dlbRet;
        int flushed;

        // Simply drain internal encoder buffers
        do {
            int frameAvailable;

            DLBPAT_CALL_BREAK(dlbRet, dlb_paec_flush, (self->paecState, &flushed, &frameAvailable));

            if (frameAvailable) {
                dlb_paio_bitstream_block block;

                block.num_bitstreams = 1;
                block.p_bitstreams[0] = dummyBuffer;

                DLBPAT_CALL_BREAK(dlbRet, dlb_paec_get_frame, (self->paecState, &block));
            }
        } while (! flushed);
    }
    // The encoder holds no input any more
    dlbpat_resetTimestamps(self);
}


// Moves the timestamp the base class gave (its first pending input sample) back to the input the frames encode
static GstFlowReturn _dlbpatenc_pre_push(GstAudioEncoder *encoder, GstBuffer **buffer) {
    GstDlbPatEnc *self = GST_DLBPATENC_ENC(encoder);

    if (self->pushOutSamples > 0 && GST_BUFFER_PTS_IS_VALID(*buffer)) {
        GstClockTime pts   = GST_BUFFER_PTS(*buffer);
        GstClockTime shift = gst_util_uint64_scale_int(ABS(self->pushLagSamples), GST_SECOND, DLBPAT_DEFAULT_SAMPLE_RATE);

        *buffer = gst_buffer_make_writable(*buffer);

        if (self->pushLagSamples >= 0) {
            pts = pts > shift ? pts - shift : 0;

        } else {
            pts += shift;
        }

        GST_BUFFER_PTS(*buffer)      = pts;
        GST_BUFFER_DTS(*buffer)      = pts;
        GST_BUFFER_DURATION(*buffer) = gst_util_uint64_scale_int(self->pushOutSamples, GST_SECOND, DLBPAT_DEFAULT_SAMPLE_RATE);

        GST_LOG_OBJECT(self, "Output of %" G_GUINT64_FORMAT " samples at %" GST_TIME_FORMAT " (encoder holds %" G_GINT64_FORMAT " samples)",
            self->pushOutSamples, GST_TIME_ARGS(pts), self->pushLagSamples);
    }

    return GST_FLOW_OK;
}


static gboolean _dlbpatenc_set_format (GstAudioEncoder *encoder, GstAudioInfo *info) {
    gboolean ret = TRUE;

    {
        GstDlbPatEnc *self = GST_DLBPATENC_ENC(encoder);

        do {
            gsize outputLatencyInSamples;
            gboolean recreateDlbPat = FALSE;

            if (! self->isDrained) {
                dlbpat_flush(self);

                self->isDrained = TRUE;
            }

            // TODO: If runtime changes will be possible then implement PAEC runtime parameters 
            // update instead of recreating the PAEC context.
            dlbpat_cleanup(self);

            // Check if output codec has changed
            {
                DlbPatCodec newOutputCodec    = DLBPAT_CODEC_UNKNOWN;
                gint        newOutputChannels = DLBPAT_CHANNELS_NOT_SET;

                {
                    GstCaps *allowedCaps = gst_pad_get_allowed_caps(GST_AUDIO_ENCODER_SRC_PAD(encoder));

                    if (IS_NULL(allowedCaps) || gst_caps_get_size(allowedCaps) > 1) {
                        newOutputChannels = DLBPAT_DEFAULT_OUTPUT_CHANNELS;
                        newOutputCodec    = DLBPAT_DEFAULT_OUTPUT_CODEC;

                    } else {
                        GstStructure *s = gst_caps_get_structure(allowedCaps, 0);

                        if (gst_structure_get_field_type(s, "channels") == GST_TYPE_LIST) {
                            newOutputChannels = DLBPAT_DEFAULT_OUTPUT_CHANNELS;

                        } else {
                            gst_structure_get_int(s, "channels", &newOutputChannels);
                        }

                        newOutputCodec = dlbpat_utils_mimeTypeToCodec(
                            gst_structure_get_name(s)
                        );

                        gst_caps_unref(allowedCaps);
                    }
                }

                if (self->outputCodec != newOutputCodec) {
                    GST_INFO_OBJECT(self, "Output codec has changed %d -> %d", self->outputCodec, newOutputCodec);

                    self->outputCodec = newOutputCodec;
                }

                if (self->outputChannels != newOutputChannels) {
                    GST_INFO_OBJECT(self, "Output channels number has changed %d -> %d", self->outputChannels, newOutputChannels);

                    self->outputChannels = newOutputChannels;
                }
            }

            if (
                (self->outputChannels == DLBPAT_CHANNELS_NOT_SET) ||
                (self->outputCodec    == DLBPAT_CODEC_UNKNOWN)
            ) {
                GST_WARNING_OBJECT(self, "Input or Output parameters are incomplete - skipping allocating Dolby context");

                ret = FALSE;
                break;
            }

            if (self->outputChannels == 1) {
                self->outputDataRate = GST_DLBPAT_ENC_DATARATE_64K;
            }

            ret = dlbpat_prepare(self, &outputLatencyInSamples);
            if (! ret) break;

            // TODO: Verify if it is required - encoder does not provide silence frames at the beginning
            {
                gint64 latency = gst_util_uint64_scale(outputLatencyInSamples, GST_SECOND, DLBPAT_DEFAULT_SAMPLE_RATE);

                GST_DEBUG_OBJECT(encoder, "Transcoder latency is %" GST_TIME_FORMAT, GST_TIME_ARGS (latency));

                gst_audio_encoder_set_latency(encoder, latency, latency);
            }

            {
                GstCaps *srcCaps = gst_caps_new_simple(dlbpat_utils_codecToMimeType(self->outputCodec),
                    "rate", G_TYPE_INT, DLBPAT_DEFAULT_SAMPLE_RATE,
                    "framed", G_TYPE_BOOLEAN, TRUE,
                    "channels", G_TYPE_INT, self->outputChannels,
                    NULL
                );

                gst_audio_encoder_set_output_format(encoder, srcCaps);
                gst_caps_unref(srcCaps);
            }
        } while (0);

        if (! ret) {
            dlbpat_cleanup(self);
        }
    }

    return ret;
}


static void gst_dlbpatenc_init(GstDlbPatEnc *encoder) {
    GST_PAD_SET_ACCEPT_TEMPLATE(GST_AUDIO_ENCODER_SINK_PAD (encoder));

    encoder->outputDataRate = DEFAULT_DATARATE;
    encoder->dialnorm       = DEFAULT_DIALNORM;
    encoder->dmixmod        = DEFAULT_DMIXMOD;
    encoder->drcline        = DEFAULT_DRCLINE;
    encoder->drcrf          = DEFAULT_DRCRF;
    encoder->surroundTrim   = DEFAULT_SURROUND_TRIM;
    encoder->heightTrim     = DEFAULT_HEIGHT_TRIM;

    encoder->ltrtcmixlev   = DEFAULT_DOWNMIX_LEVEL;
    encoder->ltrtsurmixlev = DEFAULT_DOWNMIX_LEVEL;
    encoder->lorocmixlev   = DEFAULT_DOWNMIX_LEVEL;
    encoder->lorosurmixlev = DEFAULT_DOWNMIX_LEVEL;

    encoder->outputAdapter  = gst_adapter_new();
}


static void _dlbpatenc_finalize(GObject *object) {
    GstDlbPatEnc *self = GST_DLBPATENC_ENC(object);

    gst_object_unref(self->outputAdapter);

    G_OBJECT_CLASS(gst_dlbpatenc_parent_class)->finalize (object);
}


static void _dlbpatenc_set_property(GObject *object, guint propId, const GValue *value, GParamSpec *paramSpec) {
    GstDlbPatEnc *self = GST_DLBPATENC_ENC(object);

    switch (propId) {
        case PROPERTY_DATARATE:
            self->outputDataRate = g_value_get_enum(value);
            break;

        case PROPERTY_DIALNORM:
            self->dialnorm = g_value_get_int(value);
            break;

        case PROPERTY_DRCLINE:
            self->drcline = g_value_get_enum(value);
            break;

        case PROPERTY_DRCRF:
            self->drcrf = g_value_get_enum(value);
            break;

        case PROPERTY_SURROUND_TRIM:
            self->surroundTrim = g_value_get_enum(value);
            break;

        case PROPERTY_HEIGHT_TRIM:
            self->heightTrim = g_value_get_enum(value);
            break;

        case PROPERTY_DMIXMOD:
            self->dmixmod = g_value_get_enum(value);
            break;

        case PROPERTY_LTRTCMIXLEV:
            self->ltrtcmixlev = g_value_get_enum(value);
            break;

        case PROPERTY_LTRTSURMIXLEV:
            self->ltrtsurmixlev = g_value_get_enum(value);
            break;

        case PROPERTY_LOROCMIXLEV:
            self->lorocmixlev = g_value_get_enum(value);
            break;

        case PROPERTY_LOROSURMIXLEV:
            self->lorosurmixlev = g_value_get_enum(value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propId, paramSpec);
            break;
    }
}


static void _dlbpatenc_get_property(GObject *object, guint propId, GValue *value, GParamSpec *paramSpec) {
    GstDlbPatEnc *self = GST_DLBPATENC_ENC(object);

    switch (propId) {
        case PROPERTY_DATARATE:
            g_value_set_enum(value, self->outputDataRate);
            break;

        case PROPERTY_DIALNORM:
            g_value_set_int(value, self->dialnorm);
            break;

        case PROPERTY_DRCLINE:
            g_value_set_enum(value, self->drcline);
            break;

        case PROPERTY_DRCRF:
            g_value_set_enum(value, self->drcrf);
            break;

        case PROPERTY_SURROUND_TRIM:
            g_value_set_enum(value, self->surroundTrim);
            break;

        case PROPERTY_HEIGHT_TRIM:  
            g_value_set_enum(value, self->heightTrim);
            break;

        case PROPERTY_DMIXMOD:
            g_value_set_enum(value, self->dmixmod);
            break;

        case PROPERTY_LTRTCMIXLEV:
            g_value_set_enum(value, self->ltrtcmixlev);
            break;

        case PROPERTY_LTRTSURMIXLEV:
            g_value_set_enum(value, self->ltrtsurmixlev);
            break;

        case PROPERTY_LOROCMIXLEV:
            g_value_set_enum(value, self->lorocmixlev);
            break;

        case PROPERTY_LOROSURMIXLEV:
            g_value_set_enum(value, self->lorosurmixlev);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propId, paramSpec);
            break;
    }
}

static GstCaps *_dlbpatenc_sink_getcaps(GstAudioEncoder * enc, GstCaps * filter) {
    GstCaps *ret = NULL;
    GstPad *pad;

    pad = GST_AUDIO_ENCODER_SINK_PAD (enc);

    ret = gst_pad_get_current_caps (pad);
    if (ret == NULL) {
        ret = gst_pad_get_pad_template_caps (pad);
    }

    GST_DEBUG_OBJECT (pad, "Return caps %" GST_PTR_FORMAT, ret);

    return ret;
}

static void gst_dlbpatenc_class_init (GstDlbPatEncClass * klass) {
    GObjectClass         *gobjectClass = G_OBJECT_CLASS (klass);
    GstElementClass      *elementClass = GST_ELEMENT_CLASS (klass);
    GstAudioEncoderClass *baseClass    = GST_AUDIO_ENCODER_CLASS (klass);

    dlbpat_common_initialize();

    gst_element_class_add_static_pad_template(elementClass, &_srcTemplate);
    gst_element_class_add_static_pad_template(elementClass, &_sinkTemplate);

    gobjectClass->finalize     = GST_DEBUG_FUNCPTR(_dlbpatenc_finalize);
    gobjectClass->get_property = GST_DEBUG_FUNCPTR(_dlbpatenc_get_property);
    gobjectClass->set_property = GST_DEBUG_FUNCPTR(_dlbpatenc_set_property);

    baseClass->start        = GST_DEBUG_FUNCPTR(_dlbpatenc_start);
    baseClass->stop         = GST_DEBUG_FUNCPTR(_dlbpatenc_stop);
    baseClass->set_format   = GST_DEBUG_FUNCPTR(_dlbpatenc_set_format);
    baseClass->handle_frame = GST_DEBUG_FUNCPTR(_dlbpatenc_handle_frame);
    baseClass->flush        = GST_DEBUG_FUNCPTR(_dlbpatenc_flush);
    baseClass->getcaps      = GST_DEBUG_FUNCPTR(_dlbpatenc_sink_getcaps);
    baseClass->pre_push     = GST_DEBUG_FUNCPTR(_dlbpatenc_pre_push);

    gst_element_class_set_static_metadata(
        elementClass,
        "Dolby PAT audio encoder", 
        "Codec/Encoder/Audio",
        "Encodes raw PCM into Dolby DD/DD+/AC4", 
        GST_AUTHORS
    );

    // Properties
    {
        GParamSpec *properties[PROPERTY_LAST] = { NULL, };

        properties[PROPERTY_DATARATE] = g_param_spec_enum(
            "bitrate",
            "PEAC DD(P)/AC4 encoded data rate",
            "Data rate of output bitstream",
            dlbpatenc_datarate_getType(),
            DEFAULT_DATARATE,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        properties[PROPERTY_DIALNORM] = g_param_spec_int(
            "dialnorm",
            "Dialogue normalization range",
            "Dialogue normalization range",
            DLB_PAEC_DIALNORM_MIN,
            DLB_PAEC_DIALNORM_MAX,
            DEFAULT_DIALNORM,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        properties[PROPERTY_DRCLINE] = g_param_spec_enum(
            "drc-line",
            "Dynamic range control Line mode",
            "Dynamic range control Line mode",
            dlbpatenc_drcline_getType(),
            DEFAULT_DRCLINE,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        properties[PROPERTY_DRCRF] = g_param_spec_enum(
            "drc-rf",
            "Dynamic range control RF mode",
            "Dynamic range control RF mode",
            dlbpatenc_drcrf_getType(),
            DEFAULT_DRCRF,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        properties[PROPERTY_SURROUND_TRIM] = g_param_spec_enum(
            "surround-trim",
            "Surround trim value (in decibels)",
            "Surround trim value (in decibels)",
            dlbpatenc_surround_trim_getType(),
            DEFAULT_SURROUND_TRIM,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        properties[PROPERTY_HEIGHT_TRIM] = g_param_spec_enum(
            "height-trim",
            "Height trim value (in decibels)",
            "Height trim value (in decibels)",
            dlbpatenc_height_trim_getType(),
            DEFAULT_HEIGHT_TRIM,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        properties[PROPERTY_DMIXMOD] = g_param_spec_enum(
            "dmixmod",
            "Preferred stereo downmix mode",
            "Preferred stereo downmix mode",
            dlbpatenc_dmixmod_getType(),
            DEFAULT_DMIXMOD,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        properties[PROPERTY_LTRTCMIXLEV] = g_param_spec_enum(
            "ltrtcmixlev",
            "Lt/Rt center downmix Level",
            "Lt/Rt center downmix Level",
            dlbpatenc_downmix_level_getType(),
            DEFAULT_DOWNMIX_LEVEL,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        properties[PROPERTY_LTRTSURMIXLEV] = g_param_spec_enum(
            "ltrtsurmixlev",
            "Lt/Rt surround downmix Level",
            "Lt/Rt surround downmix Level",
            dlbpatenc_downmix_level_getType(),
            DEFAULT_DOWNMIX_LEVEL,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        properties[PROPERTY_LOROCMIXLEV] = g_param_spec_enum(
            "lorocmixlev",
            "Lo/Ro center downmix Level",
            "Lo/Ro center downmix Level",
            dlbpatenc_downmix_level_getType(),
            DEFAULT_DOWNMIX_LEVEL,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        properties[PROPERTY_LOROSURMIXLEV] = g_param_spec_enum(
            "lorosurmixlev",
            "Lo/Ro surround downmix Level",
            "Lo/Ro surround downmix Level",
            dlbpatenc_downmix_level_getType(),
            DEFAULT_DOWNMIX_LEVEL,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        g_object_class_install_properties(
            gobjectClass, PROPERTY_LAST, properties
        );
    }
}
