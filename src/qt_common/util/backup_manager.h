// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2026 Nikilite
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

#include <atomic>
#include <filesystem>

class BackupManager : public QObject {
    Q_OBJECT

public:
    explicit BackupManager(QObject* parent = nullptr);
    ~BackupManager() override;

    bool IsAutoEnabled() const;
    void SetAutoEnabled(bool enabled);

    std::filesystem::path GetSourcePath() const;
    std::filesystem::path GetAutoBasePath() const;
    std::filesystem::path GetManualBasePath() const;

    bool IsRunning() const;

    /// runs on the global thread pool. only the last 10 auto backups are kept.
    void BackupAll(bool manual = false);

signals:
    void BackupFinished(bool success, const QString& message);

private:
    void DoBackup(const std::filesystem::path& source, const std::filesystem::path& destination,
                  bool prune);
    void PruneAutoBackups();

    std::atomic<bool> running_{false};
};
