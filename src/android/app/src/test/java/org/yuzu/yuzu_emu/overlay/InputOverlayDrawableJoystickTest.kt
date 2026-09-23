// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.overlay

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class InputOverlayDrawableJoystickTest {
    private fun step(old: Pair<Float, Float>, axisX: Float, axisY: Float): Boolean {
        val (x, y) = InputOverlayDrawableJoystick.clampToCircle(axisX, axisY)
        return InputOverlayDrawableJoystick.axesChanged(old.first, old.second, x, y)
    }

    @Test
    fun horizontalPushFromCenterIsReported() {
        assertTrue(step(0f to 0f, 0.5f, 0f))
    }

    @Test
    fun verticalPushFromCenterIsReported() {
        assertTrue(step(0f to 0f, 0f, -0.75f))
    }

    @Test
    fun returningToCenterAlongOneAxisIsReported() {
        assertTrue(step(0.5f to 0f, 0f, 0f))
    }

    @Test
    fun unchangedPositionIsNotReported() {
        val (x, y) = InputOverlayDrawableJoystick.clampToCircle(0.3f, 0.4f)
        assertFalse(step(x to y, 0.3f, 0.4f))
    }

    @Test
    fun deflectionIsClampedToTheUnitCircle() {
        val (x, y) = InputOverlayDrawableJoystick.clampToCircle(3f, 4f)
        assertEquals(0.6f, x, 1e-5f)
        assertEquals(0.8f, y, 1e-5f)
    }
}
