// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.model

import android.os.Parcelable
import kotlinx.parcelize.Parcelize
import org.yuzu.yuzu_emu.NativeLibrary

@Parcelize
data class UserProfile(
    val uuid: String,
    val username: String,
    val imagePath: String = ""
) : Parcelable

object ProfileUtils {
    fun generateRandomUUID(): String =
        java.util.UUID.randomUUID().toString().replace("-", "")

    fun loadRegisteredProfiles(): List<UserProfile> {
        val userUuids = NativeLibrary.getAllUsers() ?: return emptyList()
        return userUuids.mapNotNull { uuid ->
            if (uuid.isEmpty()) {
                return@mapNotNull null
            }
            val username = NativeLibrary.getUserUsername(uuid) ?: return@mapNotNull null
            if (username.isEmpty()) {
                return@mapNotNull null
            }
            UserProfile(uuid, username, NativeLibrary.getUserImagePath(uuid).orEmpty())
        }
    }

    fun soleRegisteredUserIndex(): Int? {
        val profiles = loadRegisteredProfiles()
        if (profiles.size != 1) {
            return null
        }
        val userUuids = NativeLibrary.getAllUsers() ?: return null
        return userUuids.indexOfFirst { it == profiles[0].uuid }.takeIf { it >= 0 }
    }

    fun shouldPromptForProfileSelection(): Boolean = loadRegisteredProfiles().size > 1

    fun usernameForIndex(index: Int): String {
        val uuid = NativeLibrary.getAllUsers()?.getOrNull(index)?.takeIf { it.isNotEmpty() }
            ?: return ""
        return NativeLibrary.getUserUsername(uuid).orEmpty()
    }
}