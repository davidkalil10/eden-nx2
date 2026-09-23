// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.fragments

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class EditUserDialogFragmentTest {
    @Test
    fun asciiUpToThirtyTwoCharactersFits() {
        assertTrue(EditUserDialogFragment.isValidUsername("x".repeat(32)))
        assertFalse(EditUserDialogFragment.isValidUsername("x".repeat(33)))
    }

    @Test
    fun multiByteNamesAreMeasuredInBytes() {
        // 11 CJK characters are 33 UTF-8 bytes; 9 emoji are 36.
        assertFalse(EditUserDialogFragment.isValidUsername("\u540D".repeat(11)))
        assertTrue(EditUserDialogFragment.isValidUsername("\u540D".repeat(10)))
        assertFalse(EditUserDialogFragment.isValidUsername("\uD83D\uDE00".repeat(9)))
    }

    @Test
    fun emptyNameIsRejected() {
        assertFalse(EditUserDialogFragment.isValidUsername(""))
    }
}
