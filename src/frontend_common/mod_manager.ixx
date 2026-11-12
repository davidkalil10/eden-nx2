// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

module;

#include <filesystem>
#include <vector>
#include "common/common_types.h"

export module frontend:mod;

export namespace FrontendCommon {

enum ModInstallResult {
    Cancelled,
    Failed,
    Success,
};

std::vector<std::filesystem::path> GetModFolder(const std::string& root);

ModInstallResult InstallMod(const std::filesystem::path &path, const u64 program_id, const bool copy = true);
}
