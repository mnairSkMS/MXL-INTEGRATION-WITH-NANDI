#include <gst/audio/audio-format.h>

#include "common.h"
#include "common/plugin.h"
#include "common/utils.h"
#include "common/metadata.h"
#include "common/queue.h"

#include "dlbpatmx.h"
#include "types.h"


GST_DEBUG_CATEGORY (gst_dlbpatmx_debug);
#define GST_CAT_DEFAULT gst_dlbpatmx_debug

#define gst_dlbpatmx_parent_class parent_class
G_DEFINE_TYPE (GstDlbPatMx, gst_dlbpatmx, GST_TYPE_BASE_TRANSFORM);

GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (
    dlbpatmx, 
    "dlbpatmx",
    GST_RANK_PRIMARY, 
    GST_DLBPATMX_TYPE,
    GST_DEBUG_CATEGORY_INIT (
        gst_dlbpatmx_debug, "dlbpatmx", 0, "Dolby Pro Renderer element"
    );
);


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

        DLBPAT_MIMETYPE_ES ", "
        "format = (string) " GST_AUDIO_NE (S32) ", "
        "layout = (string) interleaved, "
        "rate = (int) " G_STRINGIFY(DLBPAT_DEFAULT_SAMPLE_RATE) ", "
        "channels = (int) 2, "
        "channel-mask = (bitmask) 0x3;"

        // 5.1 with rear (first: what audioconvert produces when folding 7.1) or side surrounds, see dlbpat_utils_normalizePositions()
        DLBPAT_MIMETYPE_ES ", "
        "format = (string) " GST_AUDIO_NE (S32) ", "
        "layout = (string) interleaved, "
        "rate = (int) " G_STRINGIFY(DLBPAT_DEFAULT_SAMPLE_RATE) ", "
        "channels = (int) 6, "
        "channel-mask = (bitmask) 0x3f;"

        DLBPAT_MIMETYPE_ES ", "
        "format = (string) " GST_AUDIO_NE (S32) ", "
        "layout = (string) interleaved, "
        "rate = (int) " G_STRINGIFY(DLBPAT_DEFAULT_SAMPLE_RATE) ", "
        "channels = (int) 6, "
        "channel-mask = (bitmask) 0xc0f;"

        DLBPAT_MIMETYPE_ES ", "
        "format = (string) " GST_AUDIO_NE (S32) ", "
        "layout = (string) interleaved, "
        "rate = (int) " G_STRINGIFY(DLBPAT_DEFAULT_SAMPLE_RATE) ", "
        "channels = (int) 6, "
        "channel-mask = (bitmask) 0xc00000f;"

        DLBPAT_MIMETYPE_ES ", "
        "format = (string) " GST_AUDIO_NE (S32) ", "
        "layout = (string) interleaved, "
        "rate = (int) " G_STRINGIFY(DLBPAT_DEFAULT_SAMPLE_RATE) ", "
        "channels = (int) 8, "
        "channel-mask = (bitmask) 0xc00300f;"

        DLBPAT_MIMETYPE_ES ", "
        "format = (string) " GST_AUDIO_NE (S32) ", "
        "layout = (string) interleaved, "
        "rate = (int) " G_STRINGIFY(DLBPAT_DEFAULT_SAMPLE_RATE) ", "
        "channels = (int) 10, "
        "channel-mask = (bitmask) 0xc03300f;"

        DLBPAT_MIMETYPE_ES ", "
        "format = (string) " GST_AUDIO_NE (S32) ", "
        "layout = (string) interleaved, "
        "rate = (int) " G_STRINGIFY(DLBPAT_DEFAULT_SAMPLE_RATE) ", "
        "channels = (int) 12, "
        "channel-mask = (bitmask) 0xc03303f;"
        
        DLBPAT_MIMETYPE_ES ", "
        "format = (string) " GST_AUDIO_NE (S32) ", "
        "layout = (string) interleaved, "
        "rate = (int) " G_STRINGIFY(DLBPAT_DEFAULT_SAMPLE_RATE) ", "
        "channels = (int) 16, "
        "channel-mask = (bitmask) 0xf0f303f;"
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


static void dlbpat_cleanup(GstDlbPatMx *self) {
    GST_TRACE_OBJECT(self, "CALL");

    if (NOT_NULL(self->padmxState)) {
        DLBPAT_CALL_CHECK(dlb_padmx_close, (self->padmxState));

        self->padmxState = NULL;
    }

    if (NOT_NULL(self->padmxMd)) {
        DLBPAT_CALL_CHECK(dlb_pamif_md_segment_destroy, (&self->padmxMd));

        self->padmxMd = NULL;
    }

    if (NOT_NULL(self->inputPcmBuffer)) {
        free(self->inputPcmBuffer);

        self->inputPcmBuffer = NULL;
    }

    self->padmxInputSize      = 0;
    self->padmxOutputSize     = 0;
    self->padmxLatencySamples = 0;
}


static gboolean dlbpat_prepare(GstDlbPatMx *self) {
    int dlbRet = DLBPAT_SUCCESS;

    do {
        dlb_padmx_requirements requirements;

        {
            dlb_padmx_init_config config;

            DLBPAT_CALL_BREAK(dlbRet, dlb_padmx_setup, (&config));

            config.input_speaker_cfg  = self->inputChannels;
            config.target_speaker_cfg = self->outputChannels;

            // Downmix by default - determine mode automatically from metadata
            if (! self->inputRawPcm) {
                config.renderer_mode = self->sourceMode ? self->sourceRenderer : DLB_PADMX_RENDERER_MODE_DOWNMIX;

            } else {
                // Ref: DevelopmentGuideDolbyProAudioTranscoder.pdf, page 348
                // "Output channel configuration"
                if (config.target_speaker_cfg > DLB_PADMX_TARGET_SPEAKER_CFG_5_1_4) {
                    // From: DevelopmentGuideDolbyProAudioTranscoder.pdf
                    // [b] Because the Dolby loudness leveler component supports up to 10 input channels, we recommend not to use this channel
                    // configurations even though it is available for encoding to Dolby Digital Plus with Dolby Atmos content.
                    GST_ERROR("PADMX supports upmix from 2.0 to a maximum 5.1.4 only");
                    dlbRet = DLBPAT_FAILURE;

                } else {
                    switch (config.input_speaker_cfg) {
                        case DLB_PADMX_INPUT_SPEAKER_CFG_2_0:
                            // 5_1_2 configuration is not supported by encoder
                            if (
                                (config.target_speaker_cfg == DLB_PADMX_TARGET_SPEAKER_CFG_5_1)   ||
                                // (config.target_speaker_cfg == DLB_PADMX_TARGET_SPEAKER_CFG_5_1_2) ||
                                (config.target_speaker_cfg == DLB_PADMX_TARGET_SPEAKER_CFG_5_1_4)
                            ) {
                                config.renderer_mode = DLB_PADMX_RENDERER_MODE_UPMIX;
                            }
                            break;

                        case DLB_PADMX_INPUT_SPEAKER_CFG_5_1:
                            // 5_1_2 configuration is not supported by encoder
                            if (
                                (config.target_speaker_cfg == DLB_PADMX_TARGET_SPEAKER_CFG_5_1)   ||
                                // (config.target_speaker_cfg == DLB_PADMX_TARGET_SPEAKER_CFG_5_1_2) ||
                                (config.target_speaker_cfg == DLB_PADMX_TARGET_SPEAKER_CFG_5_1_4)
                            ) {
                                config.renderer_mode = DLB_PADMX_RENDERER_MODE_UPMIX;
                            }
                            break;

                        case DLB_PADMX_INPUT_SPEAKER_CFG_5_1_2:
                            // 5_1_2 configuration is not supported by encoder
                            if (
                                // (config.target_speaker_cfg == DLB_PADMX_TARGET_SPEAKER_CFG_5_1_2) ||
                                (config.target_speaker_cfg == DLB_PADMX_TARGET_SPEAKER_CFG_5_1_4)
                            ) {
                                config.renderer_mode = DLB_PADMX_RENDERER_MODE_UPMIX;
                            }
                            break;

                        case DLB_PADMX_INPUT_SPEAKER_CFG_5_1_4:
                            if (
                                (config.target_speaker_cfg == DLB_PADMX_TARGET_SPEAKER_CFG_5_1_4)
                            ) {
                                config.renderer_mode = DLB_PADMX_RENDERER_MODE_UPMIX;
                            }
                            break;

                        case DLB_PADMX_INPUT_SPEAKER_CFG_METADATA:
                            break;
                    }
                }

                if (dlbRet != DLBPAT_SUCCESS) {
                    break;
                }
            }

            switch (self->downmixMode) {
                case DLB_PADMX_MODE_DOWNMIX:
                    {
                        GST_INFO("Forcing downmix mode");
                        config.renderer_mode = DLB_PADMX_RENDERER_MODE_DOWNMIX;
                    }
                    break;

                case DLB_PADMX_MODE_UPMIX:
                    {
                        GST_INFO("Forcing upmix mode");
                        config.renderer_mode = DLB_PADMX_RENDERER_MODE_UPMIX;
                    }
                    break;

                default:
                    break;
            }

            DLBPAT_CALL_BREAK(dlbRet, dlb_padmx_query, (&config, &requirements));
            DLBPAT_CALL_BREAK(dlbRet, dlb_padmx_open,  (&self->padmxState, &config));

            GST_INFO_OBJECT(self, "PADMX open: input %d, target %d, renderer %d, latency %u samples",
                config.input_speaker_cfg, config.target_speaker_cfg, config.renderer_mode, requirements.latency
            );
        }

        DLBPAT_CALL_BREAK(dlbRet, dlb_pamif_md_segment_create, (&self->padmxMd));

        self->padmxInputSize      = DLBPAT_SAMPLES_SIZE(requirements.max_input_samples);
        self->padmxOutputSize     = DLBPAT_SAMPLES_SIZE(requirements.max_output_samples);
        self->padmxLatencySamples = requirements.latency;
        self->instanceSamples     = 0;

        if (self->inputRawPcm) {
            self->inputPcmBuffer = (guint8 *) malloc(self->padmxInputSize);
        }
    } while (0);

    return dlbRet == DLBPAT_SUCCESS;
}


static GstFlowReturn dlbpat_process(GstDlbPatMx *self, uint8_t *buffer, gsize bufferSize, dlb_pamif_md_segment *md) {
    int dlbRet = DLBPAT_SUCCESS;

    gsize processedSize = 0;
    int goFurther;
    unsigned int samplesAvailable;

    GST_DEBUG("Call for %" G_GSIZE_FORMAT " bytes", bufferSize);

    do {
        gsize toProcess = MIN(bufferSize - processedSize, self->padmxInputSize);

        goFurther = 1;

        if (toProcess) {
            dlb_paio_pcm_block block;

            dlbpat_utils_paioPcmBlockInit(&block);

            block.num_samples  = DLBPAT_SAMPLES_COUNT(toProcess);
            block.p_pcm        = (int32_t *)(buffer + processedSize);
            block.p_md         = md;

            GST_DEBUG("toProcess: %" G_GSIZE_FORMAT ", %u", toProcess, block.num_samples);

            DLBPAT_CALL_BREAK(dlbRet, dlb_padmx_add_samples, (self->padmxState, &block, &goFurther, &samplesAvailable));
        }

        if (goFurther) {
            dlb_paio_pcm_block output;

            unsigned int cont = 0;
            do {
                if (toProcess) {
                    DLBPAT_CALL_BREAK(dlbRet, dlb_padmx_process, (self->padmxState, &cont, &samplesAvailable));

                } else {
                    DLBPAT_CALL_BREAK(dlbRet, dlb_padmx_flush, (self->padmxState, &cont, &samplesAvailable));

                    cont = !cont;
                }

                GST_DEBUG("Process finished, samles to read: %u, cont: %u", samplesAvailable, cont);

                // Reads stop at timeline marks, so every output buffer maps to one input run
                while (samplesAvailable) {
                    GstBuffer *outBuffer;
                    GstMapInfo outBufferMap;
                    guint toRead;

                    dlbpat_timeline_due(&self->timeline, &self->outputQueue);

                    toRead    = dlbpat_timeline_chunk(&self->timeline, samplesAvailable);
                    outBuffer = gst_buffer_new_allocate(NULL, DLBPAT_SAMPLES_SIZE(toRead), NULL);

                    gst_buffer_map(outBuffer, &outBufferMap, GST_MAP_WRITE);
                    {
                        dlbpat_utils_paioPcmBlockInit(&output);

                        output.p_pcm       = (int32_t *) outBufferMap.data;
                        output.p_md        = self->padmxMd;
                        output.num_samples = 0;

                        dlbRet = dlb_padmx_get_samples(self->padmxState, toRead, &output, &samplesAvailable);
                    }
                    gst_buffer_unmap(outBuffer, &outBufferMap);

                    if (dlbRet != DLBPAT_SUCCESS) {
                        GST_ERROR_OBJECT(self, "dlb_padmx_get_samples(%u) has failed with code: %d!", toRead, dlbRet);
                    }

                    if (! dlbpat_queue_output(&self->timeline, &self->outputQueue, outBuffer, self->padmxMd, output.num_samples)) {
                        break;
                    }

                    if (dlbRet != DLBPAT_SUCCESS) {
                        break;
                    }
                }

                if (dlbRet != DLBPAT_SUCCESS) {
                    break;
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


static gboolean _dlbpatmx_start(GstBaseTransform *trans) {
    GstDlbPatMx *self = GST_DLBPATMX_PROC(trans);

    GST_LOG_OBJECT(self, "call");

    return TRUE;
}


static gboolean _dlbpatmx_stop(GstBaseTransform *trans) {
    GstDlbPatMx *self = GST_DLBPATMX_PROC(trans);

    GST_LOG_OBJECT(self, "call");

    dlbpat_cleanup(self);

    dlbpat_timeline_clear(&self->timeline);
    dlbpat_queue_clear(&self->outputQueue);

    gst_caps_replace(&self->inputCaps, NULL);

    return TRUE;
}


static gboolean _dlbpatmx_query(GstBaseTransform *trans, GstPadDirection direction, GstQuery * query) {
    gboolean ret = TRUE;

    {
        GstDlbPatMx *self = GST_DLBPATMX_PROC(trans);

        switch (GST_QUERY_TYPE (query)) {
            case GST_QUERY_LATENCY:
                if ((gst_pad_peer_query (GST_BASE_TRANSFORM_SINK_PAD (trans), query))) {
                    guint64 latency = gst_util_uint64_scale_round(self->padmxLatencySamples, GST_SECOND, DLBPAT_DEFAULT_SAMPLE_RATE);

                    GstClockTime min, max;
                    gboolean live;

                    gst_query_parse_latency (query, &live, &min, &max);

                    min += latency;
                    if (GST_CLOCK_TIME_IS_VALID (max)) {
                        max += latency;
                    }

                    GST_DEBUG_OBJECT (self, "Calculated total latency : min %" GST_TIME_FORMAT " max %" GST_TIME_FORMAT, GST_TIME_ARGS (min), GST_TIME_ARGS (max));

                    gst_query_set_latency(query, live, min, max);
                }
                break;

            case GST_QUERY_CAPS:
                {
                    GstCaps *result = gst_pad_get_pad_template_caps(
                        direction == GST_PAD_SINK ? GST_BASE_TRANSFORM_SINK_PAD(trans) : GST_BASE_TRANSFORM_SRC_PAD(trans)
                    );

                    gst_query_set_caps_result(query, result);
                }
                break;
            
            default:
                ret = GST_BASE_TRANSFORM_CLASS(parent_class)->query (trans, direction, query);
        }
    }

    return ret;
}


GstFlowReturn _dlbpatmx_generate_output(GstBaseTransform *trans, GstBuffer **outbuf) {
    GstDlbPatMx *self = GST_DLBPATMX_PROC(trans);

    *outbuf = dlbpat_queue_next(trans, &self->outputQueue, GST_BASE_TRANSFORM_CLASS(parent_class)->sink_event);

    return GST_FLOW_OK;
}


// Flushes PADMX and pushes everything it still holds, with the events queued in between
static GstFlowReturn _dlbpatmx_drain(GstDlbPatMx *self) {
    GstFlowReturn ret = GST_FLOW_OK;

    if (NOT_NULL(self->padmxState)) {
        GST_DEBUG_OBJECT(self, "Draining %" G_GUINT64_FORMAT " samples", dlbpat_timeline_pending(&self->timeline));

        // An instance that never got input has nothing to give back
        if (self->instanceSamples) {
            ret = dlbpat_process(self, NULL, 0, NULL);
        }

        // A flushed PADMX accepts no more input
        dlbpat_cleanup(self);
    }

    dlbpat_timeline_finish(&self->timeline, &self->outputQueue);

    {
        GstFlowReturn pushRet = dlbpat_queue_push_all(GST_BASE_TRANSFORM(self), &self->outputQueue, GST_BASE_TRANSFORM_CLASS(parent_class)->sink_event);

        return ret != GST_FLOW_OK ? ret : pushRet;
    }
}


static guint _dlbpatmx_target_channels(dlb_padmx_target_speaker_config target) {
    switch (target) {
        case DLB_PADMX_TARGET_SPEAKER_CFG_5_1:   return 6;
        case DLB_PADMX_TARGET_SPEAKER_CFG_5_1_2: return 8;
        case DLB_PADMX_TARGET_SPEAKER_CFG_5_1_4: return 10;
        case DLB_PADMX_TARGET_SPEAKER_CFG_7_1_4: return 12;
        case DLB_PADMX_TARGET_SPEAKER_CFG_9_1_6: return 16;

        default:
            return 2;
    }
}


// in-channels=source: the PADMX configuration for a block from a `sourceChannels`-channel source.
//
// Stereo with input metadata is passed through by PADMX (C/LFE/Ls/Rs silent), so stereo for a 5.1 or 5.1.4
// target is fed as plain 2.0 (metadata dropped) and upmixed. Everything else uses the metadata: rendered
// down to the target, passed through when it matches, or upmixed from 5.1 to 5.1.4
// (DevelopmentGuideDolbyProAudioTranscoder v4.5, section 10.9.3, Table 153). Mono stays on the metadata
// path because its position in the Dolby layout depends on the producer.
//
// in-channels=source-auto: a 2.0, 5.1 or 5.1.4 source for a 5.1 or 5.1.4 target is rendered in upmix mode with
// automatic detection instead. PADMX then decides from the audio itself: stereo carried in a 5.1 header (silent
// C/LFE/surrounds) is upmixed and real 5.1 is passed through, switching on the block where the content changes.
// Detection only runs without input metadata - with metadata, Auto behaves exactly like metadata
// (DevelopmentGuideDolbyProAudioTranscoder v4.5, "Input channel configuration" and section 10.9.3), so these
// blocks are fed as plain PCM. One instance covers 2.0 and 5.1 sources, and upmix mode has a single latency for
// every input configuration, so a change between them needs no reopen and does not move the timestamps.
static void _dlbpatmx_source_config(GstDlbPatMx *self, guint sourceChannels, dlb_padmx_input_speaker_config *input, dlb_padmx_renderer_mode *mode) {
    guint target = _dlbpatmx_target_channels(self->outputChannels);

    gboolean upmixTarget = (target == 6) || (target == 10);

    if (self->sourceAuto && upmixTarget && (sourceChannels == 2 || sourceChannels == 6 || (sourceChannels == 10 && target == 10))) {
        *input = DLB_PADMX_INPUT_SPEAKER_CFG_AUTO;
        *mode  = DLB_PADMX_RENDERER_MODE_UPMIX;

    } else if (sourceChannels == 2 && upmixTarget) {
        *input = DLB_PADMX_INPUT_SPEAKER_CFG_2_0;
        *mode  = DLB_PADMX_RENDERER_MODE_UPMIX;

    } else if (sourceChannels > 2 && sourceChannels < target && target == 10) {
        *input = DLB_PADMX_INPUT_SPEAKER_CFG_METADATA;
        *mode  = DLB_PADMX_RENDERER_MODE_UPMIX;

    } else {
        *input = DLB_PADMX_INPUT_SPEAKER_CFG_METADATA;
        *mode  = DLB_PADMX_RENDERER_MODE_DOWNMIX;
    }
}


// Switches PADMX to the configuration for `sourceChannels` before the block that carries it. The old
// instance is drained first, so the switch lands exactly on the first sample of the new source.
static GstFlowReturn _dlbpatmx_follow_source(GstDlbPatMx *self, guint sourceChannels) {
    dlb_padmx_input_speaker_config input;
    dlb_padmx_renderer_mode mode;

    // Unknown (e.g. fallback silence) keeps the current configuration
    if (! self->sourceMode || sourceChannels == 0 || sourceChannels == self->sourceChannels) {
        return GST_FLOW_OK;
    }

    _dlbpatmx_source_config(self, sourceChannels, &input, &mode);

    GST_INFO_OBJECT(self, "Source channels %u -> %u: input %d -> %d, renderer %d -> %d",
        self->sourceChannels, sourceChannels, self->inputChannels, input, self->sourceRenderer, mode
    );

    self->sourceChannels = sourceChannels;

    if (input == self->inputChannels && mode == self->sourceRenderer) {
        return GST_FLOW_OK;
    }

    {
        GstFlowReturn ret = _dlbpatmx_drain(self);

        self->inputChannels  = input;
        self->sourceRenderer = mode;

        // Reopened by the caller with the new configuration
        return ret;
    }
}


// PADMX is closed after a drain (EOS, source change) or a flush and reopened with the current configuration
static GstFlowReturn _dlbpatmx_open(GstDlbPatMx *self) {
    if (NOT_NULL(self->padmxState)) {
        return GST_FLOW_OK;
    }

    if (IS_NULL(self->inputCaps) || ! dlbpat_prepare(self)) {
        dlbpat_cleanup(self);

        return GST_FLOW_NOT_NEGOTIATED;
    }

    dlbpat_timeline_start(&self->timeline, self->padmxLatencySamples);

    return GST_FLOW_OK;
}


GstFlowReturn _dlbpatmx_submit_input_buffer(GstBaseTransform *trans, gboolean is_discont, GstBuffer *inBuffer) {
    /* Let baseclass handle QoS first */
    GstFlowReturn ret = GST_BASE_TRANSFORM_CLASS(parent_class)->submit_input_buffer(trans, is_discont, inBuffer);

    if (ret == GST_FLOW_OK) {
        GstDlbPatMx *self = GST_DLBPATMX_PROC(trans);

        GstMapInfo inBufferMap;

        GST_DEBUG("call discont: %d, size: %zd, inputPcmBufferSize: %zd (%zd samples), inputPcmMode: %d, inputNeedReorder %d", 
            is_discont, gst_buffer_get_size(inBuffer), self->padmxInputSize, DLBPAT_SAMPLES_COUNT(self->padmxInputSize), self->inputRawPcm, self->inputNeedReorder
        );

        gst_buffer_map(inBuffer, &inBufferMap, GST_MAP_READ);
        {
            // Input data is a raw PCM - no metadata is available, channels order and number might be different from expected
            if (self->inputRawPcm) {
                gsize channels = GST_AUDIO_INFO_CHANNELS(&self->inputInfo);
                gsize inBpf    = GST_AUDIO_INFO_BPF(&self->inputInfo);
                gsize outBpf   = GST_AUDIO_INFO_BPF(&self->outputInfo);

                gsize samples = inBufferMap.size / inBpf;
                gsize written = 0;

                ret = _dlbpatmx_open(self);

                if (ret == GST_FLOW_OK) {
                    dlbpat_timeline_input(&self->timeline, GST_BUFFER_PTS(inBuffer), samples, channels, is_discont);

                    self->instanceSamples += samples;
                }

                while (written != samples && ret == GST_FLOW_OK) {
                    gsize samplesToWrite = samples - written;

                    if (samplesToWrite > DLBPAT_SAMPLES_COUNT(self->padmxInputSize)) {
                        samplesToWrite = DLBPAT_SAMPLES_COUNT(self->padmxInputSize);
                    }
                    
                    // Copy X-channels layout to 16 channels Dolby layout - reorder if needed
                    for (gsize i = 0; i < samplesToWrite; i++) {
                        memcpy(
                            self->inputPcmBuffer + (i * outBpf),
                            inBufferMap.data + ((written + i) * inBpf),
                            inBpf
                        );
                    }

                    if (self->inputNeedReorder) {
                        gst_audio_reorder_channels(
                            self->inputPcmBuffer, 
                            samplesToWrite * outBpf,
                            GST_AUDIO_INFO_FORMAT(&self->inputInfo),
                            channels,
                            self->channelOrderGst,
                            self->channelOrderDlb
                        );
                    }

                    ret = dlbpat_process(self, self->inputPcmBuffer, DLBPAT_SAMPLES_SIZE(samplesToWrite), NULL);

                    written += samplesToWrite;
                }

            // Input data from other Dolby component
            } else {
                gsize metaCount = dlbpat_metadata_count(inBuffer);

                if (metaCount > 0) {
                    gsize inBufferOffset = 0;

                    GST_DEBUG("PCM blocks in buffer: %" G_GSIZE_FORMAT, metaCount);

                    for (gsize blockIdx = 0; blockIdx < metaCount; blockIdx++) {
                        DlbPatMetadata *metadata = dlbpat_metadata_at(inBuffer, blockIdx);

                        gsize blockSamples = DLBPAT_SAMPLES_COUNT(metadata->blockSize);

                        // With input metadata PADMX passes stereo through (C/LFE/Ls/Rs silent) instead of upmixing it.
                        gboolean ignoreInputMd;


                        ret = _dlbpatmx_follow_source(self, metadata->sourceChannels);
                        if (ret != GST_FLOW_OK) break;

                        ret = _dlbpatmx_open(self);
                        if (ret != GST_FLOW_OK) break;

                        // Auto detection needs plain PCM too: with metadata it follows the declared layout
                        ignoreInputMd = self->inputChannels == DLB_PADMX_INPUT_SPEAKER_CFG_2_0 ||
                                        (self->sourceAuto && self->inputChannels == DLB_PADMX_INPUT_SPEAKER_CFG_AUTO);

                        dlbpat_timeline_input(
                            &self->timeline,
                            dlbpat_utils_offsetPts(GST_BUFFER_PTS(inBuffer), DLBPAT_SAMPLES_COUNT(inBufferOffset)),
                            blockSamples,
                            metadata->sourceChannels,
                            is_discont && blockIdx == 0
                        );

                        self->instanceSamples += blockSamples;

                        ret = dlbpat_process(self, inBufferMap.data + inBufferOffset, metadata->blockSize, ignoreInputMd ? NULL : metadata->md);
                        if (ret != GST_FLOW_OK) break;

                        inBufferOffset += metadata->blockSize;
                    }

                } else {
                    ret = _dlbpatmx_open(self);

                    if (ret == GST_FLOW_OK) {
                        dlbpat_timeline_input(&self->timeline, GST_BUFFER_PTS(inBuffer), DLBPAT_SAMPLES_COUNT(inBufferMap.size), 0, is_discont);

                        self->instanceSamples += DLBPAT_SAMPLES_COUNT(inBufferMap.size);

                        ret = dlbpat_process(self, inBufferMap.data, inBufferMap.size, NULL);
                    }
                }
            }
        }
        gst_buffer_unmap(inBuffer, &inBufferMap);
    }

    return ret;
}


static GstCaps *_dlbpatmx_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps, GstCaps *filter) {
    GstCaps *ret = NULL;

    if (direction == GST_PAD_SINK) {
        ret = gst_pad_get_pad_template_caps(GST_BASE_TRANSFORM_SRC_PAD(trans));

    } else {
        ret = gst_caps_copy(caps);
        
        if (filter) {
            GstCaps *tmp = gst_caps_intersect_full(filter, ret, GST_CAPS_INTERSECT_FIRST);

            gst_caps_unref(ret);

            ret = tmp;
        }
    }

    return ret;
}


static gboolean _dlbpatmx_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
    gboolean ret = TRUE;

    GstDlbPatMx *self = GST_DLBPATMX_PROC(trans);

    // A repeated caps event must not reset PADMX
    if (NOT_NULL(self->padmxState) && NOT_NULL(self->inputCaps) && gst_caps_is_equal(incaps, self->inputCaps)) {
        return TRUE;
    }

    do {
        // Buffered samples were drained by the CAPS event handler
        dlbpat_cleanup(self);
        
        // Input
        {
            GstCapsFeatures *features = gst_caps_get_features(incaps, 0);

            if (gst_caps_features_contains(features, DLBPAT_CAPS_FEATURE_METADATA)) {
                // An explicit 2.0 input keeps only the front left/right channels of the Dolby PCM layout,
                // regardless of the layout declared by the metadata (e.g. stereo carried as 5.1).
                // With in-channels=source this keeps the configuration of the current source (metadata until a block says otherwise),
                // and with in-channels=source-auto the auto detection chosen for it.
                if (
                    self->inputChannels != DLB_PADMX_INPUT_SPEAKER_CFG_2_0 &&
                    ! (self->sourceAuto && self->inputChannels == DLB_PADMX_INPUT_SPEAKER_CFG_AUTO)
                ) {
                    self->inputChannels = DLB_PADMX_INPUT_SPEAKER_CFG_METADATA;
                }


                self->inputRawPcm      = FALSE;
                self->inputNeedReorder = FALSE;

            } else if (self->inputChannels == DLBPADMX_INPUT_CHANNELS_UNKNOWN || self->sourceMode) {
                gint inputChannels;

                ret = gst_audio_info_from_caps(&self->inputInfo, incaps);
                if (! ret) {
                    GST_ERROR("Unable to map input caps to audio info!");
                    break;
                }

                inputChannels = GST_AUDIO_INFO_CHANNELS(&self->inputInfo);

                {
                    switch (inputChannels) {
                        case  2: self->inputChannels = DLB_PADMX_INPUT_SPEAKER_CFG_2_0;   break;
                        case  6: self->inputChannels = DLB_PADMX_INPUT_SPEAKER_CFG_5_1;   break;
                        case  8: self->inputChannels = DLB_PADMX_INPUT_SPEAKER_CFG_5_1_2; break;
                        case 10: self->inputChannels = DLB_PADMX_INPUT_SPEAKER_CFG_5_1_4; break;
                        case 12: self->inputChannels = DLB_PADMX_INPUT_SPEAKER_CFG_7_1_4; break;
                        case 16: self->inputChannels = DLB_PADMX_INPUT_SPEAKER_CFG_9_1_6; break;

                        default:
                            GST_ERROR("Not supported channel number! %d", inputChannels);
                            ret = FALSE;
                    }

                    if (! ret) break;
                }

                {
                    guint64 inMask;
                    guint64 dlbMask;

                    for (int i = 0; i < DLB_PAIO_PCM_NUM_CHANNELS; i++) {
                        if (i < inputChannels) {
                            self->channelOrderGst[i] = GST_AUDIO_INFO_POSITION(&self->inputInfo, i);

                        } else {
                            self->channelOrderGst[i] = GST_AUDIO_CHANNEL_POSITION_INVALID;
                        }
                    }

                    dlbpat_utils_normalizePositions(self->channelOrderGst, inputChannels);
                    dlbpat_utils_getChannelsPosition(self->channelOrderDlb, inputChannels);

                    gst_audio_channel_positions_to_mask(self->channelOrderGst, inputChannels, FALSE, &inMask);
                    gst_audio_channel_positions_to_mask(self->channelOrderDlb, inputChannels, FALSE, &dlbMask);

                    if (inMask != dlbMask) {
                        GST_ERROR("Could not find a valid channel layout - input channel positions configuration is not supported by Dolby PAT");

                        ret = FALSE;
                        break;
                    }

                    self->inputNeedReorder = memcmp(self->channelOrderGst, self->channelOrderDlb, inputChannels * sizeof(GstAudioChannelPosition)) != 0;
                }

                self->inputRawPcm = TRUE;
            }
        }

        // Output
        {
            ret = gst_audio_info_from_caps(&self->outputInfo, outcaps);
            if (! ret) {
                GST_ERROR("Unable to map output caps to audio info!");
                break;
            }
        }

        ret = dlbpat_prepare(self);
        if (! ret) break;

        dlbpat_timeline_start(&self->timeline, self->padmxLatencySamples);

        gst_caps_replace(&self->inputCaps, incaps);
    } while (0);

    if (! ret) {
        dlbpat_cleanup(self);
    }

    return ret;
}


static gboolean _dlbpatmx_sink_event(GstBaseTransform *base, GstEvent * event) {
    GstDlbPatMx *self = GST_DLBPATMX_PROC(base);

    GST_DEBUG_OBJECT(self, "%s", GST_EVENT_TYPE_NAME(event));

    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_EOS:
            _dlbpatmx_drain(self);
            break;

        case GST_EVENT_CAPS:
            {
                GstCaps *caps;

                gst_event_parse_caps(event, &caps);

                if (NOT_NULL(self->inputCaps) && ! gst_caps_is_equal(caps, self->inputCaps)) {
                    _dlbpatmx_drain(self);

                    gst_caps_replace(&self->inputCaps, NULL);
                }
            }
            break;

        case GST_EVENT_FLUSH_STOP:
            dlbpat_timeline_clear(&self->timeline);
            dlbpat_queue_clear(&self->outputQueue);

            // Reopened on the next buffer
            dlbpat_cleanup(self);
            break;

        default:
            if (GST_EVENT_IS_SERIALIZED(event) && dlbpat_queue_hold_event(&self->timeline, &self->outputQueue, event)) {
                return TRUE;
            }
            break;
    }

    return GST_BASE_TRANSFORM_CLASS(parent_class)->sink_event(base, event);
}


static void _dlbpatmx_finalize(GObject *object) {
    GstDlbPatMx *self = GST_DLBPATMX_PROC(object);

    GST_DEBUG_OBJECT(self, "CALL");

    dlbpat_timeline_clear(&self->timeline);
    dlbpat_queue_clear(&self->outputQueue);

    gst_caps_replace(&self->inputCaps, NULL);

    G_OBJECT_CLASS(parent_class)->finalize (object);
}


static void _dlbpatmx_set_property(GObject *object, guint propId, const GValue *value, GParamSpec *paramSpec) {
    GstDlbPatMx *self = GST_DLBPATMX_PROC(object);

    switch (propId) {
        case PROPERTY_OUT_CHANNELS:
            self->outputChannels = g_value_get_enum(value);
            break;

        case PROPERTY_IN_CHANNELS:
            {
                gint inputChannels = g_value_get_enum(value);

                self->sourceAuto = inputChannels == DLBPADMX_INPUT_CHANNELS_SOURCE_AUTO;
                self->sourceMode = inputChannels == DLBPADMX_INPUT_CHANNELS_SOURCE || self->sourceAuto;

                if (self->sourceMode) {
                    self->inputChannels  = DLB_PADMX_INPUT_SPEAKER_CFG_METADATA;
                    self->sourceChannels = 0;
                    self->sourceRenderer = DLB_PADMX_RENDERER_MODE_DOWNMIX;

                } else {
                    self->inputChannels = inputChannels;
                }
            }
            break;

        case PROPERTY_MODE:
            self->downmixMode = g_value_get_enum(value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propId, paramSpec);
            break;
    }
}


static void _dlbpatmx_get_property(GObject *object, guint propId, GValue *value, GParamSpec *paramSpec) {
    GstDlbPatMx *self = GST_DLBPATMX_PROC(object);

    switch (propId) {
        case PROPERTY_OUT_CHANNELS:
            g_value_set_enum(value, self->outputChannels);
            break;

        case PROPERTY_IN_CHANNELS:
            g_value_set_enum(value,
                self->sourceAuto ? DLBPADMX_INPUT_CHANNELS_SOURCE_AUTO :
                self->sourceMode ? DLBPADMX_INPUT_CHANNELS_SOURCE      : (gint) self->inputChannels
            );
            break;

        case PROPERTY_MODE:
            g_value_set_enum(value, self->downmixMode);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propId, paramSpec);
            break;
    }
}


static void gst_dlbpatmx_init(GstDlbPatMx *filter) {
    GST_DEBUG_OBJECT(filter, "CALL");

    g_queue_init(&filter->outputQueue);

    dlbpat_timeline_init(&filter->timeline, DLBPAT_DEFAULT_SAMPLE_RATE);
    filter->outputChannels = DLB_PADMX_TARGET_SPEAKER_CFG_DEFAULT;
    
    gst_audio_info_init(&filter->outputInfo);

    filter->inputChannels    = DLBPADMX_INPUT_CHANNELS_UNKNOWN;
    filter->inputRawPcm      = FALSE;
    filter->inputPcmBuffer   = NULL;
    filter->inputNeedReorder = FALSE;
    filter->inputCaps        = NULL;

    filter->sourceMode      = FALSE;
    filter->sourceAuto      = FALSE;
    filter->sourceChannels  = 0;
    filter->sourceRenderer  = DLB_PADMX_RENDERER_MODE_DOWNMIX;
    filter->instanceSamples = 0;

    gst_audio_info_init(&filter->inputInfo);
}


static void gst_dlbpatmx_class_init (GstDlbPatMxClass * klass) {
    GObjectClass          *gobjectClass = G_OBJECT_CLASS (klass);
    GstElementClass       *elementClass = GST_ELEMENT_CLASS (klass);
    GstBaseTransformClass *baseClass    = GST_BASE_TRANSFORM_CLASS (klass);

    dlbpat_common_initialize();

    gst_element_class_add_static_pad_template(elementClass, &_srcTemplate);
    gst_element_class_add_static_pad_template(elementClass, &_sinkTemplate);

    {
        gobjectClass->finalize     = GST_DEBUG_FUNCPTR(_dlbpatmx_finalize);
        gobjectClass->get_property = GST_DEBUG_FUNCPTR(_dlbpatmx_get_property);
        gobjectClass->set_property = GST_DEBUG_FUNCPTR(_dlbpatmx_set_property);

        baseClass->passthrough_on_same_caps = FALSE;

        baseClass->start               = GST_DEBUG_FUNCPTR(_dlbpatmx_start);
        baseClass->stop                = GST_DEBUG_FUNCPTR(_dlbpatmx_stop);
        baseClass->query               = GST_DEBUG_FUNCPTR(_dlbpatmx_query);
        baseClass->generate_output     = GST_DEBUG_FUNCPTR(_dlbpatmx_generate_output);
        baseClass->submit_input_buffer = GST_DEBUG_FUNCPTR(_dlbpatmx_submit_input_buffer);
        baseClass->transform_caps      = GST_DEBUG_FUNCPTR(_dlbpatmx_transform_caps);
        baseClass->set_caps            = GST_DEBUG_FUNCPTR(_dlbpatmx_set_caps);
        baseClass->sink_event          = GST_DEBUG_FUNCPTR(_dlbpatmx_sink_event);
    }

    gst_element_class_set_static_metadata(
        elementClass,
        "Dolby PAT audio renderer", 
        "Audio",
        "Processes incoming PCM by either upmixing, or downmixing channel-based audio", 
        GST_AUTHORS
    );

    // Properties
    {
        GParamSpec *properties[PROPERTY_LAST] = { NULL, };

        properties[PROPERTY_OUT_CHANNELS] = g_param_spec_enum(
            "out-channels",
            "Output channels count",
            "Number of expected output channels", 
            dlbpadmx_outputChannels_getType(), 
            DLB_PADMX_TARGET_SPEAKER_CFG_DEFAULT,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        properties[PROPERTY_IN_CHANNELS] = g_param_spec_enum(
            "in-channels",
            "Input channels count",
            "Number of expected input channels", 
            dlbpadmx_inputChannels_getType(),
            DLBPADMX_INPUT_CHANNELS_UNKNOWN,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        properties[PROPERTY_MODE] = g_param_spec_enum(
            "mode",
            "Force downmix or upmix opreation mode",
            "Enable to force downmix or upmix mode",
            dlbpadmx_mode_getType(),
            DLB_PADMX_MODE_AUTO,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        g_object_class_install_properties(
            gobjectClass, PROPERTY_LAST, properties
        );
    }
}
