// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.model

import android.graphics.BitmapFactory
import android.os.Parcelable
import android.widget.ImageView
import kotlinx.parcelize.Parcelize
import org.yuzu.yuzu_emu.NativeLibrary
import java.io.File

@Parcelize
data class UserProfile(
    val uuid: String,
    val username: String,
    val imagePath: String = ""
) : Parcelable

object ProfileUtils {
    fun generateRandomUUID(): String {
        val uuid = java.util.UUID.randomUUID()
        return uuid.toString().replace("-", "")
    }

    fun formatDisplayUuid(uuid: String): String {
        if (uuid.length != 32) {
            return uuid
        }
        return buildString {
            append(uuid, 0, 8)
            append('-')
            append(uuid, 8, 12)
            append('-')
            append(uuid, 12, 16)
            append('-')
            append(uuid, 16, 20)
            append('-')
            append(uuid, 20, 32)
        }
    }

    fun loadRegisteredProfiles(): List<UserProfile> {
        val userUuids = NativeLibrary.getAllUsers() ?: return emptyList()
        return userUuids.mapNotNull { uuid ->
            if (uuid.isEmpty()) {
                return@mapNotNull null
            }
            val username = NativeLibrary.getUserUsername(uuid) ?: return@mapNotNull null
            if (username.isEmpty()) {
                return@mapNotNull null
            }
            UserProfile(uuid, username, NativeLibrary.getUserImagePath(uuid).orEmpty())
        }
    }

    fun bindProfileAvatar(imageView: ImageView, imagePath: String) {
        imageView.scaleType = ImageView.ScaleType.CENTER_CROP
        val imageFile = File(imagePath)
        if (imageFile.exists()) {
            imageView.imageTintList = null
            imageView.setImageBitmap(BitmapFactory.decodeFile(imagePath))
        } else {
            val jpegData = NativeLibrary.getDefaultAccountBackupJpeg()
            imageView.imageTintList = null
            imageView.setImageBitmap(BitmapFactory.decodeByteArray(jpegData, 0, jpegData.size))
        }
    }
}
