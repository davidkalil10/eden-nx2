// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.fetcher

import androidx.core.net.toUri
import java.net.URLEncoder

sealed class ParsedDriverLink {
    data class Direct(val normalizedUrl: String) : ParsedDriverLink()
    data class ReleaseRepo(
        val normalizedUrl: String,
        val source: DriverReleaseSource,
    ) : ParsedDriverLink()

    data object Invalid : ParsedDriverLink()
}

object DriverFetcherLinkParser {

    fun normalizeUrlInput(raw: String): String {
        val trimmed = raw.trim()
        if (trimmed.isEmpty()) {
            return trimmed
        }
        return when {
            trimmed.contains("://") -> trimmed
            trimmed.startsWith("//") -> "https:$trimmed"
            else -> "https://$trimmed"
        }
    }

    fun parse(raw: String): ParsedDriverLink {
        val normalized = normalizeUrlInput(raw)
        if (normalized.isEmpty()) {
            return ParsedDriverLink.Invalid
        }

        val uri = normalized.toUri()
        val scheme = uri.scheme?.lowercase()
        if (scheme != "http" && scheme != "https") {
            return ParsedDriverLink.Invalid
        }
        if (uri.host.isNullOrBlank()) {
            return ParsedDriverLink.Invalid
        }

        val path = uri.path.orEmpty()
        if (path.contains("/releases/download/", ignoreCase = true)) {
            return ParsedDriverLink.Direct(normalized)
        }

        parseGithubRepoPath(uri)?.let { repoPath ->
            return ParsedDriverLink.ReleaseRepo(
                normalized,
                DriverReleaseSource(
                    DriverReleaseBackend.GITHUB,
                    repoPath,
                    "https://api.github.com",
                    normalized,
                ),
            )
        }

        parseGitLabProject(uri)?.let { source ->
            return ParsedDriverLink.ReleaseRepo(normalized, source)
        }

        parseGiteaLikeRepo(uri)?.let { source ->
            return ParsedDriverLink.ReleaseRepo(normalized, source)
        }

        return ParsedDriverLink.Direct(normalized)
    }

    private fun parseGithubRepoPath(uri: android.net.Uri): String? {
        val host = uri.host?.lowercase() ?: return null
        if (host != "github.com" && host != "www.github.com") {
            return null
        }

        val segments = pathSegments(uri.path)
        if (segments.size < 2) {
            return null
        }

        val owner = segments[0]
        val repo = segments[1]
        val third = segments.getOrNull(2)?.lowercase()
        if (third == null || third == "releases" || third == "tags") {
            return "$owner/$repo"
        }

        return null
    }

    private fun parseGitLabProject(uri: android.net.Uri): DriverReleaseSource? {
        val host = uri.host?.lowercase() ?: return null
        if (!host.contains("gitlab")) {
            return null
        }

        val segments = pathSegments(uri.path)
        if (segments.size < 2) {
            return null
        }
        val dashIdx = segments.indexOf("-")
        val projectParts = if (dashIdx >= 0) segments.subList(0, dashIdx) else segments
        if (projectParts.size < 2) {
            return null
        }

        val projectPath = projectParts.joinToString("/")
        val scheme = uri.scheme ?: "https"
        val apiBase = "$scheme://$host/api/v4"
        val webUrl = "$scheme://$host/$projectPath"
        return DriverReleaseSource(
            DriverReleaseBackend.GITLAB,
            projectPath,
            apiBase,
            webUrl,
        )
    }

    /**
     * Codeberg, Forgejo, Eden Forgejo, and other Gitea-compatible hosts.
     */
    private fun parseGiteaLikeRepo(uri: android.net.Uri): DriverReleaseSource? {
        val host = uri.host?.lowercase() ?: return null
        if (host == "github.com" || host == "www.github.com" || host.contains("gitlab")) {
            return null
        }

        val segments = pathSegments(uri.path)
        if (segments.size < 2) {
            return null
        }

        val third = segments.getOrNull(2)?.lowercase()
        if (third != null && third !in setOf("releases", "src", "wiki", "packages")) {
            return null
        }

        val owner = segments[0]
        val repo = segments[1]
        val scheme = uri.scheme ?: "https"
        val apiBase = "$scheme://$host/api/v1"
        val webUrl = "$scheme://$host/$owner/$repo"
        return DriverReleaseSource(
            DriverReleaseBackend.GITEA,
            "$owner/$repo",
            apiBase,
            webUrl,
        )
    }

    private fun pathSegments(path: String?): List<String> =
        path?.trim('/')?.split('/')?.filter { it.isNotEmpty() } ?: emptyList()

    fun gitLabProjectId(repoPath: String): String =
        URLEncoder.encode(repoPath, Charsets.UTF_8.name())
}