// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.fetcher

enum class DriverReleaseBackend {
    GITHUB,
    GITLAB,
    GITEA,
}

data class DriverReleaseSource(
    val backend: DriverReleaseBackend,
    val repoPath: String,
    val apiBaseUrl: String,
    val webUrl: String,
)