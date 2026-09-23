// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element
import org.yuzu.yuzu_emu.features.settings.model.view.SettingsItem

/**
 * The settings dialogs pair a names array with a values array by position, and the values are
 * native enum indices; a mismatch stores a different option than the one the user picked.
 */
class SettingsArraysTest {
    private val arrays: Map<String, List<String>> by lazy {
        val doc = DocumentBuilderFactory.newInstance().newDocumentBuilder()
            .parse(File("src/main/res/values/arrays.xml"))
        val out = mutableMapOf<String, List<String>>()
        for (tag in listOf("string-array", "integer-array", "array")) {
            val nodes = doc.documentElement.getElementsByTagName(tag)
            for (i in 0 until nodes.length) {
                val e = nodes.item(i) as Element
                val items = e.getElementsByTagName("item")
                out[e.getAttribute("name")] =
                    (0 until items.length).map { items.item(it).textContent.trim() }
            }
        }
        out
    }

    private fun enumMembers(name: String): List<String> {
        val header = File("../../common/settings_enums.h").readText()
        val body = Regex("""ENUM\(\s*$name\s*,([^)]*)\)""").find(header)?.groupValues?.get(1)
            ?: error("enum $name not found")
        return body.split(',').map { it.trim() }.filter { it.isNotEmpty() }
    }

    private fun normalize(s: String) = s.replace("_", "").lowercase()

    // Every names/values pair used in Kotlin, found by scanning the sources.
    private fun usedPairs(): Set<Pair<String, String>> {
        val pair = Regex(
            """R\.array\.(\w+)\s*,\s*(?:valuesId\s*=\s*)?R\.array\.(\w+)"""
        )
        return File("src/main/java").walk().filter { it.extension == "kt" }
            .flatMap { f -> pair.findAll(f.readText()).map { it.groupValues[1] to it.groupValues[2] } }
            .toSet()
    }

    @Test
    fun namesAndValuesArraysHaveTheSameLength() {
        val pairs = usedPairs()
        assertTrue("no array pairs found", pairs.isNotEmpty())
        val problems = pairs.mapNotNull { (names, values) ->
            val n = arrays[names] ?: return@mapNotNull "$names missing"
            val v = arrays[values] ?: return@mapNotNull "$values missing"
            if (n.size != v.size) "$names has ${n.size} items, $values has ${v.size}" else null
        }
        assertTrue(problems.joinToString("\n"), problems.isEmpty())
    }

    @Test
    fun scalingFilterNamesMatchTheNativeEnum() {
        val members = enumMembers("ScalingFilter")
        val names = arrays.getValue("rendererScalingFilterNames")
        val values = arrays.getValue("rendererScalingFilterValues").map { it.toInt() }
        assertEquals(members.size, names.size)
        names.zip(values).forEach { (name, value) ->
            val label = name.removePrefix("@string/scaling_filter_")
            assertEquals("entry $name stores $value", normalize(members[value]), normalize(label))
        }
    }

    @Test
    fun everyAppLanguageHasALocaleCode() {
        assertEquals(
            arrays.getValue("appLanguageNames").size,
            YuzuApplication.LANGUAGE_CODES.size
        )
        arrays.getValue("appLanguageValues").forEachIndexed { i, v -> assertEquals(i, v.toInt()) }
    }

    @Test
    fun resolutionWarningStartsAtTwoTimesAndCoversTheHighestChoice() {
        val members = enumMembers("ResolutionSetup")
        assertEquals(members.indexOf("Res2X"), SettingsItem.RESOLUTION_WARN_FROM)
        assertEquals(
            arrays.getValue("rendererResolutionValues").last().toInt(),
            SettingsItem.RESOLUTION_WARN_TO
        )
    }
}
