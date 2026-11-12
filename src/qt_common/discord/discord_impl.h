// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2018 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "discord.h"

#include <string>
#include <QNetworkAccessManager>
#include <QPointer>
#include "discord_client.h"

class QNetworkReply;

namespace Core {
class System;
}

namespace Discord {

class DiscordImpl : public QObject, public DiscordInterface {
public:
    DiscordImpl(Core::System& system_);
    ~DiscordImpl() override;

    void Pause() override;
    void Update() override;

private:
    std::string GetGameString(const std::string& title);
    void UpdateGameStatus(const QString& image_url);
    void CancelArtworkRequest();

    std::string game_title{};
    Core::System& system;
    Client client;
    QNetworkAccessManager network;
    QPointer<QNetworkReply> artwork_reply;
    qint64 start_time = 0;
};

} // namespace Discord
