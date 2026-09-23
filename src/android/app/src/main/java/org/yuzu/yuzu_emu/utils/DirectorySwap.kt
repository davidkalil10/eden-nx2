// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import java.io.File

/**
 * Replaces a directory with freshly unpacked content without ever leaving it half written.
 * The siblings are the journal: `.import` is being unpacked or deleted and never trusted,
 * `.ready` is complete and only ever renamed, `.previous` holds the replaced data until
 * `.ready` has taken its place.
 */
object DirectorySwap {
    enum class Result { Replaced, UnpackFailed, SwapFailed }

    private fun sibling(target: File, suffix: String): File? =
        target.absoluteFile.parentFile?.let { File(it, "${target.name}.$suffix") }

    /**
     * Settles a swap a killed process left behind. Run it before anything reads [target]:
     * the next launch recreates an empty target, so its existence proves nothing.
     */
    fun recover(target: File) {
        val staging = sibling(target, "import") ?: return
        val ready = sibling(target, "ready") ?: return
        val previous = sibling(target, "previous") ?: return
        staging.deleteRecursively()
        when {
            ready.exists() && previous.exists() -> {
                // Killed between the renames: whatever is at target appeared after the first.
                target.deleteRecursively()
                swapIn(target, ready, previous)
            }
            // Killed before the first rename: the old data is intact, the import never ran.
            ready.exists() -> discard(ready, staging)
            // No .ready means the second rename happened, so .previous is the old data.
            previous.exists() -> if (target.exists()) {
                previous.deleteRecursively()
            } else {
                previous.renameTo(target)
            }
        }
    }

    fun replace(target: File, beforeSwap: () -> Unit = {}, unpack: (File) -> Unit): Result {
        val staging = sibling(target, "import") ?: return Result.SwapFailed
        val ready = sibling(target, "ready") ?: return Result.SwapFailed
        val previous = sibling(target, "previous") ?: return Result.SwapFailed
        recover(target)

        try {
            staging.mkdirs()
            unpack(staging)
        } catch (_: Exception) {
            staging.deleteRecursively()
            return Result.UnpackFailed
        }
        if (!staging.renameTo(ready)) {
            staging.deleteRecursively()
            return Result.SwapFailed
        }

        beforeSwap()
        return if (swapIn(target, ready, previous)) Result.Replaced else Result.SwapFailed
    }

    // Deleting in place could leave a partial .ready that looks complete; as .import it is
    // thrown away by the next recover() if this delete is cut short.
    private fun discard(ready: File, staging: File) {
        if (ready.renameTo(staging)) {
            staging.deleteRecursively()
        }
    }

    // Every step keeps a full copy under one of the journal names, so recover() can settle.
    private fun swapIn(target: File, ready: File, previous: File): Boolean {
        val staging = sibling(target, "import") ?: return false
        if (target.exists() && !target.renameTo(previous)) {
            discard(ready, staging)
            return false
        }
        if (ready.renameTo(target)) {
            previous.deleteRecursively()
            return true
        }
        // Put the old data back before dropping the new copy.
        if (!previous.exists() || previous.renameTo(target)) {
            if (target.exists()) {
                discard(ready, staging)
            }
        }
        return false
    }
}
