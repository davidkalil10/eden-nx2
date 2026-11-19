// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.model.ProfileUtils

object SaveDataUserUtil {
    fun userIndexForUuid(uuid: String): Int {
        if (uuid.isEmpty()) {
            return -1
        }
        return NativeLibrary.getAllUsers()?.indexOfFirst { it == uuid } ?: -1
    }

    fun effectiveUserIndex(): Int =
        userIndexForUuid(NativeLibrary.getCurrentUser().orEmpty()).coerceAtLeast(0)

    fun saveUserIndices(): Iterable<Int> {
        val indices = linkedSetOf(effectiveUserIndex())
        ProfileUtils.loadRegisteredProfiles().forEach { profile ->
            userIndexForUuid(profile.uuid).takeIf { it >= 0 }?.let { indices.add(it) }
        }
        return indices
    }
}