#include "common/dolby.h"
#include "types.h"


GType dlbpatloud_reg_type_getType() {
    static GType type = 0;

    static const GEnumValue values[] = {
        { DLB_PALOUD_REG_TYPE_ATSC,   "Program loudness complies with ATSC A/85    (dialogue intelligence = ON , target loudness = -24 LKFS)", "atsc"  },
        { DLB_PALOUD_REG_TYPE_EBU,    "Program loudness complies with EBU R 128    (dialogue intelligence = OFF, target loudness = -23 LKFS)", "ebu"   },
        { DLB_PALOUD_REG_TYPE_ARIB,   "Program loudness complies with ARIB TR-B32  (dialogue intelligence = ON,  target loudness = -24 LKFS)", "arib"  },
        { DLB_PALOUD_REG_TYPE_FREETV, "Program loudness complies with FreeTV OP-59 (dialogue intelligence = OFF, target loudness = -24 LKFS)", "freetv"},
        { DLB_PALOUD_REG_TYPE_MANUAL, "No specification indicated. Loudness control configured manually", "manual"},
        { 0, NULL, NULL}
    };

    if (! type) {
        type = g_enum_register_static("GstDlbPatLoudRegulationType", values);
    }
    
    return type;
}


GType dlbpatloud_channels_getType() {
    static GType type = 0;

    static const GEnumValue values[] = {
        { DLB_PALOUD_CHANNEL_CONFIG_10,  "First channel for a mono program"     , "1"  },
        { DLB_PALOUD_CHANNEL_CONFIG_20,  "First 2 channels for a stereo program", "2"  },
        { DLB_PALOUD_CHANNEL_CONFIG_51,  "First 6 channels for a 5.1 program"   , "6"  },
        { DLB_PALOUD_CHANNEL_CONFIG_514, "First 10 channels for a 5.1.4 program", "10" },
        { 0, NULL, NULL}
    };

    if (! type) {
        type = g_enum_register_static("GstDlbPatLoudChannels", values);
    }
    
    return type;
}


GType dlbpatloud_mode_getType() {
    static GType type = 0;

    static const GEnumValue values[] = {
        { DLB_PALOUD_LEVEL_MODE_ON,   "Force (ignoring external metadata) leveling", "on"   },
        { DLB_PALOUD_LEVEL_MODE_OFF,  "Bypass audio leveling"                      , "off"  },
        { DLB_PALOUD_LEVEL_MODE_AUTO, "Follows external metadata if available"     , "auto" },
        { 0, NULL, NULL}
    };

    if (! type) {
        type = g_enum_register_static("GstDlbPatLoudMode", values);
    }
    
    return type;
}


GType dlbpatloud_dialint_getType() {
    static GType type = 0;

    static const GEnumValue values[] = {
        { DLB_PALOUD_DIALOGUE_INTELLIGENCE_ON,   "Dialogue intelligence on",  "on"   },
        { DLB_PALOUD_DIALOGUE_INTELLIGENCE_OFF,  "Dialogue intelligence off", "off"  },
        { 0, NULL, NULL}
    };

    if (! type) {
        type = g_enum_register_static("GstDlbPatDialInt", values);
    }
    
    return type;
}