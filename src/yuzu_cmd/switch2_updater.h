// SPDX-FileCopyrightText: Copyright 2026 Eden-NX2 Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __SWITCH2__

#include <cstdint>
#include <functional>
#include <string>

#define EDEN_NX2_VERSION "0.1.9"

struct Switch2UpdateInfo {
    bool ok = false;
    bool update_available = false;
    std::string current_version = EDEN_NX2_VERSION;
    std::string latest_version;
    std::string tag_name;
    std::string release_name;
    std::string download_url;
    std::string error_msg;
};

using Switch2UpdateProgressFn = std::function<bool(std::uint64_t downloaded, std::uint64_t total)>;

// Resolves the writable NRO path on sdmc:/ using argv0 if valid, or default fallback paths.
std::string Switch2ResolveSelfNroPath(const char* argv0);

// Queries GitHub Releases latest endpoint for davidkalil10/eden-nx2.
Switch2UpdateInfo Switch2CheckForUpdate();

// Downloads the new NRO to a .tmp file, validates NRO0 + OUN0 headers, and atomically replaces target_nro_path.
bool Switch2DownloadAndInstallUpdate(const std::string& download_url,
                                     const std::string& target_nro_path,
                                     const Switch2UpdateProgressFn& progress_cb,
                                     std::string* out_error);

#endif // __SWITCH2__
