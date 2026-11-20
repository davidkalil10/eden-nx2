// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2026 Nikilite
// SPDX-License-Identifier: GPL-3.0-or-later

#include "qt_common/util/backup_manager.h"

#include <QDateTime>
#include <QThreadPool>

#include <algorithm>
#include <vector>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"

namespace fs = std::filesystem;

namespace {
constexpr std::size_t MaxAutoBackups = 10;
}

BackupManager::BackupManager(QObject* parent) : QObject(parent) {}

BackupManager::~BackupManager() = default;

bool BackupManager::IsAutoEnabled() const {
    QSettings settings;
    return settings.value(QStringLiteral("backup/auto_enabled"), false).toBool();
}

void BackupManager::SetAutoEnabled(bool enabled) {
    QSettings settings;
    settings.setValue(QStringLiteral("backup/auto_enabled"), enabled);
}

fs::path BackupManager::GetSourcePath() const {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "user" / "save";
}

fs::path BackupManager::GetAutoBasePath() const {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir) / "backups" / "auto";
}

fs::path BackupManager::GetManualBasePath() const {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir) / "backups" / "manual";
}

bool BackupManager::IsRunning() const {
    return running_.load();
}

void BackupManager::BackupAll(bool manual) {
    if (running_.exchange(true)) {
        LOG_WARNING(Frontend, "Backup already in progress, skipping");
        return;
    }

    const auto source = GetSourcePath();
    std::error_code ec;
    if (!fs::exists(source, ec)) {
        running_.store(false);
        LOG_INFO(Frontend, "No save directory yet, nothing to back up");
        emit BackupFinished(true, tr("No save data to back up."));
        return;
    }

    const auto timestamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    const auto base = manual ? GetManualBasePath() : GetAutoBasePath();
    const auto destination = base / timestamp.toStdString();

    LOG_INFO(Frontend, "Backing up saves to {}", destination.string());

    QThreadPool::globalInstance()->start(
        [this, source, destination, prune = !manual] { DoBackup(source, destination, prune); });
}

void BackupManager::DoBackup(const fs::path& source, const fs::path& destination, bool prune) {
    std::error_code ec;
    fs::create_directories(destination, ec);
    if (!ec) {
        fs::copy(source, destination, fs::copy_options::recursive, ec);
    }

    const bool success = !ec;
    if (!success) {
        LOG_ERROR(Frontend, "Save backup failed: {}", ec.message());
    } else if (prune) {
        PruneAutoBackups();
    }

    running_.store(false);
    QMetaObject::invokeMethod(
        this,
        [this, success] {
            emit BackupFinished(success, success ? tr("Backup complete.") : tr("Backup failed."));
        },
        Qt::QueuedConnection);
}

void BackupManager::PruneAutoBackups() {
    std::error_code ec;
    std::vector<fs::path> dirs;
    for (fs::directory_iterator it(GetAutoBasePath(), ec), end; !ec && it != end;
         it.increment(ec)) {
        if (it->is_directory()) {
            dirs.push_back(it->path());
        }
    }

    // timestamp names sort chronologically
    std::sort(dirs.begin(), dirs.end());
    while (dirs.size() > MaxAutoBackups) {
        fs::remove_all(dirs.front(), ec);
        dirs.erase(dirs.begin());
    }
}
