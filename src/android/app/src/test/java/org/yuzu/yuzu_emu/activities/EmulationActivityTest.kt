// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.activities

import java.io.File
import java.util.concurrent.atomic.AtomicInteger
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
    /** Models the process-wide native session: one game at most, surviving any activity. */
    private class FakeSession : EmulationState.Backend<String> {
        @Volatile var running = false

        @Volatile var paused = false
        val boots = AtomicInteger()
        val programChanges = AtomicInteger()
        var surface: String? = null

        override fun isValid(surface: String) = true
        override fun isRunning() = running
        override fun isPaused() = running && paused
        override fun surfaceChanged(surface: String?) {
            this.surface = surface
        }
        override fun surfaceDestroyed() {
            surface = null
        }
        override fun run(path: String, programIndex: Int) {
            boots.incrementAndGet()
            running = true
        }
        override fun changeProgram(path: String, programIndex: Int) {
            programChanges.incrementAndGet()
        }
        override fun pause() {
            paused = true
        }
        override fun unpause() {
            paused = false
        }
        override fun stop() {
            running = false
        }
        override fun debug(message: String) {}
        override fun warning(message: String) {}
    }

    private fun fragmentState(
        session: FakeSession,
        stoppingForSwap: Boolean = false,
        onAttached: () -> Unit = {}
    ) = EmulationState(
        gamePath = "game.nsp",
        emulationCanStart = { !stoppingForSwap },
        canAttach = { !stoppingForSwap },
        onAttachedToRunningSession = onAttached,
        backend = session
    )

    private fun EmulationState<String>.awaitThread() = emulationThread?.join()

    private fun bootedGame(session: FakeSession): EmulationState<String> {
        val first = fragmentState(session)
        first.newSurface("first")
        first.awaitThread()
        assertEquals(1, session.boots.get())
        return first
    }

    @Test
    fun recreatedActivityShowsTheRunningGamePausedInsteadOfBootingIt() {
        val session = FakeSession()
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

        var attached = 0
        val restored = fragmentState(session) { attached++ }
        restored.newSurface("second")
        restored.awaitThread()

        assertEquals(1, session.boots.get())
        assertTrue(restored.isPaused)
        assertEquals("second", session.surface)
        assertEquals(1, attached)

        restored.resume()
        assertTrue(restored.isRunning)
        assertFalse(session.isPaused())
        assertEquals(1, session.boots.get())
    }

    @Test
    fun runningGameFoundUnpausedIsPausedOnAttach() {
        val session = FakeSession()
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
    fun programChangeAfterAttachWaitsForTheSessionThread() {
        val session = FakeSession()
        bootedGame(session)
        val restored = fragmentState(session)
        restored.newSurface("second")

        restored.changeProgram(1)
        restored.awaitThread()

        assertEquals(1, session.programChanges.get())
        assertEquals(1, session.boots.get())
    }

    @Test
    fun fragmentStoppingForSwapNeitherAttachesNorBoots() {
        val session = FakeSession()
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
        val session = FakeSession()
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
}
