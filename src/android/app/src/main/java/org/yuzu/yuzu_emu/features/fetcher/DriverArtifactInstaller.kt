// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.fetcher

import android.view.LayoutInflater
import android.widget.Toast
import androidx.core.net.toUri
import androidx.fragment.app.FragmentActivity
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.databinding.DialogProgressBinding
import org.yuzu.yuzu_emu.model.DriverViewModel
import org.yuzu.yuzu_emu.utils.FileUtil
import org.yuzu.yuzu_emu.utils.GpuDriverHelper
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.net.URL

object DriverArtifactInstaller {
    private val client = OkHttpClient.Builder()
        .followRedirects(true)
        .followSslRedirects(true)
        .build()

    fun installFromUrl(
        activity: FragmentActivity,
        driverViewModel: DriverViewModel,
        url: URL,
        fileName: String,
        onInstalled: (() -> Unit)? = null,
    ) {
        if (GpuDriverHelper.isDriverZipInstalledByName(fileName)) {
            Toast.makeText(
                activity,
                activity.getString(R.string.driver_already_installed),
                Toast.LENGTH_SHORT
            ).show()
            onInstalled?.invoke()
            return
        }

        val dialogBinding = DialogProgressBinding.inflate(LayoutInflater.from(activity))
        dialogBinding.progressBar.isIndeterminate = true
        dialogBinding.title.text = activity.getString(R.string.installing_driver)
        dialogBinding.status.text = activity.getString(R.string.downloading)

        val progressDialog = MaterialAlertDialogBuilder(activity)
            .setView(dialogBinding.root)
            .setCancelable(false)
            .create()

        progressDialog.show()

        CoroutineScope(Dispatchers.Main).launch {
            try {
                val request = Request.Builder()
                    .url(url)
                    .header("Accept", "application/octet-stream")
                    .build()

                val cacheDir = activity.externalCacheDir
                    ?: throw IOException(activity.getString(R.string.failed_cache_dir))
                cacheDir.mkdirs()

                val file = File(cacheDir, fileName)

                withContext(Dispatchers.IO) {
                    client.newCall(request).execute().use { response ->
                        if (!response.isSuccessful) {
                            throw IOException("${response.code}")
                        }
                        response.body?.byteStream()?.use { input ->
                            FileOutputStream(file).use { output ->
                                input.copyTo(output)
                            }
                        } ?: throw IOException(activity.getString(R.string.empty_response_body))
                    }
                }

                if (file.length() == 0L) {
                    throw IOException(activity.getString(R.string.driver_empty))
                }

                dialogBinding.status.text = activity.getString(R.string.installing)

                val driverData = GpuDriverHelper.getMetadataFromZip(file)
                val storageRoot = GpuDriverHelper.driverStoragePath
                    ?: throw IOException(activity.getString(R.string.driver_failed_title))
                if (GpuDriverHelper.copyDriverToInternalStorage(file.toUri())) {
                    val driverPath =
                        "$storageRoot${FileUtil.getFilename(file.toUri())}"
                    driverViewModel.onDriverAdded(Pair(driverPath, driverData))
                    progressDialog.dismiss()
                    Toast.makeText(
                        activity,
                        activity.getString(R.string.successfully_installed, driverData.name),
                        Toast.LENGTH_SHORT
                    ).show()
                    onInstalled?.invoke()
                } else {
                    throw IOException(
                        activity.getString(R.string.failed_install_driver, fileName)
                    )
                }
            } catch (e: Exception) {
                progressDialog.dismiss()
                MaterialAlertDialogBuilder(activity)
                    .setTitle(activity.getString(R.string.driver_failed_title))
                    .setMessage(e.message)
                    .setPositiveButton(R.string.ok) { dialog, _ -> dialog.cancel() }
                    .show()
            }
        }
    }
}