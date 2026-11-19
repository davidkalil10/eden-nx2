// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.fetcher

import android.content.Context
import com.fasterxml.jackson.databind.JsonNode
import com.fasterxml.jackson.module.kotlin.jacksonObjectMapper
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import org.yuzu.yuzu_emu.fragments.DriverFetcherFragment.Artifact
import org.yuzu.yuzu_emu.fragments.DriverFetcherFragment.Release
import org.yuzu.yuzu_emu.fragments.DriverFetcherFragment.SortMode
import java.io.IOException
import java.net.URL
import java.time.Instant
import java.time.LocalDateTime
import java.time.ZoneId
import java.util.concurrent.TimeUnit

object DriverReleasesClient {

    private const val MAX_PAGES = 15
    private const val USER_AGENT = "Eden-Android-DriverFetcher"
    /** Skip network when cache is newer than this (per repo). */
    private const val CACHE_FRESH_MS = 15L * 60L * 1000L

    private val httpClient = OkHttpClient.Builder()
        .connectTimeout(30, TimeUnit.SECONDS)
        .readTimeout(60, TimeUnit.SECONDS)
        .build()

    private val mapper = jacksonObjectMapper()

    fun releaseFingerprint(releases: List<Release>): String {
        val head = releases.filter { !it.prerelease }.maxByOrNull { it.publishTime }
            ?: releases.maxByOrNull { it.publishTime }
        return head?.let { "${it.tagName}\u0001${it.publishTime}" } ?: ""
    }

    /**
     * Returns cached data when fresh; otherwise probes with at most one list page (or HTTP 304)
     * before running a full paginated fetch only when new releases are likely.
     */
    suspend fun fetchAllReleasesRespectingCache(
        context: Context,
        source: DriverReleaseSource,
        useTagName: Boolean,
        sortMode: SortMode,
    ): ArrayList<Release> = withContext(Dispatchers.IO) {
        val (cacheEntry, cachedReleases) = DriverReleasesCacheStore.readCacheBundle(
            context,
            source,
            useTagName,
            sortMode,
        )
        val now = System.currentTimeMillis()

        if (cacheEntry != null && now - cacheEntry.fetchedAtMs < CACHE_FRESH_MS) {
            return@withContext cachedReleases ?: arrayListOf()
        }

        if (cacheEntry != null && !cachedReleases.isNullOrEmpty()) {
            val probe = probeFirstPage(source, useTagName, cacheEntry.etag)
            when {
                probe.notModified -> {
                    DriverReleasesCacheStore.touchFetchedAt(
                        context,
                        source,
                        useTagName,
                        sortMode,
                        cacheEntry.etag,
                    )
                    return@withContext cachedReleases
                }

                probe.fingerprint == cacheEntry.fingerprint -> {
                    DriverReleasesCacheStore.touchFetchedAt(
                        context,
                        source,
                        useTagName,
                        sortMode,
                        probe.etag ?: cacheEntry.etag,
                    )
                    return@withContext cachedReleases
                }

                probe.fingerprint.isEmpty() -> return@withContext cachedReleases
            }
        }

        val (releases, etag) = try {
            fetchAllReleasesWithEtag(source, useTagName, sortMode)
        } catch (e: Exception) {
            if (!cachedReleases.isNullOrEmpty()) {
                return@withContext cachedReleases
            }
            throw e
        }
        if (releases.isNotEmpty()) {
            DriverReleasesCacheStore.save(context, source, useTagName, sortMode, releases, etag)
        }
        releases
    }

    private data class FirstPageProbe(
        val notModified: Boolean = false,
        val fingerprint: String = "",
        val etag: String? = null,
    )

    private fun probeFirstPage(
        source: DriverReleaseSource,
        useTagName: Boolean,
        etag: String?,
    ): FirstPageProbe {
        return try {
            when (source.backend) {
                DriverReleaseBackend.GITHUB -> {
                    val response = fetchGitHubFirstPageResponse(source.repoPath, etag)
                    if (response.notModified) {
                        return FirstPageProbe(notModified = true, etag = etag)
                    }
                    val body = response.body ?: return FirstPageProbe()
                    val page = Release.fromJsonArray(body, useTagName, SortMode.Default)
                    FirstPageProbe(
                        fingerprint = releaseFingerprint(page),
                        etag = response.etag,
                    )
                }

                DriverReleaseBackend.GITLAB -> {
                    val body = fetchGitLabFirstPageBody(source) ?: return FirstPageProbe()
                    val page = releasesFromGitLabJson(body, useTagName)
                    FirstPageProbe(fingerprint = releaseFingerprint(page))
                }

                DriverReleaseBackend.GITEA -> {
                    val body = fetchGiteaFirstPageBody(source) ?: return FirstPageProbe()
                    val page = Release.fromJsonArray(body, useTagName, SortMode.Default)
                    FirstPageProbe(fingerprint = releaseFingerprint(page))
                }
            }
        } catch (_: Exception) {
            FirstPageProbe()
        }
    }

    private fun fetchAllReleasesWithEtag(
        source: DriverReleaseSource,
        useTagName: Boolean,
        sortMode: SortMode,
    ): Pair<ArrayList<Release>, String?> = when (source.backend) {
        DriverReleaseBackend.GITHUB ->
            fetchGitHubPagesWithEtag(source.repoPath, useTagName, sortMode)

        DriverReleaseBackend.GITLAB ->
            fetchGitLabPages(source, useTagName, sortMode) to null

        DriverReleaseBackend.GITEA ->
            fetchGiteaPages(source, useTagName, sortMode) to null
    }

    private fun fetchGitHubFirstPageResponse(
        repoPath: String,
        ifNoneMatch: String?,
    ): HttpResponse {
        val builder = Request.Builder()
            .url("https://api.github.com/repos/$repoPath/releases?per_page=100&page=1")
            .header("Accept", "application/vnd.github+json")
            .header("User-Agent", USER_AGENT)
        if (!ifNoneMatch.isNullOrBlank()) {
            builder.header("If-None-Match", ifNoneMatch)
        }
        return executeRequest(builder.build(), "$repoPath page 1")
    }

    private fun fetchGitHubPagesWithEtag(
        repoPath: String,
        useTagName: Boolean,
        sortMode: SortMode,
    ): Pair<ArrayList<Release>, String?> {
        val merged = ArrayList<Release>()
        var page = 1
        var firstEtag: String? = null

        while (page <= MAX_PAGES) {
            val response = if (page == 1) {
                fetchGitHubFirstPageResponse(repoPath, null)
            } else {
                val request = Request.Builder()
                    .url(
                        "https://api.github.com/repos/$repoPath/releases?per_page=100&page=$page",
                    )
                    .header("Accept", "application/vnd.github+json")
                    .header("User-Agent", USER_AGENT)
                    .build()
                executeRequest(request, "$repoPath page $page")
            }

            if (page == 1) {
                firstEtag = response.etag
            }

            val body = response.body
            if (body.isNullOrBlank()) {
                break
            }

            val pageReleases = Release.fromJsonArray(body, useTagName, SortMode.Default)
            if (pageReleases.isEmpty()) {
                break
            }
            merged.addAll(pageReleases)
            if (pageReleases.size < 100) {
                break
            }
            page++
        }

        return finalizeReleases(merged, sortMode) to firstEtag
    }

    private fun fetchGitLabFirstPageBody(source: DriverReleaseSource): String? {
        val projectId = DriverFetcherLinkParser.gitLabProjectId(source.repoPath)
        val request = Request.Builder()
            .url(
                "${source.apiBaseUrl}/projects/$projectId/releases?per_page=100&page=1",
            )
            .header("User-Agent", USER_AGENT)
            .build()
        return executeRequest(request, "${source.repoPath} page 1").body
    }

    private fun fetchGiteaFirstPageBody(source: DriverReleaseSource): String? {
        val request = Request.Builder()
            .url(
                "${source.apiBaseUrl}/repos/${source.repoPath}/releases" +
                    "?limit=100&page=1",
            )
            .header("User-Agent", USER_AGENT)
            .build()
        return executeRequest(request, "${source.repoPath} page 1").body
    }

    private fun fetchGiteaPages(
        source: DriverReleaseSource,
        useTagName: Boolean,
        sortMode: SortMode,
    ): ArrayList<Release> {
        val merged = ArrayList<Release>()
        var page = 1

        while (page <= MAX_PAGES) {
            val request = Request.Builder()
                .url(
                    "${source.apiBaseUrl}/repos/${source.repoPath}/releases" +
                        "?limit=100&page=$page",
                )
                .header("User-Agent", USER_AGENT)
                .build()

            val body = executeRequest(request, "${source.repoPath} page $page").body
            if (body.isNullOrBlank()) {
                break
            }

            val pageReleases = Release.fromJsonArray(body, useTagName, SortMode.Default)
            if (pageReleases.isEmpty()) {
                break
            }
            merged.addAll(pageReleases)
            if (pageReleases.size < 100) {
                break
            }
            page++
        }

        return finalizeReleases(merged, sortMode)
    }

    private fun fetchGitLabPages(
        source: DriverReleaseSource,
        useTagName: Boolean,
        sortMode: SortMode,
    ): ArrayList<Release> {
        val projectId = DriverFetcherLinkParser.gitLabProjectId(source.repoPath)
        val merged = ArrayList<Release>()
        var page = 1

        while (page <= MAX_PAGES) {
            val request = Request.Builder()
                .url(
                    "${source.apiBaseUrl}/projects/$projectId/releases?per_page=100&page=$page",
                )
                .header("User-Agent", USER_AGENT)
                .build()

            val body = executeRequest(request, "${source.repoPath} page $page").body
            if (body.isNullOrBlank()) {
                break
            }

            val pageReleases = releasesFromGitLabJson(body, useTagName)
            if (pageReleases.isEmpty()) {
                break
            }
            merged.addAll(pageReleases)
            if (pageReleases.size < 100) {
                break
            }
            page++
        }

        return finalizeReleases(merged, sortMode)
    }

    private fun releasesFromGitLabJson(json: String, useTagName: Boolean): ArrayList<Release> {
        val releases = ArrayList<Release>()
        try {
            val root = mapper.readTree(json)
            if (!root.isArray) {
                return releases
            }
            root.forEach { node ->
                releases.add(parseGitLabRelease(node, useTagName))
            }
        } catch (_: Exception) {
            return ArrayList()
        }
        return releases
    }

    private fun parseGitLabRelease(node: JsonNode, useTagName: Boolean): Release {
        val tagName = node.path("tag_name").asText("")
        val titleName = node.path("name").asText(tagName)
        val body = node.path("description").asText("")
        val prerelease = node.path("upcoming_release").asBoolean(false)

        val published = node.path("released_at").asText("")
        val localTime = try {
            Instant.parse(published).atZone(ZoneId.systemDefault()).toLocalDateTime()
        } catch (_: Exception) {
            LocalDateTime.now()
        }

        val artifacts = ArrayList<Artifact>()
        val assets = node.path("assets")
        val links = assets.path("links")
        if (links.isArray) {
            links.forEach { linkNode ->
                val urlStr = linkNode.path("direct_asset_url").asText(null)
                    ?: linkNode.path("url").asText(null)
                val name = linkNode.path("name").asText(null)
                if (!urlStr.isNullOrBlank() && !name.isNullOrBlank()) {
                    artifacts.add(Artifact(URL(urlStr), name))
                }
            }
        }

        return Release(
            tagName = tagName,
            titleName = titleName,
            title = if (useTagName) tagName else titleName,
            body = body,
            artifacts = artifacts,
            prerelease = prerelease,
            publishTime = localTime,
        )
    }

    private data class HttpResponse(
        val body: String? = null,
        val etag: String? = null,
        val notModified: Boolean = false,
    )

    private fun executeRequest(request: Request, label: String): HttpResponse {
        httpClient.newCall(request).execute().use { response ->
            if (response.code == 304) {
                return HttpResponse(
                    notModified = true,
                    etag = response.header("ETag"),
                )
            }
            if (!response.isSuccessful) {
                val detail = response.body?.string().orEmpty().take(200)
                throw IOException("HTTP ${response.code} for $label: $detail")
            }
            return HttpResponse(
                body = response.body?.string(),
                etag = response.header("ETag"),
            )
        }
    }

    private fun finalizeReleases(
        releases: ArrayList<Release>,
        sortMode: SortMode,
    ): ArrayList<Release> {
        releases.forEach { it.latest = false }

        when (sortMode) {
            SortMode.PublishTime -> releases.sortByDescending { it.publishTime }
            SortMode.Default -> {}
        }

        releases.firstOrNull { !it.prerelease }?.latest = true
        return releases
    }
}