// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.settings.model

import java.io.File
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * NativeConfig setters write the global slot of a switchable setting unless the Kotlin setter
 * first marks the per-game override, so reset() must go through that setter.
 */
class SettingResetTest {
    @Test
    fun resetNeverWritesNativeConfigDirectly() {
        val dir = File("src/main/java/org/yuzu/yuzu_emu/features/settings/model")
        val files = dir.listFiles { f -> f.extension == "kt" }!!
        assertTrue("no setting sources under ${dir.absolutePath}", files.isNotEmpty())
        val direct = Regex("""fun reset\(\)\s*=\s*NativeConfig\.""")
        val offenders = files.filter { direct.containsMatchIn(it.readText()) }.map { it.name }
        assertTrue("reset() bypasses the per-game setter in $offenders", offenders.isEmpty())
    }
}
