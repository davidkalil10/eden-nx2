// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include "core/file_sys/vfs/vfs_types.h"

namespace Loader {

/// Returns the embedded RomFS slice from a homebrew NRO, or nullptr if absent/invalid.
[[nodiscard]] FileSys::VirtualFile ExtractEmbeddedRomFsFromNro(const FileSys::VirtualFile& nro_file);

/// True when the raw RomFS image contains a file at the given path (e.g. "assets.zip").
[[nodiscard]] bool EmbeddedRomFsContainsPath(const FileSys::VirtualFile& romfs_image,
                                             const std::string& path);

/// libnx-style argv path for romfsMountSelf (e.g. sdmc:/switch/hbmenu.nro).
[[nodiscard]] std::string NormalizeHomebrewNroArgvPath(const std::string& sdmc_path);

} // namespace Loader
