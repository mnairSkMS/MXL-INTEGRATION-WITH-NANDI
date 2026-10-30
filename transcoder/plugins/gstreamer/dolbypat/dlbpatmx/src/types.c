#include "types.h"
#include "common/dolby.h"

GType dlbpadmx_outputChannels_getType() {
    static GType type = 0;

    static const GEnumValue values[] = {
        { DLB_PADMX_TARGET_SPEAKER_CFG_2_0_LTRT, "Stereo output with LtRt downmix",                                "2_ltrt" },
        { DLB_PADMX_TARGET_SPEAKER_CFG_2_0_LORO, "Stereo output with LoRo downmix",                                "2_loro" },
        { DLB_PADMX_TARGET_SPEAKER_CFG_2_0_AUTO, "Stereo speaker output with automatically detected downmix type", "2_auto" },
        { DLB_PADMX_TARGET_SPEAKER_CFG_5_1,      "5.1 speaker output",                                             "6"      },
        { DLB_PADMX_TARGET_SPEAKER_CFG_5_1_2,    "5.1.2 speaker output",                                           "8"      },
        { DLB_PADMX_TARGET_SPEAKER_CFG_5_1_4,    "5.1.4 speaker output",                                           "10"     },
        { DLB_PADMX_TARGET_SPEAKER_CFG_7_1_4,    "7.1.4 speaker output",                                           "12"     },
        { DLB_PADMX_TARGET_SPEAKER_CFG_9_1_6,    "9.1.6 speaker output",                                           "16"     },
        { 0, NULL, NULL}
    };

    if (! type) {
        type = g_enum_register_static("GstDlbPadmxOutputChannelsType", values);
    }
    
    return type;
}


GType dlbpadmx_inputChannels_getType() {
    static GType type = 0;

    static const GEnumValue values[] = {
        { DLBPADMX_INPUT_CHANNELS_UNKNOWN,      "Input channels is unknown - will be determined from src caps", "unknown"},
        { DLB_PADMX_INPUT_SPEAKER_CFG_2_0,      "2.0 input speaker configuration",                              "2"      },
        { DLB_PADMX_INPUT_SPEAKER_CFG_5_1,      "5.1 input speaker configuration",                              "6"      },
        { DLB_PADMX_INPUT_SPEAKER_CFG_5_1_2,    "5.1.2 input speaker configuration",                            "8"      },
        { DLB_PADMX_INPUT_SPEAKER_CFG_5_1_4,    "5.1.4 input speaker configuration",                            "10"     },
        { DLB_PADMX_INPUT_SPEAKER_CFG_7_1_4,    "7.1.4 input speaker configuration",                            "12"     },
        { DLB_PADMX_INPUT_SPEAKER_CFG_9_1_6,    "9.1.6 input speaker configuration",                            "16"     },
        { DLB_PADMX_INPUT_SPEAKER_CFG_METADATA, "MIF to determine input speaker configuration",                 "meta"   },
        { DLB_PADMX_INPUT_SPEAKER_CFG_AUTO,     "channel mode auto detection (2.0, 5.1, 5.1.4)",                "auto"   },
        { DLBPADMX_INPUT_CHANNELS_SOURCE,       "Follow the source channel count of each block (stereo is upmixed, surround is rendered)", "source" },
        { DLBPADMX_INPUT_CHANNELS_SOURCE_AUTO,  "As source, but 2.0/5.1/5.1.4 sources are analysed by PADMX auto detection (stereo carried as 5.1 is upmixed)", "source-auto" },
        { 0, NULL, NULL}
    };

    if (! type) {
        type = g_enum_register_static("GstDlbPadmxInputChannelsType", values);
    }
    
    return type;
}


GType dlbpadmx_mode_getType() {
    static GType type = 0;

    static const GEnumValue values[] = {
        { DLB_PADMX_MODE_AUTO,    "Mode will be determined autoatically", "auto"    },
        { DLB_PADMX_MODE_DOWNMIX, "Mode will be determined autoatically", "downmix" },
        { DLB_PADMX_MODE_UPMIX,   "Mode will be determined autoatically", "upmix"   },
        { 0, NULL, NULL }
    };

    if (! type) {
        type = g_enum_register_static("GstDlbPadmxMode", values);
    }
    
    return type;
}