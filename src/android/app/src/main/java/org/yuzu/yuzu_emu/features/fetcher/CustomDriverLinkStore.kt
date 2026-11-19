// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.fetcher

import android.content.Context
import androidx.core.content.edit
import androidx.preference.PreferenceManager
import com.fasterxml.jackson.module.kotlin.jacksonObjectMapper
import com.fasterxml.jackson.module.kotlin.readValue
import androidx.core.net.toUri
import java.util.UUID

data class CustomDriverLink(
    val id: String = UUID.randomUUID().toString(),
    val label: String,
    val url: String,
    /** Legacy GitHub-only field; prefer [releaseRepoPath]. */
    val githubRepo: String? = null,
    val releaseBackend: String? = null,
    val releaseRepoPath: String? = null,
    val releaseApiBase: String? = null,
)

fun CustomDriverLink.resolveReleaseSource(): DriverReleaseSource? {
    val path = releaseRepoPath?.takeIf { it.isNotBlank() } ?: githubRepo?.takeIf { it.isNotBlank() }
        ?: return null

    val backend = when (releaseBackend?.uppercase()) {
        DriverReleaseBackend.GITLAB.name -> DriverReleaseBackend.GITLAB
        DriverReleaseBackend.GITEA.name -> DriverReleaseBackend.GITEA
        else -> DriverReleaseBackend.GITHUB
    }

    val apiBase = releaseApiBase?.takeIf { it.isNotBlank() }
        ?: when (backend) {
            DriverReleaseBackend.GITHUB -> "https://api.github.com"
            DriverReleaseBackend.GITLAB -> {
                val host = DriverFetcherLinkParser.normalizeUrlInput(url).toUri().host
                    ?: return null
                "https://${host}/api/v4"
            }
            DriverReleaseBackend.GITEA -> {
                val normalized = DriverFetcherLinkParser.normalizeUrlInput(url)
                val uri = normalized.toUri()
                "${uri.scheme}://${uri.host}/api/v1"
            }
        }

    return DriverReleaseSource(
        backend = backend,
        repoPath = path,
        apiBaseUrl = apiBase,
        webUrl = url,
    )
}

fun CustomDriverLink.isReleaseCatalogSource(): Boolean = resolveReleaseSource() != null

enum class CustomDriverLinkCardState {
    CATALOG,
    DIRECT_DOWNLOAD,
    INVALID,
}

fun CustomDriverLink.cardState(): CustomDriverLinkCardState {
    when (val parsed = DriverFetcherLinkParser.parse(url)) {
        ParsedDriverLink.Invalid -> return CustomDriverLinkCardState.INVALID

        is ParsedDriverLink.ReleaseRepo -> {
            return if (resolveReleaseSource() != null) {
                CustomDriverLinkCardState.CATALOG
            } else {
                CustomDriverLinkCardState.INVALID
            }
        }

        is ParsedDriverLink.Direct -> {
            return try {
                val normalized = parsed.normalizedUrl
                val path = java.net.URL(normalized).path.lowercase()
                when {
                    path.endsWith(".zip") ||
                        normalized.contains("/releases/download/", ignoreCase = true) -> {
                        CustomDriverLinkCardState.DIRECT_DOWNLOAD
                    }

                    else -> CustomDriverLinkCardState.INVALID
                }
            } catch (_: Exception) {
                CustomDriverLinkCardState.INVALID
            }
        }
    }
}

object CustomDriverLinkStore {
    private const val PREF_KEY = "custom_driver_download_links"

    private val mapper = jacksonObjectMapper()

    fun load(context: Context): List<CustomDriverLink> {
        val json = PreferenceManager.getDefaultSharedPreferences(context).getString(PREF_KEY, null)
            ?: return emptyList()
        return try {
            mapper.readValue<List<CustomDriverLink>>(json).sortedBy { it.label.lowercase() }
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun save(context: Context, links: List<CustomDriverLink>) {
        PreferenceManager.getDefaultSharedPreferences(context).edit {
            putString(PREF_KEY, mapper.writeValueAsString(links))
        }
    }

    fun loadReleaseCatalogLinks(context: Context): List<CustomDriverLink> =
        load(context).filter { it.isReleaseCatalogSource() }

    fun addParsed(context: Context, label: String, parsed: ParsedDriverLink): CustomDriverLink? {
        val link = linkFromParsed(label, parsed) ?: return null
        val updated = load(context) + link
        save(context, updated)
        return link
    }

    fun updateParsed(
        context: Context,
        linkId: String,
        label: String,
        parsed: ParsedDriverLink,
    ): CustomDriverLink? {
        val links = load(context)
        if (links.none { it.id == linkId }) {
            return null
        }
        val updatedLink = linkFromParsed(label, parsed, linkId) ?: return null
        save(context, links.map { if (it.id == linkId) updatedLink else it })
        return updatedLink
    }

    private fun linkFromParsed(
        label: String,
        parsed: ParsedDriverLink,
        id: String = UUID.randomUUID().toString(),
    ): CustomDriverLink? = when (parsed) {
        is ParsedDriverLink.Direct -> CustomDriverLink(
            id = id,
            label = label.trim(),
            url = parsed.normalizedUrl,
        )

        is ParsedDriverLink.ReleaseRepo -> CustomDriverLink(
            id = id,
            label = label.trim(),
            url = parsed.normalizedUrl,
            releaseBackend = parsed.source.backend.name,
            releaseRepoPath = parsed.source.repoPath,
            releaseApiBase = parsed.source.apiBaseUrl,
            githubRepo = if (parsed.source.backend == DriverReleaseBackend.GITHUB) {
                parsed.source.repoPath
            } else {
                null
            },
        )

        ParsedDriverLink.Invalid -> null
    }

    fun remove(context: Context, id: String) {
        save(context, load(context).filterNot { it.id == id })
    }

    fun fileNameForLink(link: CustomDriverLink): String {
        val normalized = DriverFetcherLinkParser.normalizeUrlInput(link.url)
        val pathName = try {
            java.net.URL(normalized).path.substringAfterLast('/').takeIf {
                it.isNotBlank() && it.contains('.')
            }
        } catch (_: Exception) {
            null
        }
        if (pathName != null) {
            return pathName
        }
        val safeLabel = link.label.filter { it.isLetterOrDigit() || it == '_' || it == '-' }
            .ifBlank { "driver" }
        return "$safeLabel.zip"
    }
}