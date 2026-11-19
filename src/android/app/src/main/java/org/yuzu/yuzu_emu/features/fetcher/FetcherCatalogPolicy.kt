// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.fetcher

/**
 * Shared thresholds for large driver release catalogs (UI + animation).
 */
object FetcherCatalogPolicy {
    const val LARGE_CATALOG_THRESHOLD = 8
    const val SCROLLABLE_RELEASE_THRESHOLD = 4
    const val INITIAL_VISIBLE_RELEASES = 10
}