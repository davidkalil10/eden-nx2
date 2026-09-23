// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features

import java.io.File
import java.nio.file.Files
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

class DocumentProviderTest {
    private lateinit var tmp: File
    private lateinit var base: File

    @Before
    fun setUp() {
        tmp = Files.createTempDirectory("docs").toFile()
        base = File(tmp, "files").apply { mkdirs() }
        File(base, "config").mkdirs()
    }

    @After
    fun tearDown() {
        tmp.deleteRecursively()
    }

    @Test
    fun documentIdsResolveInsideTheBase() {
        val root = base.canonicalFile
        assertEquals(root, DocumentProvider.resolveDocumentFile(base, "root/"))
        assertEquals(root, DocumentProvider.resolveDocumentFile(base, "root"))
        assertEquals(
            File(root, "config/config.ini"),
            DocumentProvider.resolveDocumentFile(base, "root/config/config.ini")
        )
    }

    @Test
    fun documentIdsCannotEscapeTheBase() {
        assertNull(DocumentProvider.resolveDocumentFile(base, "root/../secret"))
        assertNull(DocumentProvider.resolveDocumentFile(base, "root/config/../../secret"))
        assertNull(DocumentProvider.resolveDocumentFile(base, "rootx/config"))
        assertNull(DocumentProvider.resolveDocumentFile(base, "other/config"))
    }

    @Test
    fun childTestRespectsPathSegments() {
        assertTrue(DocumentProvider.isChildDocumentId("root/", "root/config"))
        assertTrue(DocumentProvider.isChildDocumentId("root/config", "root/config/a.ini"))
        assertFalse(DocumentProvider.isChildDocumentId("root/config", "root/configx"))
        assertFalse(DocumentProvider.isChildDocumentId("root/", "root/../../data"))
        assertFalse(DocumentProvider.isChildDocumentId("root/config", "root/config"))
    }

    @Test
    fun displayNamesStayOneComponent() {
        assertEquals("save.bin", DocumentProvider.sanitizeDisplayName("save.bin"))
        assertNull(DocumentProvider.sanitizeDisplayName("../shared_prefs/x.xml"))
        assertNull(DocumentProvider.sanitizeDisplayName(".."))
        assertNull(DocumentProvider.sanitizeDisplayName(""))
    }

    @Test
    fun conflictNamesKeepTheExtension() {
        assertEquals("a (1).txt", DocumentProvider.conflictFreeName("a.txt", 1))
        assertEquals("README (2)", DocumentProvider.conflictFreeName("README", 2))
        assertEquals(".nomedia (1)", DocumentProvider.conflictFreeName(".nomedia", 1))
    }

    @Test
    fun unresolvableIdsAreNotFoundInsteadOfThrowing() {
        assertNull(DocumentProvider.resolveDocumentFile(base, "root/config\u0000.ini"))
        val loop = File(base, "loop")
        Files.createSymbolicLink(loop.toPath(), File(base, "loop2").toPath())
        Files.createSymbolicLink(File(base, "loop2").toPath(), loop.toPath())
        assertNull(DocumentProvider.resolveDocumentFile(base, "root/loop/x"))
    }

    @Test
    fun rootIsRefusedWhereItWouldBeRenamedMovedOrDeleted() {
        assertNull(DocumentProvider.resolveDocumentFile(base, "root", allowRoot = false))
        assertNull(DocumentProvider.resolveDocumentFile(base, "root/", allowRoot = false))
        assertNull(DocumentProvider.resolveDocumentFile(base, "root/config/..", allowRoot = false))
        assertEquals(
            File(base.canonicalFile, "config"),
            DocumentProvider.resolveDocumentFile(base, "root/config", allowRoot = false)
        )
    }

    @Test
    fun everyMutatingCallRefusesTheRoot() {
        val source = File("src/main/java/org/yuzu/yuzu_emu/features/DocumentProvider.kt")
            .readText()
        // The statement that resolves the document acted on, not merely any getFile call.
        mapOf(
            "deleteDocument" to "val file = getFile(documentId!!, allowRoot = false)",
            "removeDocument" to "val file = getFile(documentId, allowRoot = false)",
            "renameDocument" to "val sourceFile = getFile(documentId!!, allowRoot = false)",
            "moveDocument" to "getFile(sourceDocumentId, allowRoot = false)"
        ).forEach { (function, statement) ->
            val start = source.indexOf("override fun $function(")
            assertTrue("$function not found", start >= 0)
            val end = source.indexOf("\n    }\n", start)
            val text = source.substring(start, end)
            val lines = text.lines().map { it.trim() }
            assertTrue("$function can act on the root", statement in lines)
            // Any other lookup of a document id may only be the parent's.
            val lookups = Regex("""getFile\(([^)]*)\)""").findAll(text).map { it.groupValues[1] }
            val unguarded = lookups.filter {
                !it.endsWith("allowRoot = false") && !it.startsWith("parentDocumentId")
            }
            assertTrue("$function resolves $unguarded without refusing the root", unguarded.none())
        }
    }

    @Test
    fun providerCallsGoThroughTheTestedHelpers() {
        val source = File("src/main/java/org/yuzu/yuzu_emu/features/DocumentProvider.kt")
            .readText()
        fun body(name: String) = source.substringAfter(name).substringBefore("\n    }\n")
        assertTrue(body("private fun getFile(").contains("resolveDocumentFile(baseDirectory"))
        assertTrue(body("override fun isChildDocument(").contains("isChildDocumentId("))
        val resolve = body("private fun File.resolveWithoutConflict(")
        assertTrue(resolve.contains("sanitizeDisplayName("))
        assertTrue(resolve.contains("conflictFreeName("))
        assertTrue(body("override fun renameDocument(").contains("sanitizeDisplayName("))
    }
}
