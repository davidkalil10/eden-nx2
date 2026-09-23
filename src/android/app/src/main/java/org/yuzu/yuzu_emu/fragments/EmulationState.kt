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
    private val onAttachedToRunningSession: (loading: Boolean) -> Unit,
    private val backend: Backend<S>,
    private val onWaitingForSession: () -> Unit = {}
) {
    interface Backend<S> {
        fun isValid(surface: S): Boolean
        fun isRunning(): Boolean
        fun isPaused(): Boolean
        fun isStarted(): Boolean
        fun surfaceChanged(surface: S?)
        fun surfaceDestroyed()
        fun run(path: String, programIndex: Int)
        fun changeProgram(path: String, programIndex: Int)
        fun pause()
        fun requestPause()
        fun cancelPauseRequest()
        fun unpause()
        fun stopPlayTime()
        fun stop()
        fun debug(message: String)
        fun warning(message: String)
    }

    object NativeBackend : Backend<Surface> {
        override fun isValid(surface: Surface): Boolean = surface.isValid
        override fun isRunning(): Boolean = NativeLibrary.isRunning()
        override fun isPaused(): Boolean = NativeLibrary.isPaused()
        override fun isStarted(): Boolean = NativeLibrary.isStarted()
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
        override fun requestPause() = NativeLibrary.requestPauseEmulation()
        override fun cancelPauseRequest() = NativeLibrary.cancelPauseRequest()
        override fun unpause() {
            NativeLibrary.unpauseEmulation()
            NativeLibrary.playTimeManagerStart()
        }
        override fun stopPlayTime() = NativeLibrary.playTimeManagerStop()
        override fun stop() = NativeLibrary.stopEmulation()
        override fun debug(message: String) = Log.debug(message)
        override fun warning(message: String) = Log.warning(message)
    }

    private var state = State.STOPPED
    private var surface: S? = null

    // Attached while the game was still loading: the real pause waits until it runs.
    private var pauseWhenStarted = false

    // Another game's session still owns the native singleton: boot once its thread ends.
    private var bootWhenSessionGone = false

    /** The thread running the session, shared by every state since the session is. */
    val emulationThread: Thread?
        get() = sessionThread

    @get:Synchronized
    val isStopped: Boolean
        get() = state == State.STOPPED

    @get:Synchronized
    val isPaused: Boolean
        get() = state == State.PAUSED

    /** A pause asked for during the load, not yet applied to the running game. */
    @get:Synchronized
    val isPauseDeferred: Boolean
        get() = pauseWhenStarted

    @get:Synchronized
    val isRunning: Boolean
        get() = state == State.RUNNING

    @Synchronized
    fun stop() {
        pauseWhenStarted = false
        bootWhenSessionGone = false
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
            if (backend.isStarted()) {
                backend.pause()
            } else {
                // Still loading: pause() would wait on the lock the load holds.
                backend.requestPause()
                pauseWhenStarted = true
            }
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
        if (pauseWhenStarted || !backend.isStarted()) {
            // Unpausing takes the lock the load holds, and before the start it would run the
            // game over the disk shader cache load; the pause is applied once it starts.
            backend.warning("[EmulationFragment] Resume ignored while the game loads.")
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

    enum class DeferredPause { None, Loading, Started, Failed }

    /**
     * One step of the watch on a pause deferred during the load: [DeferredPause.Loading]
     * asks to look again later, [DeferredPause.Started] means the game now runs paused,
     * [DeferredPause.Failed] that its load failed (see [takeUnshownFailure]).
     */
    @Synchronized
    fun reconcileDeferredPause(): DeferredPause {
        if (bootWhenSessionGone) {
            if (sessionThread?.isAlive == true) {
                return DeferredPause.Loading
            }
            bootWhenSessionGone = false
            run()
            return DeferredPause.None
        }
        if (!pauseWhenStarted || state != State.PAUSED) {
            return DeferredPause.None
        }
        if (sessionThread?.isAlive != true) {
            return onSessionGone()
        }
        if (!backend.isStarted()) {
            return DeferredPause.Loading
        }
        onSessionStarted()
        return DeferredPause.Started
    }

    /** The session now runs; finish a pause requested while it loaded. */
    @Synchronized
    fun onSessionStarted() {
        if (!pauseWhenStarted) {
            return
        }
        pauseWhenStarted = false
        if (state != State.PAUSED) {
            return
        }
        // The load created the window from the surface the old activity had.
        surface?.let { if (backend.isValid(it)) backend.surfaceChanged(it) }
        backend.pause()
    }

    /**
     * The thread attached to during its load ended without running the game: nothing is
     * left to resume, so do what a fresh state would and boot it, unless the load failed,
     * whose error dialog is already closing the activity.
     */
    @Synchronized
    fun onSessionGone(): DeferredPause {
        if (!pauseWhenStarted || state != State.PAUSED || sessionThread?.isAlive == true) {
            return DeferredPause.None
        }
        pauseWhenStarted = false
        state = State.STOPPED
        // The request was for the session that is gone; the next load would honour it.
        backend.cancelPauseRequest()
        if (sessionFailed) {
            backend.warning("[EmulationFragment] The attached load failed; not booting again.")
            return DeferredPause.Failed
        }
        run()
        return DeferredPause.None
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

        // A recreated activity finds the running session: show it paused on the new surface
        // rather than boot a second game. The native flag is set only after the load.
        val live = backend.isRunning() || sessionThread?.isAlive == true
        val started = backend.isStarted()
        if (state == State.STOPPED && live && sessionGamePath == gamePath) {
            if (!canAttach.invoke()) {
                return
            }
            backend.debug("[EmulationFragment] Attaching to the session, paused.")
            backend.surfaceChanged(currentSurface)
            if (!started) {
                // pause() would wait on the lock the load holds.
                backend.requestPause()
                pauseWhenStarted = true
            } else if (!backend.isPaused()) {
                backend.pause()
            } else {
                // Already paused natively: still stop counting play time, as pause() does.
                backend.stopPlayTime()
            }
            state = State.PAUSED
            onAttachedToRunningSession.invoke(!started)
            return
        }

        if (!emulationCanStart.invoke()) {
            return
        }

        if (state == State.STOPPED && live) {
            // NativeLibrary.run must never run twice on the singleton.
            backend.debug("[EmulationFragment] Another game's session is live; waiting for it.")
            bootWhenSessionGone = true
            onWaitingForSession.invoke()
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
        sessionFailed = false
        unshownFailure = null
        sessionGamePath = gamePath
        sessionThread = thread
        thread.start()
    }

    private enum class State {
        STOPPED, RUNNING, PAUSED
    }

    companion object {
        @Volatile
        private var sessionThread: Thread? = null

        @Volatile
        private var sessionFailed = false

        @Volatile
        private var sessionGamePath: String? = null

        @Volatile
        private var unshownFailure: Int? = null

        /**
         * The native load reported [resultCode] through exitEmulationActivity; [shown] is
         * false when no activity was there to show its dialog.
         */
        fun markSessionFailed(resultCode: Int = 0, shown: Boolean = true) {
            unshownFailure = if (shown) null else resultCode
            sessionFailed = true
        }

        /** The result code of a failure nobody has shown yet, once. */
        @Synchronized
        fun takeUnshownFailure(): Int? = unshownFailure.also { unshownFailure = null }
    }
}
