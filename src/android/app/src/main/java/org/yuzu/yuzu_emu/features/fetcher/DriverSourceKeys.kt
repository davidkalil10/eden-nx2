// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.fetcher

object DriverSourceKeys {

    fun builtinGithubKeys(paths: Collection<String>): Set<String> =
        paths.map { key(DriverReleaseBackend.GITHUB, it) }.toSet()

    fun key(backend: DriverReleaseBackend, repoPath: String): String =
        "${backend.name.lowercase()}:${repoPath.trim().lowercase()}"

    fun key(source: DriverReleaseSource): String = key(source.backend, source.repoPath)

    fun key(link: CustomDriverLink): String? = link.resolveReleaseSource()?.let { key(it) }

    fun key(parsed: ParsedDriverLink): String? = when (parsed) {
        is ParsedDriverLink.Direct -> "direct:${parsed.normalizedUrl.trim().lowercase()}"
        is ParsedDriverLink.ReleaseRepo -> key(parsed.source)
        ParsedDriverLink.Invalid -> null
    }

    fun isDuplicate(
        parsed: ParsedDriverLink,
        existingLinks: List<CustomDriverLink>,
        builtinReleaseKeys: Set<String>,
        exceptLinkId: String? = null,
    ): Boolean {
        val candidate = key(parsed) ?: return false
        if (builtinReleaseKeys.contains(candidate)) {
            return true
        }
        return existingLinks.any {
            it.id != exceptLinkId && key(it) == candidate
        }
    }
}