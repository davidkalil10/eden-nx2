// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.activities

import java.util.concurrent.CountDownLatch
import java.util.concurrent.atomic.AtomicInteger
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.yuzu.yuzu_emu.fragments.EmulationState

/**
 * Home during the loading screen defers the pause; tapping play before the game runs must
 * not unpause natively, since UnPauseEmulation takes the m_mutex the load holds.
 */
class DeferredResumeTest {
    private class Loading : EmulationState.Backend<String> {
        @Volatile var running = false
        val lockedCallsWhileLoading = AtomicInteger()
        val loaded = CountDownLatch(1)
        val ended = CountDownLatch(1)
        val threads = java.util.Collections.synchronizedList(mutableListOf<Thread>())
        override fun isValid(surface: String) = true
        override fun isRunning() = running
        override fun isPaused() = false
        override fun isStarted() = running
        override fun surfaceChanged(surface: String?) {}
        override fun surfaceDestroyed() {}
        override fun run(path: String, programIndex: Int) {
            threads.add(Thread.currentThread())
            loaded.await()
            running = true
            ended.await()
            running = false
        }
        override fun changeProgram(path: String, programIndex: Int) {}

        // PauseEmulation, UnPauseEmulation, HaltEmulation all take m_mutex.
        override fun pause() {
            if (!running) lockedCallsWhileLoading.incrementAndGet()
        }
        override fun requestPause() {}
        override fun cancelPauseRequest() {}
        override fun unpause() {
            if (!running) lockedCallsWhileLoading.incrementAndGet()
        }
        override fun stopPlayTime() {}
        override fun stop() {
            loaded.countDown()
            ended.countDown()
        }
        override fun debug(message: String) {}
        override fun warning(message: String) {}
    }

    private val s = Loading()
    private val state = EmulationState("game.nsp", { true }, { true }, {}, s)

    // The session thread is process-wide: a live one would be attached to by the next class.
    @After
    fun end() {
        s.stop()
        state.stop()
        // Threads recorded as they run, plus the one started last, in case it has not yet run.
        val started = synchronized(s.threads) { s.threads.toList() } +
            listOfNotNull(state.emulationThread)
        started.forEach { it.join(5000) }
        assertTrue("a session thread outlived the test", started.none { it.isAlive })
    }

    @Test
    fun homeAndBackDuringTheLoadThenTapPlayNeverWaitsOnTheLoadLock() {
        state.newSurface("surface") // boots, still loading
        Thread.sleep(50)
        state.pause() // onPause: deferred, no lock taken
        state.resume() // paused icon tapped while the loading card is up
        assertEquals("UI thread waited on the load lock", 0, s.lockedCallsWhileLoading.get())
    }
}
