// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * A kill while a `.ready` copy is being deleted must not leave a partial import that the
 * next launch trusts and swaps in over the intact data.
 */
class PartialReadyTest {
    @Test
    fun partialReadyLeftByAnInterruptedCleanupIsNotSwappedIn() {
        val root = Files.createTempDirectory("pr").toFile()
        val user = File(root, "files")
        File(user, "nand/save").mkdirs()
        File(user, "nand/save/slot").writeText("old save")
        File(user, "config").mkdirs()
        File(user, "config/config.ini").writeText("old")
        // deleteRecursively goes children first: killed after the saves went, config left
        val ready = File(root, "files.ready")
        File(ready, "config").mkdirs()
        File(ready, "config/config.ini").writeText("new")
        DirectorySwap.recover(user)
        val kept = File(user, "nand/save/slot").exists()
        root.deleteRecursively()
        assertTrue("old saves were replaced by a half-deleted import", kept)
    }
}
