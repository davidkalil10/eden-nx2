// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.util.concurrent.TimeUnit

object AtmosphereInstaller {
    private const val REPO = "Atmosphere-NX/Atmosphere"
    private const val RELEASES_URL = "https://api.github.com/repos/$REPO/releases/latest"

    private val client: OkHttpClient = OkHttpClient.Builder()
        .connectTimeout(60, TimeUnit.SECONDS)
        .readTimeout(120, TimeUnit.SECONDS)
        .followRedirects(true)
        .build()

    data class ReleaseAsset(
        val tag: String,
        val name: String,
        val downloadUrl: String
    )

    @Throws(IOException::class)
    suspend fun resolveLatestSdCardPackage(): ReleaseAsset = withContext(Dispatchers.IO) {
        val request = Request.Builder()
            .url(RELEASES_URL)
            .header("Accept", "application/vnd.github+json")
            .build()

        client.newCall(request).execute().use { response ->
            if (!response.isSuccessful) {
                throw IOException("GitHub API error: HTTP ${response.code}")
            }
            val body = response.body?.string() ?: throw IOException("Empty GitHub response")
            val json = JSONObject(body)
            val tag = json.optString("tag_name", "unknown")
            val assets = json.optJSONArray("assets") ?: JSONArray()
            for (i in 0 until assets.length()) {
                val asset = assets.getJSONObject(i)
                val name = asset.getString("name")
                if (isSdCardZip(name)) {
                    return@withContext ReleaseAsset(
                        tag = tag,
                        name = name,
                        downloadUrl = asset.getString("browser_download_url")
                    )
                }
            }
            throw IOException("No Atmosphere SD card zip found in release $tag")
        }
    }

    private fun isSdCardZip(name: String): Boolean {
        val lower = name.lowercase()
        if (!lower.endsWith(".zip")) return false
        if (!lower.startsWith("atmosphere-")) return false
        if (lower.contains("fusee")) return false
        if (lower.contains("payload")) return false
        if (lower.contains("hekate")) return false
        return true
    }

    suspend fun downloadAsset(
        asset: ReleaseAsset,
        context: Context,
        progressCallback: (max: Long, progress: Long) -> Boolean,
        messageCallback: (String) -> Unit
    ): File = withContext(Dispatchers.IO) {
        val cacheDir = context.externalCacheDir ?: context.cacheDir
        val dest = File(cacheDir, asset.name)
        messageCallback(asset.name)

        val request = Request.Builder()
            .url(asset.downloadUrl)
            .header("Accept", "application/octet-stream")
            .build()

        client.newCall(request).execute().use { response ->
            if (!response.isSuccessful) {
                throw IOException("Download failed: HTTP ${response.code}")
            }
            val body = response.body ?: throw IOException("Empty download body")
            val totalBytes = body.contentLength().coerceAtLeast(-1L)
            dest.outputStream().use { output ->
                body.byteStream().use { input ->
                    val buffer = ByteArray(8192)
                    var downloaded = 0L
                    var read: Int
                    while (input.read(buffer).also { read = it } != -1) {
                        if (progressCallback(totalBytes.coerceAtLeast(1L), downloaded)) {
                            throw IOException("Cancelled")
                        }
                        output.write(buffer, 0, read)
                        downloaded += read
                        progressCallback(totalBytes.coerceAtLeast(downloaded), downloaded)
                    }
                }
            }
        }
        dest
    }

    suspend fun installToSdmc(
        zipFile: File,
        sdmcDir: File,
        progressCallback: (max: Long, progress: Long) -> Boolean,
        messageCallback: (String) -> Unit
    ) = withContext(Dispatchers.IO) {
        if (!sdmcDir.exists() && !sdmcDir.mkdirs()) {
            throw IOException("Could not create SDMC directory: ${sdmcDir.absolutePath}")
        }
        messageCallback(sdmcDir.absolutePath)
        FileUtil.unzipToInternalStorage(zipFile.absolutePath, sdmcDir, progressCallback)
    }
}
