// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import java.io.File
import java.io.IOException
import java.nio.file.Files
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/**
 * A kill can land between any two steps of an import. On relaunch getExternalFilesDir()
 * recreates an empty user directory and only then does recovery run, so every state below
 * includes the directory as the next launch leaves it.
 */
class DirectorySwapTest {
    private lateinit var root: File
    private lateinit var user: File

    @Before
    fun setUp() {
        root = Files.createTempDirectory("swap").toFile()
        user = File(root, "files")
        write(user, "old")
    }

    @After
    fun tearDown() {
        root.deleteRecursively()
    }

    private fun write(dir: File, value: String) {
        File(dir, "config").mkdirs()
        File(dir, "config/config.ini").writeText(value)
    }

    private fun config(): String = File(user, "config/config.ini").readText()

    private fun sibling(suffix: String) = File(root, "files.$suffix")

    // What the next launch does to a missing user directory before recovery runs.
    private fun relaunch() {
        user.mkdirs()
        DirectorySwap.recover(user)
    }

    private fun assertOnlyTheUserDirectoryIsLeft() =
        assertEquals(listOf("files"), root.list()!!.toList())

    @Test
    fun failedUnpackKeepsTheCurrentData() {
        var swapped = false
        val result = DirectorySwap.replace(user, beforeSwap = { swapped = true }) { staging ->
            File(staging, "partial").writeText("x")
            throw IOException("truncated zip")
        }
        assertEquals(DirectorySwap.Result.UnpackFailed, result)
        assertFalse(swapped)
        assertEquals("old", config())
        assertOnlyTheUserDirectoryIsLeft()
    }

    @Test
    fun completeUnpackReplacesTheDirectory() {
        val result = DirectorySwap.replace(user) { write(it, "new") }
        assertEquals(DirectorySwap.Result.Replaced, result)
        assertEquals("new", config())
        assertOnlyTheUserDirectoryIsLeft()
    }

    @Test
    fun missingTargetIsCreated() {
        user.deleteRecursively()
        val result = DirectorySwap.replace(user) { File(it, "a").writeText("1") }
        assertEquals(DirectorySwap.Result.Replaced, result)
        assertTrue(File(user, "a").exists())
    }

    @Test
    fun killDuringUnpackKeepsTheOldData() {
        write(sibling("import"), "partial")
        relaunch()
        assertEquals("old", config())
        assertOnlyTheUserDirectoryIsLeft()
    }

    @Test
    fun killAfterACompleteUnpackKeepsTheOldData() {
        // beforeSwap runs between the unpack and the first rename: the kill point itself.
        try {
            DirectorySwap.replace(user, beforeSwap = { throw IllegalStateException("killed") }) {
                write(it, "new")
            }
        } catch (_: IllegalStateException) {
        }
        relaunch()
        assertEquals("old", config())
        assertOnlyTheUserDirectoryIsLeft()
    }

    @Test
    fun killBetweenTheRenamesFinishesTheImportOverTheRecreatedDirectory() {
        user.renameTo(sibling("previous"))
        write(sibling("ready"), "new")
        user.mkdirs()
        write(user, "defaults written by the relaunch")
        DirectorySwap.recover(user)
        assertEquals("new", config())
        assertOnlyTheUserDirectoryIsLeft()
    }

    @Test
    fun killAfterTheSecondRenameDropsOnlyTheOldCopy() {
        user.renameTo(sibling("previous"))
        write(user, "new")
        relaunch()
        assertEquals("new", config())
        assertOnlyTheUserDirectoryIsLeft()
    }

    @Test
    fun oldCopyWithoutAnyUserDirectoryIsRestored() {
        user.renameTo(sibling("previous"))
        DirectorySwap.recover(user)
        assertEquals("old", config())
        assertOnlyTheUserDirectoryIsLeft()
    }

    @Test
    fun startupRecoversBeforeNativeCodeReadsTheUserDirectory() {
        val source = File("src/main/java/org/yuzu/yuzu_emu/utils/DirectoryInitialization.kt")
            .readText()
        val recover = source.indexOf("DirectorySwap.recover(")
        val native = source.indexOf("NativeLibrary.setAppDirectory(")
        assertTrue("startup never recovers an interrupted import", recover >= 0)
        assertTrue("recovery runs after native init", recover < native)
    }
}
