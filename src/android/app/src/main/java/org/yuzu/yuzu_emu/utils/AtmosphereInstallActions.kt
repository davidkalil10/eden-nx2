// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import androidx.fragment.app.FragmentActivity
import androidx.fragment.app.FragmentManager
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.fragments.MessageDialogFragment
import org.yuzu.yuzu_emu.fragments.ProgressDialogFragment
import java.io.File

object AtmosphereInstallActions {
    fun installLatestFromGitHub(
        activity: FragmentActivity,
        fragmentManager: FragmentManager
    ) {
        if (NativeLibrary.isRunning()) {
            MessageDialogFragment.newInstance(
                activity,
                titleId = R.string.install_atmosphere,
                descriptionId = R.string.install_atmosphere_running_blocked
            ).show(fragmentManager, MessageDialogFragment.TAG)
            return
        }

        MessageDialogFragment.newInstance(
            activity,
            titleId = R.string.install_atmosphere,
            descriptionId = R.string.install_atmosphere_confirm,
            positiveAction = {
                runInstall(activity, fragmentManager)
            },
            negativeAction = {}
        ).show(fragmentManager, MessageDialogFragment.TAG)
    }

    private fun runInstall(
        activity: FragmentActivity,
        fragmentManager: FragmentManager
    ) {
        ProgressDialogFragment.newInstance(
            activity,
            R.string.install_atmosphere_progress,
            cancellable = true
        ) { progressCallback, messageCallback ->
            val asset = AtmosphereInstaller.resolveLatestSdCardPackage()
            messageCallback(activity.getString(R.string.install_atmosphere_downloading, asset.tag))
            val zip = AtmosphereInstaller.downloadAsset(
                asset,
                activity,
                progressCallback,
                messageCallback
            )
            val sdmc = File(NativeConfig.getSdmcDir())
            messageCallback(activity.getString(R.string.install_atmosphere_extracting))
            AtmosphereInstaller.installToSdmc(zip, sdmc, progressCallback, messageCallback)
            zip.delete()
            activity.getString(R.string.install_atmosphere_success, asset.tag)
        }.show(fragmentManager, ProgressDialogFragment.TAG)
    }
}
