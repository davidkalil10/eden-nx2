// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.features.settings.model.IntSetting
import org.yuzu.yuzu_emu.features.settings.utils.SettingsFile
import org.yuzu.yuzu_emu.model.Game

object PerGameUserProfileUtil {

    private val context get() = YuzuApplication.appContext

    fun getSummaryLabel(game: Game): String {
        val customFile = SettingsFile.getCustomSettingsFile(game)
        if (!customFile.exists()) {
            return globalProfileName()
        }
        return withPerGameConfig(game) {
            if (IntSetting.CURRENT_USER.global) {
                context.getString(R.string.global)
            } else {
                usernameForUserIndex(IntSetting.CURRENT_USER.getInt(false))
                    ?: context.getString(R.string.global)
            }
        }
    }

    fun userIndexForUuid(uuid: String): Int {
        val userUuids = NativeLibrary.getAllUsers() ?: return -1
        return userUuids.indexOfFirst { it == uuid }
    }

    private fun globalProfileName(): String {
        val currentUuid = NativeLibrary.getCurrentUser()
        if (currentUuid.isNullOrEmpty()) {
            return context.getString(R.string.global)
        }
        return NativeLibrary.getUserUsername(currentUuid) ?: context.getString(R.string.global)
    }

    private fun usernameForUserIndex(index: Int): String? {
        val userUuids = NativeLibrary.getAllUsers() ?: return null
        val uuid = userUuids.getOrNull(index)?.takeIf { it.isNotEmpty() } ?: return null
        return NativeLibrary.getUserUsername(uuid)
    }

    private inline fun <T> withPerGameConfig(game: Game, block: () -> T): T {
        val wasPerGame = NativeConfig.isPerGameConfigLoaded()
        if (!wasPerGame) {
            SettingsFile.loadCustomConfig(game)
        }
        return try {
            block()
        } finally {
            if (!wasPerGame) {
                NativeConfig.unloadPerGameConfig()
                NativeConfig.reloadGlobalConfig()
            }
        }
    }
}
