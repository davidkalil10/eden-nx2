// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.overlay

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class OverlayScaleDialogTest {
    private fun onGrid(v: Float): Boolean {
        val steps = (v - OverlayScaleDialog.MIN_SCALE) / OverlayScaleDialog.SCALE_STEP
        return Math.abs(steps - Math.round(steps)) < 1e-4f
    }

    @Test
    fun scaleFromAnOldConfigFallsBackToOne() {
        assertEquals(1.0f, OverlayScaleDialog.sliderValue(0f), 0f)
        assertEquals(1.0f, OverlayScaleDialog.sliderValue(Float.NaN), 0f)
        assertEquals(1.0f, OverlayScaleDialog.sliderValue(9f), 0f)
    }

    @Test
    fun valuesLandOnTheSliderGrid() {
        for (v in listOf(0.5f, 0.73f, 1.0f, 1.25f, 2.0f, 3.99f, 4.0f)) {
            val snapped = OverlayScaleDialog.sliderValue(v)
            assertTrue("$v -> $snapped", snapped >= OverlayScaleDialog.MIN_SCALE)
            assertTrue("$v -> $snapped", snapped <= OverlayScaleDialog.MAX_SCALE)
            assertTrue("$v -> $snapped", onGrid(snapped))
        }
    }

    @Test
    fun constantsMatchTheLayout() {
        val xml = File("src/main/res/layout/dialog_overlay_scale.xml").readText()
        assertTrue(xml.contains("android:valueFrom=\"${OverlayScaleDialog.MIN_SCALE}\""))
        assertTrue(xml.contains("android:valueTo=\"${OverlayScaleDialog.MAX_SCALE}\""))
        assertTrue(xml.contains("android:stepSize=\"${OverlayScaleDialog.SCALE_STEP}\""))
    }
}
