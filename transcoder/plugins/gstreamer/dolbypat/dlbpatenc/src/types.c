#include "common/dolby.h"

#include "types.h"


GType dlbpatenc_datarate_getType() {
    static GType type = 0;

    static const GEnumValue values[] = {
        { GST_DLBPAT_ENC_DATARATE_32K,   "Bitrate 32 kbps (DD/P)",       "32k"   },
        { GST_DLBPAT_ENC_DATARATE_48K,   "Bitrate 48 kbps (AC4)",        "48k"   },
        { GST_DLBPAT_ENC_DATARATE_56K,   "Bitrate 56 kbps (DD/P)",       "56k"   },
        { GST_DLBPAT_ENC_DATARATE_64K,   "Bitrate 64 kbps (DD/P, AC4)",  "64k"   },
        { GST_DLBPAT_ENC_DATARATE_96K,   "Bitrate 96 kbps (DD/P, AC4)",  "96k"   },
        { GST_DLBPAT_ENC_DATARATE_128K,  "Bitrate 128 kbps (DD/P, AC4)", "128k"  },
        { GST_DLBPAT_ENC_DATARATE_144K,  "Bitrate 144 kbps (AC4)",       "144k"  },
        { GST_DLBPAT_ENC_DATARATE_192K,  "Bitrate 192 kbps (DD/P, AC4)", "192k"  },
        { GST_DLBPAT_ENC_DATARATE_256K,  "Bitrate 256 kbps (DD/P, AC4)", "256k"  },
        { GST_DLBPAT_ENC_DATARATE_288K,  "Bitrate 288 kbps (AC4)",       "288k"  },
        { GST_DLBPAT_ENC_DATARATE_320K,  "Bitrate 320 kbps (AC4)",       "320k"  },
        { GST_DLBPAT_ENC_DATARATE_384K,  "Bitrate 384 kbps (DD/P, AC4)", "384k"  },
        { GST_DLBPAT_ENC_DATARATE_448K,  "Bitrate 448 kbps (DD/P, AC4)", "448k"  },
        { GST_DLBPAT_ENC_DATARATE_512K,  "Bitrate 512 kbps (AC4)",       "512k"  },
        { GST_DLBPAT_ENC_DATARATE_640K,  "Bitrate 640 kbps (DDP)",       "640k"  },
        { GST_DLBPAT_ENC_DATARATE_768K,  "Bitrate 768 kbps (DD/P, AC4)", "768k"  },
        { GST_DLBPAT_ENC_DATARATE_1024K, "Bitrate 1024 kbps (AC4)",      "1024k" },
        { GST_DLBPAT_ENC_DATARATE_AUTO,  "Default bitrate value",        "auto"  },

        { 0, NULL, NULL}
    };

    if (! type) {
        type = g_enum_register_static("GstDlbPatEncDataRate", values);
    }
    
    return type;
}

GType dlbpatenc_drcline_getType() {
    static GType type = 0;

    static const GEnumValue values[] = {
        { DLB_PAEC_DRC_PROFILE_AUTO,           "Auto mode",      "auto"           },
        { DLB_PAEC_DRC_PROFILE_NONE,           "None",           "none"           },
        { DLB_PAEC_DRC_PROFILE_FILM_STANDARD,  "Film standard",  "film_standard"  },  
        { DLB_PAEC_DRC_PROFILE_FILM_LIGHT,     "Film light",     "film_light"     },
        { DLB_PAEC_DRC_PROFILE_MUSIC_STANDARD, "Music standard", "music_standard" },    
        { DLB_PAEC_DRC_PROFILE_MUSIC_LIGHT,    "Music light",    "music_light"    },
        { DLB_PAEC_DRC_PROFILE_SPEECH,         "Speech",         "speech"         },
        { 0, NULL, NULL }
    };

    if (! type) {
        type = g_enum_register_static("GstDlbPatEncDrcline", values);
    }
    
    return type;
}

GType dlbpatenc_drcrf_getType() {
    static GType type = 0;

    static const GEnumValue values[] = {
        { DLB_PAEC_DRC_PROFILE_AUTO,           "Auto mode",      "auto"           },
        { DLB_PAEC_DRC_PROFILE_NONE,           "None",           "none"           },
        { DLB_PAEC_DRC_PROFILE_FILM_STANDARD,  "Film standard",  "film_standard"  },  
        { DLB_PAEC_DRC_PROFILE_FILM_LIGHT,     "Film light",     "film_light"     },
        { DLB_PAEC_DRC_PROFILE_MUSIC_STANDARD, "Music standard", "music_standard" },    
        { DLB_PAEC_DRC_PROFILE_MUSIC_LIGHT,    "Music light",    "music_light"    },
        { DLB_PAEC_DRC_PROFILE_SPEECH,         "Speech",         "speech"         },
        { 0, NULL, NULL }
    };

    if (! type) {
        type = g_enum_register_static("GstDlbPatEncDrcrf", values);
    }
    
    return type;
}

GType dlbpatenc_surround_trim_getType() {
    static GType type = 0;

    static const GEnumValue values[] = {
        { DLB_PAEC_DDP_SURROUND_TRIM_AUTO_DEFAULT, "Auto mode", "auto" },
        { DLB_PAEC_DDP_TRIM_0DB,                   "0 dB",      "0"  },
        { DLB_PAEC_DDP_TRIM_M3DB,                  "-3 dB",     "-3" },
        { DLB_PAEC_DDP_TRIM_M6DB,                  "-6 dB",     "-6" },
        { DLB_PAEC_DDP_TRIM_M9DB,                  "-9 dB",     "-9" },
        { 0, NULL, NULL }
    };

    if (! type) {
        type = g_enum_register_static("GstDlbPatEncSurroundTrim", values);
    }
    
    return type;
}

GType dlbpatenc_height_trim_getType() {
    static GType type = 0;

    static const GEnumValue values[] = {
        { DLB_PAEC_DDP_HEIGHT_TRIM_AUTO_DEFAULT, "Auto mode", "auto"  },
        { DLB_PAEC_DDP_TRIM_M3DB,                "-3 dB",     "-3"  },
        { DLB_PAEC_DDP_TRIM_M6DB,                "-6 dB",     "-6"  },
        { DLB_PAEC_DDP_TRIM_M9DB,                "-9 dB",     "-9"  },
        { DLB_PAEC_DDP_TRIM_M12DB,               "-12 dB",    "-12" },
        { 0, NULL, NULL }
    };

    if (! type) {
        type = g_enum_register_static("GstDlbPatEncHeightTrim", values);
    }
    
    return type;
}

GType dlbpatenc_downmix_level_getType() {
    static GType type = 0;

    static const GEnumValue values[] = {
        { DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_AUTO,   "Auto mode",             "auto" },
        { DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_3_0DB,  "+3.0 dB (center only)", "3"    },
        { DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_1_5DB,  "+1.5 dB (center only)", "1.5"  },
        { DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_0DB,    "0 dB (center only)",    "0"    },
        { DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M1_5DB, "-1.5 dB",               "-1.5" },
        { DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M3_0DB, "-3.0 dB",               "-3.0" },
        { DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M4_5DB, "-4.5 dB",               "-4.5" },
        { DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M6_0DB, "-6.0 dB",               "-6.0" },
        { DLB_PAEC_DDP_LXRX_DOWNMIX_LEVEL_M_INF,  "-inifinity dB",         "-inf" },
        { 0, NULL, NULL }
    };

    if (! type) {
        type = g_enum_register_static("GstDlbPatEncDownmixLevel", values);
    }
    
    return type;
}

GType dlbpatenc_dmixmod_getType() {
    static GType type = 0;

    static const GEnumValue values[] = {
        { DLB_PAEC_DDP_DMIXMOD_AUTO,          "Auto mode",                                                 "auto"  },
        { DLB_PAEC_DDP_DMIXMOD_NOT_INDICATED, "Not indicated",                                             "ni"    },
        { DLB_PAEC_DDP_DMIXMOD_LTRT,          "Lt/Rt (Dolby Pro Logic) downmix is preferred",              "ltrt"  },
        { DLB_PAEC_DDP_DMIXMOD_LORO,          "Lo/Ro (simple stereo representation) downmix is preferred", "loro"  },
        { DLB_PAEC_DDP_DMIXMOD_PROLOGIC2,     "Dolby Pro Logic II downmix is preferred",                   "pro2"  },
        { 0, NULL, NULL }
    };

    if (! type) {
        type = g_enum_register_static("GstDlbPatEncDmixmod", values);
    }
    
    return type;
}