// Copyright (C) 2026 apkreader
// SPDX-License-Identifier: Apache-2.0

#include <android/fdsan.h>

__attribute__((constructor)) static void wifi_fdsan_compat(void) {
    // The EMUI Wi-Fi HAL closes a DIR-owned descriptor when flushing ring buffers.
    android_fdsan_set_error_level(ANDROID_FDSAN_ERROR_LEVEL_WARN_ONCE);
}
