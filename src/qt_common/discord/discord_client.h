// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <QJsonValue>
#include <QStringList>

namespace Discord {

class Client final {
public:
    explicit Client(QStringList endpoints = {});
    ~Client();

    void SetActivity(const QJsonValue& activity);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Discord
