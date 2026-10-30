#include <gst/audio/audio-format.h>

#include "common.h"
#include "common/plugin.h"
#include "common/utils.h"
#include "common/metadata.h"
#include "common/queue.h"

#include "dlbpatloud.h"


GST_DEBUG_CATEGORY (gst_dlbpatloud_debug);
#define GST_CAT_DEFAULT gst_dlbpatloud_debug

#define gst_dlbpatloud_parent_class parent_class
G_DEFINE_TYPE (GstDlbPatLoud, gst_dlbpatloud, GST_TYPE_BASE_TRANSFORM);

GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (
    dlbpatloud, 
    "dlbpatloud",
    GST_RANK_PRIMARY, 
    GST_DLBPATLOUD_TYPE,
    GST_DEBUG_CATEGORY_INIT (
        gst_dlbpatloud_debug, "dlbpatloud", 0, "Dolby Pro volume leveler element"
    );
);


#define DEFAULT_REG_TYPE   DLB_PALOUD_REG_TYPE_DEFAULT
#define DEFAULT_LEVEL_MODE DLB_PALOUD_LEVEL_MODE_AUTO
#define DEFAULT_DIALNORM   DLB_PALOUD_DIALNORM_DEFAULT
#define DEFAULT_DIALINT    DLB_PALOUD_DIALOGUE_INTELLIGENCE_DEFAULT


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
        "rate = (int) " G_STRINGIFY(DLBPAT_DEFAULT_SAMPLE_RATE) ", "
        "channels = (int) 1;"

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


static void dlbpat_cleanup(GstDlbPatLoud *self) {
    GST_TRACE_OBJECT(self, "CALL");

    if (NOT_NULL(self->paloudState)) {
        DLBPAT_CALL_CHECK(dlb_paloud_close, (self->paloudState));

        self->paloudState = NULL;
    }

    if (NOT_NULL(self->paloudMd)) {
        DLBPAT_CALL_CHECK(dlb_pamif_md_segment_destroy, (&self->paloudMd));

        self->paloudMd = NULL;
    }

    if (NOT_NULL(self->inputPcmBuffer)) {
        free(self->inputPcmBuffer);

        self->inputPcmBuffer = NULL;
    }

    self->paloudInputSize      = 0;
    self->paloudOutputSize     = 0;
    self->paloudLatencySamples = 0;
}


static gboolean dlbpat_prepare(GstDlbPatLoud *self) {
    int dlbRet = DLBPAT_SUCCESS;

    do {
        dlb_paloud_requirements requirements;

        {
            dlb_paloud_init_config config;

            DLBPAT_CALL_BREAK(dlbRet, dlb_paloud_setup, (&config));

            config.channel_cfg = self->inputChannels;

            DLBPAT_CALL_BREAK(dlbRet, dlb_paloud_query, (&config, &requirements));
            DLBPAT_CALL_BREAK(dlbRet, dlb_paloud_open,  (&self->paloudState, &config));
        }

        // Set rutime configuration
        {
            dlb_paloud_runtime_config runtimeConfig;

            DLBPAT_CALL_BREAK(dlbRet, dlb_paloud_runtime_defaults_get, (self->paloudState, &runtimeConfig));

            runtimeConfig.mode = self->levelMode;
            runtimeConfig.type = self->regulationType;
            
            runtimeConfig.config.dialnorm              = self->dialnorm;
            runtimeConfig.config.dialogue_intelligence = self->dialInt;

            DLBPAT_CALL_BREAK(dlbRet, dlb_paloud_runtime_config_set, (self->paloudState, &runtimeConfig));
        }

        DLBPAT_CALL_BREAK(dlbRet, dlb_pamif_md_segment_create, (&self->paloudMd));

        self->paloudInputSize      = DLBPAT_SAMPLES_SIZE(requirements.max_input_samples);
        self->paloudOutputSize     = DLBPAT_SAMPLES_SIZE(requirements.max_output_samples);
        self->paloudLatencySamples = requirements.latency;

        if (self->inputRawPcm) {
            self->inputPcmBuffer = (guint8 *) malloc(self->paloudInputSize);
        }
    } while (0);

    return dlbRet == DLBPAT_SUCCESS;
}


static GstFlowReturn dlbpat_process(GstDlbPatLoud *self, uint8_t *buffer, gsize bufferSize, dlb_pamif_md_segment *md) {
    int dlbRet = DLBPAT_SUCCESS;

    gsize processedSize = 0;
    int goFurther;
    unsigned int samplesAvailable;

    GST_DEBUG("Call for %" G_GSIZE_FORMAT " bytes", bufferSize);

    do {
        gsize toProcess = MIN(bufferSize - processedSize, self->paloudInputSize);

        goFurther = 1;

        if (toProcess) {
            dlb_paio_pcm_block block;

            dlbpat_utils_paioPcmBlockInit(&block);

            block.num_samples  = DLBPAT_SAMPLES_COUNT(toProcess);
            block.p_pcm        = (int32_t *)(buffer + processedSize);
            block.p_md         = md;

            GST_DEBUG("toProcess: %" G_GSIZE_FORMAT ", %u", toProcess, block.num_samples);

            DLBPAT_CALL_BREAK(dlbRet, dlb_paloud_add_samples, (self->paloudState, &block, &goFurther, &samplesAvailable));
        }

        if (goFurther) {
            dlb_paio_pcm_block output;

            unsigned int cont = 0;
            do {
                if (toProcess) {
                    DLBPAT_CALL_BREAK(dlbRet, dlb_paloud_process, (self->paloudState, &cont, &samplesAvailable));

                } else {
                    DLBPAT_CALL_BREAK(dlbRet, dlb_paloud_flush, (self->paloudState, &cont, &samplesAvailable));

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
                        output.p_md        = self->paloudMd;
                        output.num_samples = 0;

                        dlbRet = dlb_paloud_get_samples(self->paloudState, toRead, &output, &samplesAvailable);
                    }
                    gst_buffer_unmap(outBuffer, &outBufferMap);

                    if (dlbRet != DLBPAT_SUCCESS) {
                        GST_ERROR_OBJECT(self, "dlb_paloud_get_samples(%u) has failed with code: %d!", toRead, dlbRet);
                    }

                    if (! dlbpat_queue_output(&self->timeline, &self->outputQueue, outBuffer, self->paloudMd, output.num_samples)) {
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


static gboolean _dlbpatloud_start(GstBaseTransform *trans) {
    GstDlbPatLoud *self = GST_DLBPATLOUD_PROC(trans);

    GST_LOG_OBJECT(self, "call");

    return TRUE;
}


static gboolean _dlbpatloud_stop(GstBaseTransform *trans) {
    GstDlbPatLoud *self = GST_DLBPATLOUD_PROC(trans);

    GST_LOG_OBJECT(self, "call");

    dlbpat_cleanup(self);

    dlbpat_timeline_clear(&self->timeline);
    dlbpat_queue_clear(&self->outputQueue);

    gst_caps_replace(&self->inputCaps, NULL);

    return TRUE;
}


static gboolean _dlbpatloud_query(GstBaseTransform *trans, GstPadDirection direction, GstQuery * query) {
    gboolean ret = TRUE;

    {
        GstDlbPatLoud *self = GST_DLBPATLOUD_PROC(trans);

        switch (GST_QUERY_TYPE (query)) {
            case GST_QUERY_LATENCY:
                if ((gst_pad_peer_query (GST_BASE_TRANSFORM_SINK_PAD (trans), query))) {
                    guint64 latency = gst_util_uint64_scale_round(self->paloudLatencySamples, GST_SECOND, DLBPAT_DEFAULT_SAMPLE_RATE);

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


GstFlowReturn _dlbpatloud_generate_output(GstBaseTransform *trans, GstBuffer **outbuf) {
    GstDlbPatLoud *self = GST_DLBPATLOUD_PROC(trans);

    *outbuf = dlbpat_queue_next(trans, &self->outputQueue, GST_BASE_TRANSFORM_CLASS(parent_class)->sink_event);

    return GST_FLOW_OK;
}


GstFlowReturn _dlbpatloud_submit_input_buffer(GstBaseTransform *trans, gboolean is_discont, GstBuffer *inBuffer) {
    GstFlowReturn ret = GST_BASE_TRANSFORM_CLASS(parent_class)->submit_input_buffer (trans, is_discont, inBuffer);

    GST_DEBUG("call discont: %d, size: %zd", is_discont, gst_buffer_get_size(inBuffer));

    if (ret == GST_FLOW_OK) {
        GstDlbPatLoud *self = GST_DLBPATLOUD_PROC(trans);

        GstMapInfo inBufferMap;

        // PALOUD is closed after a drain (EOS) or a flush and reopened with the same configuration
        if (IS_NULL(self->paloudState)) {
            if (IS_NULL(self->inputCaps) || ! dlbpat_prepare(self)) {
                dlbpat_cleanup(self);

                return GST_FLOW_NOT_NEGOTIATED;
            }

            dlbpat_timeline_start(&self->timeline, self->paloudLatencySamples);
        }

        GST_DEBUG("call discont: %d, size: %zd, inputPcmBufferSize: %zd (%zd samples), inputPcmMode: %d, inputNeedReorder %d", 
            is_discont, gst_buffer_get_size(inBuffer), self->paloudInputSize, DLBPAT_SAMPLES_COUNT(self->paloudInputSize), self->inputRawPcm, self->inputNeedReorder
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

                dlbpat_timeline_input(&self->timeline, GST_BUFFER_PTS(inBuffer), samples, self->sourceChannels, is_discont);

                while (written != samples && ret == GST_FLOW_OK) {
                    gsize samplesToWrite = samples - written;

                    if (samplesToWrite > DLBPAT_SAMPLES_COUNT(self->paloudInputSize)) {
                        samplesToWrite = DLBPAT_SAMPLES_COUNT(self->paloudInputSize);
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

                        dlbpat_timeline_input(
                            &self->timeline,
                            dlbpat_utils_offsetPts(GST_BUFFER_PTS(inBuffer), DLBPAT_SAMPLES_COUNT(inBufferOffset)),
                            DLBPAT_SAMPLES_COUNT(metadata->blockSize),
                            self->sourceChannelsProp < 0 ? metadata->sourceChannels : (guint) self->sourceChannelsProp,
                            is_discont && blockIdx == 0
                        );

                        ret = dlbpat_process(self, inBufferMap.data + inBufferOffset, metadata->blockSize, metadata->md);
                        if (ret != GST_FLOW_OK) break;

                        inBufferOffset += metadata->blockSize;
                    }

                } else {
                    dlbpat_timeline_input(&self->timeline, GST_BUFFER_PTS(inBuffer), DLBPAT_SAMPLES_COUNT(inBufferMap.size), MAX(self->sourceChannelsProp, 0), is_discont);

                    ret = dlbpat_process(self, inBufferMap.data, inBufferMap.size, NULL);
                }
            }
        }
        gst_buffer_unmap(inBuffer, &inBufferMap);
    }

    return ret;
}


static GstCaps *_dlbpatloud_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps, GstCaps *filter) {
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


static gboolean _dlbpatloud_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
    gboolean ret = TRUE;

    GstDlbPatLoud *self = GST_DLBPATLOUD_PROC(trans);

    // A repeated caps event must not reset PALOUD (and its loudness state)
    if (NOT_NULL(self->paloudState) && NOT_NULL(self->inputCaps) && gst_caps_is_equal(incaps, self->inputCaps)) {
        return TRUE;
    }

    do {
        // Buffered samples were drained by the CAPS event handler
        dlbpat_cleanup(self);

        // Input
        {
            GstCapsFeatures *features = gst_caps_get_features(incaps, 0);

            if (gst_caps_features_contains(features, DLBPAT_CAPS_FEATURE_METADATA)) {
                self->inputRawPcm      = FALSE;
                self->inputNeedReorder = FALSE;

            } else {
                gint inputChannels;

                ret = gst_audio_info_from_caps(&self->inputInfo, incaps);
                if (! ret) {
                    GST_ERROR("Unable to map input caps to audio info!");
                    break;
                }

                inputChannels = GST_AUDIO_INFO_CHANNELS(&self->inputInfo);

                {
                    switch (inputChannels) {
                        case  1: self->inputChannels = DLB_PALOUD_CHANNEL_CONFIG_10;  break;
                        case  2: self->inputChannels = DLB_PALOUD_CHANNEL_CONFIG_20;  break;
                        case  6: self->inputChannels = DLB_PALOUD_CHANNEL_CONFIG_51;  break;
                        case 10: self->inputChannels = DLB_PALOUD_CHANNEL_CONFIG_514; break;

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

                self->inputRawPcm    = TRUE;
                self->sourceChannels = self->sourceChannelsProp < 0 ? (guint) inputChannels : (guint) self->sourceChannelsProp;

                GST_INFO_OBJECT(self, "Raw input with %d channels, source channels %u", inputChannels, self->sourceChannels);
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

        dlbpat_timeline_start(&self->timeline, self->paloudLatencySamples);

        gst_caps_replace(&self->inputCaps, incaps);
    } while (0);

    if (! ret) {
        dlbpat_cleanup(self);
    }

    return ret;
}


// Flushes PALOUD and pushes everything it still holds, with the events queued in between
static GstFlowReturn _dlbpatloud_drain(GstDlbPatLoud *self) {
    GstFlowReturn ret = GST_FLOW_OK;

    if (NOT_NULL(self->paloudState)) {
        GST_DEBUG_OBJECT(self, "Draining %" G_GUINT64_FORMAT " samples", dlbpat_timeline_pending(&self->timeline));

        ret = dlbpat_process(self, NULL, 0, NULL);

        // A flushed PALOUD accepts no more input
        dlbpat_cleanup(self);
    }

    dlbpat_timeline_finish(&self->timeline, &self->outputQueue);

    {
        GstFlowReturn pushRet = dlbpat_queue_push_all(GST_BASE_TRANSFORM(self), &self->outputQueue, GST_BASE_TRANSFORM_CLASS(parent_class)->sink_event);

        return ret != GST_FLOW_OK ? ret : pushRet;
    }
}


static gboolean _dlbpatloud_sink_event(GstBaseTransform *base, GstEvent * event) {
    GstDlbPatLoud *self = GST_DLBPATLOUD_PROC(base);

    GST_DEBUG_OBJECT(self, "%s", GST_EVENT_TYPE_NAME(event));

    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_EOS:
            _dlbpatloud_drain(self);
            break;

        case GST_EVENT_CAPS:
            {
                GstCaps *caps;

                gst_event_parse_caps(event, &caps);

                // A new layout (e.g. the next playlist item) reopens PALOUD: finish the old one first
                if (NOT_NULL(self->inputCaps) && ! gst_caps_is_equal(caps, self->inputCaps)) {
                    _dlbpatloud_drain(self);

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


static void _dlbpatloud_finalize(GObject *object) {
    GstDlbPatLoud *self = GST_DLBPATLOUD_PROC(object);

    GST_DEBUG_OBJECT(self, "CALL");

    dlbpat_timeline_clear(&self->timeline);
    dlbpat_queue_clear(&self->outputQueue);

    gst_caps_replace(&self->inputCaps, NULL);

    G_OBJECT_CLASS(parent_class)->finalize (object);
}


static void _dlbpatloud_set_property(GObject *object, guint propId, const GValue *value, GParamSpec *paramSpec) {
    GstDlbPatLoud *self = GST_DLBPATLOUD_PROC(object);

    switch (propId) {
        case PROPERTY_REG_TYPE:
            self->regulationType = g_value_get_enum(value);
            break;

        case PROPERTY_CHANNELS:
            self->inputChannels = g_value_get_enum(value);
            break;

        case PROPERTY_MODE:
            self->levelMode = g_value_get_enum(value);
            break;

        case PROPERTY_DIALNORM:
            self->dialnorm = g_value_get_int(value);
            break;

        case PROPERTY_DIALINT:
            self->dialInt = g_value_get_enum(value);
            break;

        case PROPERTY_SOURCE_CHANNELS:
            self->sourceChannelsProp = g_value_get_int(value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propId, paramSpec);
            break;
    }
}


static void _dlbpatloud_get_property(GObject *object, guint propId, GValue *value, GParamSpec *paramSpec) {
    GstDlbPatLoud *self = GST_DLBPATLOUD_PROC(object);

    switch (propId) {
        case PROPERTY_REG_TYPE:
            g_value_set_enum(value, self->regulationType);
            break;

        case PROPERTY_CHANNELS:
            g_value_set_enum(value, self->inputChannels);
            break;

        case PROPERTY_MODE:
            g_value_set_enum(value, self->levelMode);
            break;

        case PROPERTY_DIALNORM:
            g_value_set_int(value, self->dialnorm);
            break;

        case PROPERTY_DIALINT:
            g_value_set_enum(value, self->dialInt);
            break;

        case PROPERTY_SOURCE_CHANNELS:
            g_value_set_int(value, self->sourceChannelsProp);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propId, paramSpec);
            break;
    }
}


static void gst_dlbpatloud_init(GstDlbPatLoud *filter) {
    GST_DEBUG_OBJECT(filter, "CALL");

    g_queue_init(&filter->outputQueue);

    dlbpat_timeline_init(&filter->timeline, DLBPAT_DEFAULT_SAMPLE_RATE);

    filter->inputCaps          = NULL;
    filter->sourceChannelsProp = -1;
    filter->sourceChannels     = 0;

    gst_audio_info_init(&filter->outputInfo);

    filter->inputRawPcm      = FALSE;
    filter->inputPcmBuffer   = NULL;
    filter->inputNeedReorder = FALSE;
    filter->inputChannels    = DLB_PALOUD_CHANNEL_CONFIG_DEFAULT;
    
    filter->levelMode        = DEFAULT_LEVEL_MODE;
    filter->regulationType   = DEFAULT_REG_TYPE;
    filter->dialnorm         = DEFAULT_DIALNORM;
    filter->dialInt          = DEFAULT_DIALINT;

    gst_audio_info_init(&filter->inputInfo);
}


static void gst_dlbpatloud_class_init (GstDlbPatLoudClass * klass) {
    GObjectClass          *gobjectClass = G_OBJECT_CLASS (klass);
    GstElementClass       *elementClass = GST_ELEMENT_CLASS (klass);
    GstBaseTransformClass *baseClass    = GST_BASE_TRANSFORM_CLASS (klass);

    dlbpat_common_initialize();

    gst_element_class_add_static_pad_template(elementClass, &_srcTemplate);
    gst_element_class_add_static_pad_template(elementClass, &_sinkTemplate);

    {
        gobjectClass->finalize     = GST_DEBUG_FUNCPTR(_dlbpatloud_finalize);
        gobjectClass->get_property = GST_DEBUG_FUNCPTR(_dlbpatloud_get_property);
        gobjectClass->set_property = GST_DEBUG_FUNCPTR(_dlbpatloud_set_property);

        baseClass->passthrough_on_same_caps = FALSE;

        baseClass->start               = GST_DEBUG_FUNCPTR(_dlbpatloud_start);
        baseClass->stop                = GST_DEBUG_FUNCPTR(_dlbpatloud_stop);
        baseClass->query               = GST_DEBUG_FUNCPTR(_dlbpatloud_query);
        baseClass->generate_output     = GST_DEBUG_FUNCPTR(_dlbpatloud_generate_output);
        baseClass->submit_input_buffer = GST_DEBUG_FUNCPTR(_dlbpatloud_submit_input_buffer);
        baseClass->transform_caps      = GST_DEBUG_FUNCPTR(_dlbpatloud_transform_caps);
        baseClass->set_caps            = GST_DEBUG_FUNCPTR(_dlbpatloud_set_caps);
        baseClass->sink_event          = GST_DEBUG_FUNCPTR(_dlbpatloud_sink_event);
    }

    gst_element_class_set_static_metadata(
        elementClass,
        "Dolby PAT audio loudness leveler", 
        "Audio",
        "Performs loudness processing and authors loudness metadata",
        GST_AUTHORS
    );

    // Properties
    {
        GParamSpec *properties[PROPERTY_LAST] = { NULL, };

        properties[PROPERTY_REG_TYPE] = g_param_spec_enum(
            "loudreg",
            "PALAUD regulation type",
            "Loudness regulation type (preset)", 
            dlbpatloud_reg_type_getType(), 
            DEFAULT_REG_TYPE,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        properties[PROPERTY_CHANNELS] = g_param_spec_enum(
            "channels",
            "Channel configuration",
            "Channel configuration",
            dlbpatloud_channels_getType(),
            DLB_PALOUD_CHANNEL_CONFIG_DEFAULT,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        properties[PROPERTY_MODE] = g_param_spec_enum(
            "mode",
            "Loudness regulation type",
            "Loudness regulation type",
            dlbpatloud_mode_getType(),
            DEFAULT_LEVEL_MODE,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        properties[PROPERTY_DIALNORM] = g_param_spec_int(
            "dialnorm",
            "Dialogue normalization range",
            "Dialogue normalization range",
            DLB_PALOUD_DIALNORM_MIN,
            DLB_PALOUD_DIALNORM_MAX,
            DEFAULT_DIALNORM,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        properties[PROPERTY_DIALINT] = g_param_spec_enum(
            "dialint",
            "The dialogue intelligence parameters when the loudness leveler runs using 'manual' regulation type",
            "The dialogue intelligence parameters when the loudness leveler runs using 'manual' regulation type",
            dlbpatloud_dialint_getType(),
            DEFAULT_DIALINT,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        properties[PROPERTY_SOURCE_CHANNELS] = g_param_spec_int(
            "source-channels",
            "Source channel count",
            "Channel count of the source stamped on the output blocks for dlbpatmx in-channels=source: "
            "-1 = from the input caps (raw PCM) or the input blocks (Dolby PCM), 0 = unknown (keeps the mixer as it is)",
            -1,
            DLB_PAIO_PCM_NUM_CHANNELS,
            -1,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
        );

        g_object_class_install_properties(
            gobjectClass, PROPERTY_LAST, properties
        );
    }
}
