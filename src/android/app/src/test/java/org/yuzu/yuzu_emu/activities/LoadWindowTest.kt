// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.activities

import java.util.concurrent.CountDownLatch
import java.util.concurrent.atomic.AtomicInteger
import org.junit.Assert.assertEquals
import org.junit.Test
import org.yuzu.yuzu_emu.fragments.EmulationState

/**
 * Native m_is_running turns true only in RunEmulation, after InitializeEmulation (the ROM
 * load). A recreation inside that window must not boot the game a second time.
 */
class LoadWindowTest {
    private class LoadingSession : EmulationState.Backend<String> {
        @Volatile var running = false
        val boots = AtomicInteger()
        val loadDone = CountDownLatch(1)
        override fun isValid(surface: String) = true
        override fun isRunning() = running
        override fun isPaused() = false
        override fun isStarted() = running
        override fun surfaceChanged(surface: String?) {}
        override fun surfaceDestroyed() {}
        override fun run(path: String, programIndex: Int) {
            boots.incrementAndGet()
            loadDone.await() // InitializeEmulation: m_is_running still false
            running = true
        }
        override fun changeProgram(path: String, programIndex: Int) {}
        override fun pause() {}
        override fun requestPause() {}
        override fun cancelPauseRequest() {}
        override fun unpause() {}
        override fun stopPlayTime() {}
        override fun stop() {
            running = false
        }
        override fun debug(message: String) {}
        override fun warning(message: String) {}
    }

    private fun state(s: LoadingSession) = EmulationState(
        gamePath = "game.nsp",
        emulationCanStart = { true },
        canAttach = { true },
        onAttachedToRunningSession = {},
        backend = s
    )

    @Test
    fun activityRecreatedWhileTheGameLoadsBootsItOnlyOnce() {
        val s = LoadingSession()
        val first = state(s)
        first.newSurface("first")
        Thread.sleep(100)
        first.clearSurface() // old activity destroyed mid-load
        val restored = state(s) // recreated activity, fresh fragment state
        restored.newSurface("second")
        Thread.sleep(100)
        s.loadDone.countDown()
        restored.emulationThread?.join()
        assertEquals(1, s.boots.get())
    }
}
