// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu

import java.io.File
import javax.xml.parsers.DocumentBuilderFactory
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element

/**
 * A translation that asks String.format for more arguments than the code passes, or for
 * a different type, throws at runtime in that locale only.
 */
class StringResourcesTest {
    private val resDir = File("src/main/res")
    private val placeholder = Regex("""%(?:(\d+)\$)?[-#+0,(]*\d*(?:\.\d+)?([a-zA-Z%])""")

    private fun placeholders(text: String): Set<Pair<Int, Char>> {
        var index = 0
        return placeholder.findAll(text).mapNotNull {
            val conv = it.groupValues[2][0]
            if (conv == '%' || conv == 'n') {
                null
            } else {
                index++
                val pos = it.groupValues[1].toIntOrNull() ?: index
                pos to (if (conv == 'X') 'x' else conv)
            }
        }.toSet()
    }

    // key: "name" for <string>, "name/quantity" for plural items
    private fun load(file: File): Map<String, String> {
        if (!file.exists()) return emptyMap()
        val doc = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(file)
        val out = mutableMapOf<String, String>()
        val root = doc.documentElement
        val strings = root.getElementsByTagName("string")
        for (i in 0 until strings.length) {
            val e = strings.item(i) as Element
            if (e.getAttribute("formatted") == "false") continue
            out[e.getAttribute("name")] = e.textContent
        }
        val plurals = root.getElementsByTagName("plurals")
        for (i in 0 until plurals.length) {
            val p = plurals.item(i) as Element
            val items = p.getElementsByTagName("item")
            for (j in 0 until items.length) {
                val item = items.item(j) as Element
                out["${p.getAttribute("name")}/${item.getAttribute("quantity")}"] = item.textContent
            }
        }
        return out
    }

    @Test
    fun translationsNeverNeedMoreFormatArgumentsThanTheBaseString() {
        val base = load(File(resDir, "values/strings.xml"))
        assertTrue("base strings not found from ${File(".").absolutePath}", base.isNotEmpty())
        val problems = mutableListOf<String>()
        resDir.listFiles { f -> f.isDirectory && f.name.startsWith("values-") }!!
            .sortedBy { it.name }
            .forEach { dir ->
                load(File(dir, "strings.xml")).forEach { (key, text) ->
                    // plural quantities missing from English (few, many) follow "other"
                    val baseText = base[key]
                        ?: key.substringBefore('/').let { base["$it/other"] }.takeIf { '/' in key }
                        ?: return@forEach
                    val expected = placeholders(baseText)
                    val extra = placeholders(text) - expected
                    if (extra.isNotEmpty()) {
                        problems += "${dir.name} $key: $extra not in $expected"
                    }
                }
            }
        assertTrue(problems.joinToString("\n"), problems.isEmpty())
    }
}
