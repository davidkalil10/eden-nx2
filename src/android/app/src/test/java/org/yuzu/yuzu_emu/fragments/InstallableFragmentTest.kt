// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.fragments

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class InstallableFragmentTest {
    @Test
    fun hexTitleFolderBecomesDecimal() {
        assertEquals(
            "72057594037993472",
            InstallableFragment.titleIdFromSaveFolder("0100000000010000")
        )
    }

    @Test
    fun foldersThatAreNotTitleIdsAreSkipped() {
        assertNull(InstallableFragment.titleIdFromSaveFolder("saves"))
        assertNull(InstallableFragment.titleIdFromSaveFolder(""))
        assertNull(InstallableFragment.titleIdFromSaveFolder("-1"))
    }
}
