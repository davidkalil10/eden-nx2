// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.activities

import java.io.File
import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.atomic.AtomicInteger
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.yuzu.yuzu_emu.fragments.EmulationState

/**
 * An activity recreated mid-game (locale, font size, "Don't keep activities") must show the
 * game it replaced, never boot a second one on the native singleton.
 */
class EmulationActivityTest {
    /**
     * Models the process-wide native session: one game at most, surviving any activity.
     * - run() is Java_..._run: the load (InitializeEmulation, holding m_mutex), then
     *   RunEmulation: m_is_running, LoadDiskResources, the pause check, m_is_started, the
     *   wait for a halt, then ShutdownEmulation, which clears the flags.
     * - [pauseFlag] is m_is_paused; the pause check honours it only if the request came
     *   first ([requestBeatsTheLoad]); [systemPaused] is whether the game really is paused.
     * - stop() is HaltEmulation: during the load it waits for m_mutex, and RunEmulation then
     *   sets m_is_running again, so that halt is lost.
     * - [nativeWindow] is m_native_window and [surface] the renderer's, taken at load start
     *   and updated by surfaceChanged only while running.
     */
    private class FakeSession(
        instantLoad: Boolean = true,
        private val loadFails: Boolean = false,
        private val requestBeatsTheLoad: Boolean = true,
        holdCacheLoad: Boolean = false,
        holdExit: Boolean = false,
        private val failureShown: Boolean = true
    ) : EmulationState.Backend<String> {
        @Volatile var running = false

        @Volatile var started = false

        @Volatile var pauseFlag = false

        @Volatile var systemPaused = false

        @Volatile var playTimeRunning = false

        @Volatile var nativeWindow: String? = null

        @Volatile var surface: String? = null
        val boots = AtomicInteger()
        val programChanges = AtomicInteger()
        val pausesWhileLoading = AtomicInteger()
        val unpausesBeforeStart = AtomicInteger()
        val loaded = CountDownLatch(if (instantLoad) 0 else 1)
        val cacheLoaded = CountDownLatch(if (holdCacheLoad) 1 else 0)

        // ShutdownEmulation has run; the thread may still be alive until [exitGate].
        val shutDown = CountDownLatch(1)
        val exitGate = CountDownLatch(if (holdExit) 1 else 0)
        val threads: MutableList<Thread> = Collections.synchronizedList(mutableListOf())
        private val lock = Any()
        private var loadFinished = false
        private val loadFinishedLatch = CountDownLatch(1)

        @Volatile private var ended = CountDownLatch(1)

        @Volatile private var killed = false

        override fun isValid(surface: String) = true
        override fun isRunning() = running
        override fun isPaused() = running && pauseFlag
        override fun isStarted() = running && started
        override fun surfaceChanged(surface: String?) {
            nativeWindow = surface
            if (running) this.surface = surface
        }
        override fun surfaceDestroyed() {
            nativeWindow = null
        }
        override fun run(path: String, programIndex: Int) {
            threads.add(Thread.currentThread())
            ended = CountDownLatch(if (killed) 0 else 1)
            surface = nativeWindow
            boots.incrementAndGet()
            loaded.await()
            if (loadFails) {
                // Java_..._run calls exitEmulationActivity on this thread and returns.
                EmulationState.markSessionFailed(7, shown = failureShown)
                synchronized(lock) { loadFinished = true }
                loadFinishedLatch.countDown()
                return
            }
            synchronized(lock) {
                loadFinished = true
                running = true
            }
            loadFinishedLatch.countDown()
            cacheLoaded.await()
            if (pauseFlag && requestBeatsTheLoad) systemPaused = true
            playTimeRunning = !systemPaused
            started = true
            ended.await()
            // ShutdownEmulation
            running = false
            started = false
            pauseFlag = false
            systemPaused = false
            playTimeRunning = false
            shutDown.countDown()
            exitGate.await()
        }
        override fun changeProgram(path: String, programIndex: Int) {
            programChanges.incrementAndGet()
        }
        override fun pause() {
            // The native pause takes the lock the load holds: on the UI thread, an ANR. Before
            // the start it also runs into the disk shader cache load.
            if (!started) pausesWhileLoading.incrementAndGet()
            pauseFlag = true
            systemPaused = true
            playTimeRunning = false
        }
        override fun stopPlayTime() {
            playTimeRunning = false
        }
        override fun requestPause() {
            pauseFlag = true
        }
        override fun cancelPauseRequest() {
            pauseFlag = false
        }
        override fun unpause() {
            if (!started) unpausesBeforeStart.incrementAndGet()
            pauseFlag = false
            systemPaused = false
            playTimeRunning = true
        }
        override fun stop() {
            if (synchronized(lock) { !loadFinished }) {
                loadFinishedLatch.await()
                return
            }
            running = false
            ended.countDown()
        }

        /** Test teardown only: ends every phase so no session thread outlives the test. */
        fun kill() {
            killed = true
            loaded.countDown()
            cacheLoaded.countDown()
            ended.countDown()
            exitGate.countDown()
        }
        override fun debug(message: String) {}
        override fun warning(message: String) {}
    }

    private val sessions = mutableListOf<FakeSession>()

    private val states = mutableListOf<EmulationState<String>>()

    private fun session(
        instantLoad: Boolean = true,
        loadFails: Boolean = false,
        requestBeatsTheLoad: Boolean = true,
        holdCacheLoad: Boolean = false,
        holdExit: Boolean = false,
        failureShown: Boolean = true
    ) = FakeSession(
        instantLoad,
        loadFails,
        requestBeatsTheLoad,
        holdCacheLoad,
        holdExit,
        failureShown
    ).also { sessions.add(it) }

    // The session thread is process-wide: one left alive would be attached to by the next test.
    @After
    fun endSessions() {
        sessions.forEach { it.kill() }
        val threads = sessions.flatMap { synchronized(it.threads) { it.threads.toList() } } +
            states.mapNotNull { it.emulationThread }
        threads.forEach { it.join(5000) }
        assertTrue("a session thread outlived the test", threads.none { it.isAlive })
        sessions.clear()
        states.clear()
    }

    private fun fragmentState(
        session: FakeSession,
        stoppingForSwap: Boolean = false,
        gamePath: String = "game.nsp",
        onAttached: (Boolean) -> Unit = {}
    ) = EmulationState(
        gamePath = gamePath,
        emulationCanStart = { !stoppingForSwap },
        canAttach = { !stoppingForSwap },
        onAttachedToRunningSession = onAttached,
        backend = session
    ).also { states.add(it) }

    private fun awaitBoot(session: FakeSession, boots: Int = 1) {
        val deadline = System.currentTimeMillis() + 5000
        while (session.boots.get() < boots) {
            assertTrue("the game never booted", System.currentTimeMillis() < deadline)
            Thread.sleep(5)
        }
    }

    private fun awaitRunning(session: FakeSession) {
        val deadline = System.currentTimeMillis() + 5000
        while (!session.started) {
            assertTrue("the game never ran", System.currentTimeMillis() < deadline)
            Thread.sleep(5)
        }
    }

    private fun bootedGame(session: FakeSession): EmulationState<String> {
        val first = fragmentState(session)
        first.newSurface("first")
        awaitRunning(session)
        assertEquals(1, session.boots.get())
        return first
    }

    @Test
    fun recreatedActivityShowsTheRunningGamePausedInsteadOfBootingIt() {
        val session = session()
        val old = bootedGame(session)

        // The old activity goes away: its surface is destroyed, which pauses the game.
        old.clearSurface()
        assertFalse(
            EmulationActivity.shouldDeferLaunchForSwap(
                isRecreated = true,
                hasSession = true,
                isSwapIntent = true
            )
        )

        val attached = mutableListOf<Boolean>()
        val restored = fragmentState(session) { attached.add(it) }
        restored.newSurface("second")

        assertEquals(1, session.boots.get())
        assertTrue(restored.isPaused)
        assertEquals("second", session.surface)
        assertEquals(listOf(false), attached)

        restored.resume()
        assertTrue(restored.isRunning)
        assertFalse(session.isPaused())
        assertEquals(1, session.boots.get())
    }

    @Test
    fun runningGameFoundUnpausedIsPausedOnAttach() {
        val session = session()
        bootedGame(session)
        assertFalse(session.isPaused())

        val restored = fragmentState(session)
        restored.run()
        restored.newSurface("second")

        assertEquals(1, session.boots.get())
        assertTrue(restored.isPaused)
        assertTrue(session.isPaused())
    }

    @Test
    fun activityRecreatedDuringTheLoadAttachesAndPausesOnceTheGameRuns() {
        // The request lands after the load's own check: only the watcher's pause holds it.
        val session = session(instantLoad = false, requestBeatsTheLoad = false)
        val old = fragmentState(session)
        old.newSurface("first")
        awaitBoot(session)
        old.clearSurface()

        val attached = mutableListOf<Boolean>()
        val restored = fragmentState(session) { attached.add(it) }
        restored.newSurface("second")
        assertEquals(1, session.boots.get())
        assertEquals(listOf(true), attached)
        assertTrue(restored.isPaused && restored.isPauseDeferred)
        assertEquals(0, session.pausesWhileLoading.get())
        assertTrue("the attach did not ask the load to pause", session.pauseFlag)

        session.loaded.countDown()
        awaitRunning(session)
        assertEquals("first", session.surface)
        assertEquals(EmulationState.DeferredPause.Started, restored.reconcileDeferredPause())
        assertFalse(restored.isPauseDeferred)
        assertTrue("the game runs unpaused", session.systemPaused)
        assertFalse(session.playTimeRunning)
        assertEquals("the new surface was not re-sent", "second", session.surface)
        assertEquals(1, session.boots.get())
    }

    @Test
    fun attachDuringTheLoadRequestsThePauseItself() {
        // The old state never paused: its surface teardown was skipped (PiP, a newer fragment).
        val session = session(instantLoad = false)
        fragmentState(session).newSurface("first")
        awaitBoot(session)
        assertFalse(session.pauseFlag)

        val restored = fragmentState(session)
        restored.newSurface("second")
        assertTrue(restored.isPauseDeferred)
        assertTrue("the attach did not ask the load to pause", session.pauseFlag)
        session.loaded.countDown()
        awaitRunning(session)
        assertTrue(session.systemPaused)
    }

    @Test
    fun sessionThatEndsAfterTheAttachBootsAgainWithoutItsPauseRequest() {
        // The attach lands after ShutdownEmulation cleared m_is_paused, while the thread
        // still unwinds: its request would otherwise pause the next boot.
        val dying = session(holdExit = true)
        bootedGame(dying)
        dying.stop()
        dying.shutDown.await()
        val restored = fragmentState(dying)
        restored.newSurface("second")
        assertTrue(restored.isPauseDeferred)
        assertTrue(dying.pauseFlag)

        val first = restored.emulationThread!!
        dying.exitGate.countDown()
        first.join()
        assertEquals(EmulationState.DeferredPause.None, restored.reconcileDeferredPause())
        awaitBoot(dying, 2)
        awaitRunning(dying)
        assertTrue(restored.isRunning)
        assertFalse("the dead session's pause request reached the new one", dying.pauseFlag)
        assertFalse(dying.systemPaused)
    }

    @Test
    fun attachingToAGamePausedNativelyStopsItsPlayTime() {
        val session = session()
        bootedGame(session)
        session.pauseFlag = true // e.g. paused from PiP, play time still counting
        session.systemPaused = true
        assertTrue(session.playTimeRunning)

        val restored = fragmentState(session)
        restored.newSurface("second")
        assertTrue(restored.isPaused)
        assertFalse(session.playTimeRunning)
    }

    @Test
    fun aStateForAnotherGameDoesNotAttachToTheRunningOne() {
        val session = session()
        bootedGame(session)
        val attached = mutableListOf<Boolean>()
        val other = fragmentState(session, gamePath = "other.nsp") { attached.add(it) }
        other.newSurface("second")

        assertTrue(attached.isEmpty())
        // Never a second NativeLibrary.run on the singleton while the first session lives.
        assertTrue(other.isStopped)
        assertEquals(EmulationState.DeferredPause.Loading, other.reconcileDeferredPause())
        Thread.sleep(50)
        assertEquals(1, session.boots.get())

        val first = other.emulationThread!!
        session.stop()
        first.join()
        assertEquals(EmulationState.DeferredPause.None, other.reconcileDeferredPause())
        assertTrue(other.isRunning)
        // The boot happens on the new session thread; wait for it, not a racy read.
        awaitBoot(session, 2)
    }

    @Test
    fun aFailureWithNoActivityToShowItIsHandedToTheWatcherOnce() {
        val failing = session(instantLoad = false, loadFails = true, failureShown = false)
        fragmentState(failing).newSurface("first")
        val restored = fragmentState(failing)
        restored.newSurface("second")

        failing.loaded.countDown()
        restored.emulationThread?.join()
        assertEquals(EmulationState.DeferredPause.Failed, restored.reconcileDeferredPause())
        assertEquals(7, EmulationState.takeUnshownFailure())
        assertEquals(null, EmulationState.takeUnshownFailure())
        assertTrue(restored.isStopped)
        assertEquals(1, failing.boots.get())
    }

    @Test
    fun theFragmentShowsAFailureTheWatcherReports() {
        val fragment = File("src/main/java/org/yuzu/yuzu_emu/fragments/EmulationFragment.kt")
            .readText()
        val watch = fragment.substringAfter("private val deferredPauseWatch")
            .substringBefore("\n    }\n")
        val branch = watch.substringAfter("EmulationState.DeferredPause.Failed ->")
        assertTrue(branch.contains("EmulationState.takeUnshownFailure()"))
        assertTrue(branch.contains("NativeLibrary.exitEmulationActivity(it)"))
    }

    @Test
    fun aFailedLoadDoesNotBlockTheRebootOfALaterSession() {
        val failing = session(loadFails = true)
        fragmentState(failing).newSurface("first")
        awaitBoot(failing)
        synchronized(failing.threads) { failing.threads.toList() }.forEach { it.join() }

        // A later session starts clean: its own end must still lead to a reboot.
        val dying = session(holdExit = true)
        bootedGame(dying)
        dying.stop()
        dying.shutDown.await()
        val restored = fragmentState(dying)
        restored.newSurface("second")
        val first = restored.emulationThread!!
        dying.exitGate.countDown()
        first.join()
        assertEquals(EmulationState.DeferredPause.None, restored.reconcileDeferredPause())
        awaitBoot(dying, 2)
        assertTrue(restored.isRunning)
    }

    @Test
    fun stoppingDuringADeferredPauseEndsTheWatch() {
        val session = session(instantLoad = false)
        val state = fragmentState(session)
        state.newSurface("first")
        awaitBoot(session)
        state.pause()
        assertTrue(state.isPauseDeferred)

        // HaltEmulation waits for the load to release m_mutex, so the stop returns only then.
        Thread {
            Thread.sleep(50)
            session.loaded.countDown()
        }.start()
        state.stop()
        assertFalse(state.isPauseDeferred)
        assertEquals(EmulationState.DeferredPause.None, state.reconcileDeferredPause())
        assertEquals(1, session.boots.get())
        assertTrue(state.isStopped)
    }

    @Test
    fun failedLoadAttachedToIsNotBootedAgain() {
        val failing = session(instantLoad = false, loadFails = true)
        fragmentState(failing).newSurface("first")
        val restored = fragmentState(failing)
        restored.newSurface("second")
        assertTrue(restored.isPauseDeferred)

        failing.loaded.countDown()
        restored.emulationThread?.join()
        assertEquals(EmulationState.DeferredPause.Failed, restored.reconcileDeferredPause())
        restored.emulationThread?.join()
        assertEquals(1, failing.boots.get())
        assertTrue(restored.isStopped)
    }

    @Test
    fun noUnpauseOverlapsTheDiskShaderCacheLoad() {
        val session = session(holdCacheLoad = true)
        val state = fragmentState(session)
        state.newSurface("first")
        val deadline = System.currentTimeMillis() + 5000
        while (!session.running) {
            assertTrue(System.currentTimeMillis() < deadline)
            Thread.sleep(5)
        }
        // m_is_running is set, LoadDiskResources still runs: home, then the play icon.
        state.pause()
        assertTrue(state.isPauseDeferred)
        state.resume()
        assertEquals(EmulationState.DeferredPause.Loading, state.reconcileDeferredPause())
        assertEquals(0, session.pausesWhileLoading.get())
        assertEquals(0, session.unpausesBeforeStart.get())

        session.cacheLoaded.countDown()
        awaitRunning(session)
        assertEquals(EmulationState.DeferredPause.Started, state.reconcileDeferredPause())
        state.resume()
        assertTrue(state.isRunning)
        assertEquals(0, session.unpausesBeforeStart.get())
    }

    @Test
    fun watcherWaitsOutTheLoadThenReportsTheGamePausedOnce() {
        val session = session(instantLoad = false)
        val state = fragmentState(session)
        state.newSurface("first")
        state.pause()
        assertTrue(session.pauseFlag)
        assertEquals(EmulationState.DeferredPause.Loading, state.reconcileDeferredPause())
        assertEquals(EmulationState.DeferredPause.Loading, state.reconcileDeferredPause())
        assertFalse(session.systemPaused)

        session.loaded.countDown()
        awaitRunning(session)
        assertEquals(EmulationState.DeferredPause.Started, state.reconcileDeferredPause())
        assertTrue(state.isPaused && session.systemPaused)
        assertEquals(EmulationState.DeferredPause.None, state.reconcileDeferredPause())
        state.resume()
        assertTrue(state.isRunning)
    }

    @Test
    fun programChangeOnARestoredStateWaitsForTheThreadTheFirstStarted() {
        val session = session()
        bootedGame(session)
        val restored = fragmentState(session)
        restored.newSurface("second")

        val change = Thread { restored.changeProgram(1) }
        change.start()
        change.join(200)
        assertTrue("changeProgram did not wait for the running game", change.isAlive)
        assertEquals(0, session.programChanges.get())

        session.stop()
        change.join()
        restored.emulationThread?.join()
        assertEquals(1, session.programChanges.get())
    }

    @Test
    fun fragmentStoppingForSwapNeitherAttachesNorBoots() {
        val session = session()
        bootedGame(session)
        var attached = 0
        val stopping = fragmentState(session, stoppingForSwap = true) { attached++ }
        stopping.newSurface("second")

        assertTrue(stopping.isStopped)
        assertEquals(0, attached)
        assertEquals(1, session.boots.get())
    }

    @Test
    fun newGameIntentOverRunningSessionStillSwaps() {
        assertTrue(
            EmulationActivity.shouldDeferLaunchForSwap(
                isRecreated = false,
                hasSession = true,
                isSwapIntent = true
            )
        )
    }

    @Test
    fun withoutASessionTheGameBoots() {
        val session = session()
        val state = bootedGame(session)
        assertTrue(state.isRunning)
        assertEquals("first", session.surface)
    }

    private val activitySource: String by lazy {
        File("src/main/java/org/yuzu/yuzu_emu/activities/EmulationActivity.kt").readText()
    }

    // The argument list of each call to [name], with nested parentheses kept.
    private fun calls(source: String, name: String): List<Pair<String, String>> =
        Regex("""([\w.]*)\b$name\(""").findAll(source).map { match ->
            var depth = 1
            var end = match.range.last + 1
            while (depth > 0) {
                when (source[end]) {
                    '(' -> depth++
                    ')' -> depth--
                }
                end++
            }
            match.groupValues[1] to source.substring(match.range.last + 1, end - 1)
        }.toList()

    @Test
    fun pictureInPictureReceiverIsNotExportedOnAnyApiLevel() {
        val registrations = calls(activitySource, "registerReceiver")
        assertTrue("no receiver registration found", registrations.isNotEmpty())
        registrations.forEach { (receiver, args) ->
            // Context.registerReceiver without the flag exports the receiver below API 33.
            assertEquals("ContextCompat.", receiver)
            assertTrue(args, args.contains("RECEIVER_NOT_EXPORTED"))
        }
    }

    private fun body(source: String, declaration: String): String =
        source.substringAfter(declaration).substringBefore("\n    }\n")

    @Test
    fun pictureInPictureActionsReachTheLiveFragmentDuringASwap() {
        val receiver = body(activitySource, "private var pictureInPictureReceiver")
        assertTrue(receiver.contains("currentEmulationFragment()"))
        assertFalse(receiver.contains("firstOrNull()"))
        val pick = body(activitySource, "internal fun currentEmulationFragment()")
        assertTrue(pick.contains("asReversed()") && pick.contains("!it.isRemoving"))
    }

    @Test
    fun statsOverlaysKeepRunningInPictureInPictureAndStopWhenHidden() {
        val fragment = File("src/main/java/org/yuzu/yuzu_emu/fragments/EmulationFragment.kt")
            .readText()
        val onPause = body(fragment, "override fun onPause()")
        val gate = onPause.indexOf("isInPictureInPictureMode != true")
        assertTrue("onPause stops the overlays in picture-in-picture", gate >= 0)
        assertTrue(onPause.indexOf("stopStatsOverlays()") > gate)
        assertTrue(body(fragment, "override fun onStop()").contains("stopStatsOverlays()"))
    }

    @Test
    fun surfaceTeardownComparesWithTheLiveFragment() {
        val fragment = File("src/main/java/org/yuzu/yuzu_emu/fragments/EmulationFragment.kt")
            .readText()
        val lines = body(fragment, "private fun hasNewerEmulationFragment()").lines()
            .map { it.trim() }
        assertTrue(
            "val current = emulationActivity?.currentEmulationFragment() ?: return false" in lines
        )
        assertTrue("return current !== this" in lines)
    }

    @Test
    fun touchAndControllerCallbacksReachTheLiveFragment() {
        assertFalse(activitySource.contains("fragments?.firstOrNull()"))
        listOf(
            "override fun dispatchTouchEvent(",
            "private fun notifyControllerInput(",
            "private fun notifyPhysicalControllerState("
        ).forEach {
            val lines = body(activitySource, it).lines().map { line -> line.trim() }
            assertTrue(it, "val emulationFragment = currentEmulationFragment()" in lines)
        }
    }
}
