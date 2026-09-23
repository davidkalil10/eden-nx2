// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.model

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TaskViewModelTest {
    @Test
    fun freshViewModelReportsNoTask() {
        // What a progress dialog restored after process death finds.
        assertFalse(TaskViewModel().hasTask)
    }

    @Test
    fun assignedTaskIsReported() {
        val model = TaskViewModel()
        model.task = { _, _ -> Unit }
        assertTrue(model.hasTask)
    }
}
