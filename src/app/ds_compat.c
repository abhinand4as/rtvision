/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

/* ds_compat.c - symbols that the in-image deepstream-app sources reference but that Perceptra
 * provides itself instead of compiling DeepStream's nvds_utils.cpp (which would add an OpenSSL
 * dependency for an unused SHA-256 helper). See docs/DEVIATIONS.md.
 *
 * nvds_get_hostname_with_ip() is only called by deepstream_app.c's msgapi error-propagation
 * path, which Perceptra never enables. Contract (nvds_utils.h): newly allocated string, caller
 * frees with g_free(). Own implementation, not derived from DeepStream.
 */
#include <glib.h>

#include "nvds_msgapi.h" /* NvDsMsgApiErrorType, used by nvds_utils.h */
#include "nvds_utils.h"

char *nvds_get_hostname_with_ip(void) {
    return g_strdup(g_get_host_name());
}
