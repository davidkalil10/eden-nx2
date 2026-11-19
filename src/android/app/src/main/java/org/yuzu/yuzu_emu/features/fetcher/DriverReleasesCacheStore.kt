// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.fetcher

import android.content.Context
import com.fasterxml.jackson.module.kotlin.jacksonObjectMapper
import com.fasterxml.jackson.module.kotlin.readValue
import org.yuzu.yuzu_emu.fragments.DriverFetcherFragment.Artifact
import org.yuzu.yuzu_emu.fragments.DriverFetcherFragment.Release
import org.yuzu.yuzu_emu.fragments.DriverFetcherFragment.SortMode
import java.io.File
import java.net.URL
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter

internal data class CachedArtifactDto(
    val url: String,
    val name: String,
)

internal data class CachedReleaseDto(
    val tagName: String,
    val titleName: String,
    val title: String,
    val body: String,
    val artifacts: List<CachedArtifactDto>,
    val prerelease: Boolean,
    val latest: Boolean,
    val publishTime: String,
)

internal data class DriverReleasesCacheEntry(
    val fetchedAtMs: Long,
    val etag: String?,
    val fingerprint: String,
    val useTagName: Boolean,
    val sortMode: String,
    val releases: List<CachedReleaseDto>,
)

object DriverReleasesCacheStore {

    private val mapper = jacksonObjectMapper()
    private val publishFormatter = DateTimeFormatter.ISO_LOCAL_DATE_TIME

    private fun cacheDir(context: Context): File =
        File(context.cacheDir, "driver_release_catalog").apply { mkdirs() }

    private fun cacheFile(
        context: Context,
        source: DriverReleaseSource,
        useTagName: Boolean,
        sortMode: SortMode,
    ): File {
        val base = DriverSourceKeys.key(source)
            .replace(':', '_')
            .replace('/', '_')
        val name = "${base}_${useTagName}_${sortMode.name}.json"
        return File(cacheDir(context), name)
    }

    fun readReleases(
        context: Context,
        source: DriverReleaseSource,
        useTagName: Boolean,
        sortMode: SortMode,
    ): ArrayList<Release>? = readCacheBundle(context, source, useTagName, sortMode).second

    internal fun readCacheBundle(
        context: Context,
        source: DriverReleaseSource,
        useTagName: Boolean,
        sortMode: SortMode,
    ): Pair<DriverReleasesCacheEntry?, ArrayList<Release>?> {
        val entry = readEntry(context, source, useTagName, sortMode)
        return entry to entry?.toReleases()
    }

    private fun DriverReleasesCacheEntry.toReleases(): ArrayList<Release> =
        releases.map { it.toRelease() }.toCollection(ArrayList())

    internal fun readEntry(
        context: Context,
        source: DriverReleaseSource,
        useTagName: Boolean,
        sortMode: SortMode,
    ): DriverReleasesCacheEntry? {
        val file = cacheFile(context, source, useTagName, sortMode)
        if (!file.isFile) {
            return null
        }
        return try {
            mapper.readValue<DriverReleasesCacheEntry>(file.readText())
        } catch (_: Exception) {
            null
        }
    }

    fun save(
        context: Context,
        source: DriverReleaseSource,
        useTagName: Boolean,
        sortMode: SortMode,
        releases: List<Release>,
        etag: String?,
    ) {
        val entry = DriverReleasesCacheEntry(
            fetchedAtMs = System.currentTimeMillis(),
            etag = etag,
            fingerprint = DriverReleasesClient.releaseFingerprint(releases),
            useTagName = useTagName,
            sortMode = sortMode.name,
            releases = releases.map { it.toDto() },
        )
        val file = cacheFile(context, source, useTagName, sortMode)
        file.writeText(mapper.writeValueAsString(entry))
    }

    fun touchFetchedAt(
        context: Context,
        source: DriverReleaseSource,
        useTagName: Boolean,
        sortMode: SortMode,
        etag: String?,
    ) {
        val entry = readEntry(context, source, useTagName, sortMode) ?: return
        val updated = entry.copy(
            fetchedAtMs = System.currentTimeMillis(),
            etag = etag ?: entry.etag,
        )
        cacheFile(context, source, useTagName, sortMode)
            .writeText(mapper.writeValueAsString(updated))
    }

    private fun CachedReleaseDto.toRelease(): Release =
        Release(
            tagName = tagName,
            titleName = titleName,
            title = title,
            body = body,
            artifacts = artifacts.map { Artifact(URL(it.url), it.name) },
            prerelease = prerelease,
            latest = latest,
            publishTime = try {
                LocalDateTime.parse(publishTime, publishFormatter)
            } catch (_: Exception) {
                LocalDateTime.now()
            },
        )

    private fun Release.toDto(): CachedReleaseDto =
        CachedReleaseDto(
            tagName = tagName,
            titleName = titleName,
            title = title,
            body = body,
            artifacts = artifacts.map { CachedArtifactDto(it.url.toString(), it.name) },
            prerelease = prerelease,
            latest = latest,
            publishTime = publishTime.format(publishFormatter),
        )
}