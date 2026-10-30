#include "common.h"

#include "common/dolby.h"
#include "common/utils.h"


GST_DEBUG_CATEGORY(gst_dlbpatcommon_debug);
#define GST_CAT_DEFAULT gst_dlbpatcommon_debug


void dlbpat_common_initialize() {
    static gsize dummy = 0;

    if (g_once_init_enter (&dummy)) {
        int dlbRet;

        GST_DEBUG_CATEGORY_INIT(gst_dlbpatcommon_debug, "dlbpatcommon", 0, "dlbpatcommon");

        {
            dlb_padc_version version;

            dlbRet = dlb_padc_version_get(&version);
            if (dlbRet != DLB_PALOUD_STATUS_OK) {
                GST_ERROR("Failed to retrieve PADC version! Error code: %d", dlbRet);

            } else {
                GST_INFO("Dolby PADC version %u.%u.%u.%04x", 
                    version.v_major, version.v_minor, version.v_func, version.v_build
                );
            }
        }

        {
            dlb_padmx_version version;

            dlbRet = dlb_padmx_version_get(&version);
            if (dlbRet != DLB_PALOUD_STATUS_OK) {
                GST_ERROR("Failed to retrieve PADMX version! Error code: %d", dlbRet);

            } else {
                GST_INFO("Dolby PADMX version %u.%u.%u.%04x", 
                    version.v_major, version.v_minor, version.v_func, version.v_build
                );
            }
        }

        {
            dlb_paloud_version version;

            dlbRet = dlb_paloud_version_get(&version);
            if (dlbRet != DLB_PALOUD_STATUS_OK) {
                GST_ERROR("Failed to retrieve PALOUD version! Error code: %d", dlbRet);

            } else {
                GST_INFO("Dolby PALOUD version %u.%u.%u.%04x", 
                    version.v_major, version.v_minor, version.v_func, version.v_build
                );
            }
        }

        {
            dlb_paec_version version;

            dlbRet = dlb_paec_version_get(&version);
            if (dlbRet != DLB_PALOUD_STATUS_OK) {
                GST_ERROR("Failed to retrieve PAEC version! Error code: %d", dlbRet);

            } else {
                GST_INFO("Dolby PAEC version %u.%u.%u.%04x", 
                    version.v_major, version.v_minor, version.v_func, version.v_build
                );
            }
        }

        {
            dlb_pamif_version version;

            dlbRet = dlb_pamif_version_get(&version);
            if (dlbRet != DLB_PALOUD_STATUS_OK) {
                GST_ERROR("Failed to retrieve PAMIF version! Error code: %d", dlbRet);

            } else {
                GST_INFO("Dolby PAMIF version %u.%u.%u.%04x", 
                    version.v_major, version.v_minor, version.v_func, version.v_build
                );
            }   
        }

        g_once_init_leave(&dummy, dlbRet == DLBPAT_SUCCESS);
    }
}