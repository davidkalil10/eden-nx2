// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class FileUtilTest {
    @Test
    fun providerNamesAreReducedToOneComponent() {
        assertEquals("driver.zip", FileUtil.safeFileName("driver.zip"))
        assertEquals("driver.zip", FileUtil.safeFileName("/driver.zip"))
        assertEquals("x.xml", FileUtil.safeFileName("../shared_prefs/x.xml"))
    }

    @Test
    fun namesWithNothingLeftAreRejected() {
        assertNull(FileUtil.safeFileName(""))
        assertNull(FileUtil.safeFileName(".."))
        assertNull(FileUtil.safeFileName("a/.."))
        assertNull(FileUtil.safeFileName("dir/"))
    }
}
