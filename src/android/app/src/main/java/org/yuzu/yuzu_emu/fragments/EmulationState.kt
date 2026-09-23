// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu.fragments

import android.view.Surface
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.utils.Log

/**
 * Frontend view of the native emulation session. The native session is a process-wide
 * singleton that outlives the fragment, so a fresh state may find a game already running.
 */
internal class EmulationState<S : Any>(
    private val gamePath: String,
    private val emulationCanStart: () -> Boolean,
    private val canAttach: () -> Boolean,
    private val onAttachedToRunningSession: () -> Unit,
    private val backend: Backend<S>
) {
    interface Backend<S> {
        fun isValid(surface: S): Boolean
        fun isRunning(): Boolean
        fun isPaused(): Boolean
        fun surfaceChanged(surface: S?)
        fun surfaceDestroyed()
        fun run(path: String, programIndex: Int)
        fun changeProgram(path: String, programIndex: Int)
        fun pause()
        fun unpause()
        fun stop()
        fun debug(message: String)
        fun warning(message: String)
    }

    object NativeBackend : Backend<Surface> {
        override fun isValid(surface: Surface): Boolean = surface.isValid
        override fun isRunning(): Boolean = NativeLibrary.isRunning()
        override fun isPaused(): Boolean = NativeLibrary.isPaused()
        override fun surfaceChanged(surface: Surface?) = NativeLibrary.surfaceChanged(surface)
        override fun surfaceDestroyed() = NativeLibrary.surfaceDestroyed()
        override fun run(path: String, programIndex: Int) =
            NativeLibrary.run(path, programIndex, true)
        override fun changeProgram(path: String, programIndex: Int) =
            NativeLibrary.run(path, programIndex, false)
        override fun pause() {
            NativeLibrary.pauseEmulation()
            NativeLibrary.playTimeManagerStop()
        }
        override fun unpause() {
            NativeLibrary.unpauseEmulation()
            NativeLibrary.playTimeManagerStart()
        }
        override fun stop() = NativeLibrary.stopEmulation()
        override fun debug(message: String) = Log.debug(message)
        override fun warning(message: String) = Log.warning(message)
    }

    private var state = State.STOPPED
    private var surface: S? = null

    /** The thread running the session, shared by every state since the session is. */
    val emulationThread: Thread?
        get() = sessionThread

    @get:Synchronized
    val isStopped: Boolean
        get() = state == State.STOPPED

    @get:Synchronized
    val isPaused: Boolean
        get() = state == State.PAUSED

    @get:Synchronized
    val isRunning: Boolean
        get() = state == State.RUNNING

    @Synchronized
    fun stop() {
        if (state != State.STOPPED) {
            backend.debug("[EmulationFragment] Stopping emulation.")
            state = State.STOPPED
        } else {
            backend.warning("[EmulationFragment] Stop called while already stopped.")
        }
        backend.stop()
    }

    @Synchronized
    fun pause() {
        if (state != State.PAUSED) {
            backend.debug("[EmulationFragment] Pausing emulation.")
            backend.pause()
            state = State.PAUSED
        } else {
            backend.warning("[EmulationFragment] Pause called while already paused.")
        }
    }

    @Synchronized
    fun run() {
        // If the surface is set, run now. Otherwise, wait for it to get set.
        if (surface != null) {
            runWithValidSurface()
        }
    }

    @Synchronized
    fun resume() {
        if (state != State.PAUSED) {
            backend.warning("[EmulationFragment] Resume called while emulation is not paused.")
            return
        }
        if (!emulationCanStart.invoke()) {
            backend.warning("[EmulationFragment] Resume blocked by emulationCanStart check.")
            return
        }
        val currentSurface = surface
        if (currentSurface == null || !backend.isValid(currentSurface)) {
            backend.debug("[EmulationFragment] Resume requested with invalid surface.")
            return
        }

        backend.surfaceChanged(currentSurface)
        backend.debug("[EmulationFragment] Resuming emulation.")
        backend.unpause()
        state = State.RUNNING
    }

    @Synchronized
    fun changeProgram(programIndex: Int) {
        sessionThread?.join()
        startThread { backend.changeProgram(gamePath, programIndex) }
    }

    // Surface callbacks
    @Synchronized
    fun newSurface(surface: S?) {
        this.surface = surface
        if (this.surface != null) {
            runWithValidSurface()
        }
    }

    @Synchronized
    fun updateSurface() {
        if (surface != null && state == State.RUNNING) {
            backend.surfaceChanged(surface)
        }
    }

    @Synchronized
    fun updateSurfaceReference(surface: S?) {
        this.surface = surface
        if (this.surface != null && state == State.RUNNING) {
            backend.surfaceChanged(this.surface)
        }
    }

    @Synchronized
    fun clearSurface() {
        if (surface == null) {
            backend.debug("[EmulationFragment] clearSurface called, but surface already null.")
            return
        }
        if (state == State.RUNNING) {
            pause()
        }
        backend.surfaceDestroyed()
        surface = null
        backend.debug("[EmulationFragment] Surface destroyed in state $state.")
    }

    private fun runWithValidSurface() {
        val currentSurface = surface
        if (currentSurface == null || !backend.isValid(currentSurface)) {
            backend.debug("[EmulationFragment] runWithValidSurface called with invalid surface.")
            return
        }

        // A recreated activity finds the session of the one it replaced: show it paused
        // on the new surface. Booting it again would run a second game on the singleton.
        if (state == State.STOPPED && backend.isRunning()) {
            if (!canAttach.invoke()) {
                return
            }
            backend.debug("[EmulationFragment] Attaching to the running session, paused.")
            backend.surfaceChanged(currentSurface)
            if (!backend.isPaused()) {
                backend.pause()
            }
            state = State.PAUSED
            onAttachedToRunningSession.invoke()
            return
        }

        if (!emulationCanStart.invoke()) {
            return
        }

        when (state) {
            State.STOPPED -> {
                backend.surfaceChanged(currentSurface)
                startThread { backend.run(gamePath, 0) }
                state = State.RUNNING
            }

            State.PAUSED -> backend.debug(
                "[EmulationFragment] Surface restored while emulation paused; " +
                    "waiting for explicit resume."
            )

            else -> backend.debug("[EmulationFragment] Bug, run called while already running.")
        }
    }

    private fun startThread(body: () -> Unit) {
        val thread = Thread({
            backend.debug("[EmulationFragment] Starting emulation thread.")
            body()
        }, "NativeEmulation")
        sessionThread = thread
        thread.start()
    }

    private enum class State {
        STOPPED, RUNNING, PAUSED
    }

    companion object {
        @Volatile
        private var sessionThread: Thread? = null
    }
}
