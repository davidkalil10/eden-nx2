// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2018 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

#include <QDateTime>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <boost/algorithm/string/replace.hpp>

#include "common/string_util.h"
#include "core/core.h"
#include "core/loader/loader.h"

#include "discord_impl.h"

namespace Discord {

DiscordImpl::DiscordImpl(Core::System& system_) : system{system_} {}

DiscordImpl::~DiscordImpl() {
    CancelArtworkRequest();
}

void DiscordImpl::Pause() {
    CancelArtworkRequest();
    client.SetActivity(QJsonValue::Null);
    game_title.clear();
    start_time = 0;
}

void DiscordImpl::CancelArtworkRequest() {
    if (artwork_reply) {
        artwork_reply->disconnect(this);
        artwork_reply->abort();
        artwork_reply->deleteLater();
        artwork_reply.clear();
    }
}

std::string DiscordImpl::GetGameString(const std::string& title) {
    // Convert to lowercase
    std::string icon_name = Common::ToLower(title);

    // Replace spaces with dashes
    std::replace(icon_name.begin(), icon_name.end(), ' ', '-');
    boost::replace_all(icon_name, "é", "e");

    // Remove non-alphanumeric characters but keep dashes
    std::erase_if(icon_name, [](unsigned char c) { return !std::isalnum(c) && c != '-'; });

    // Remove dashes from the start and end of the string
    icon_name.erase(icon_name.begin(), std::find_if(icon_name.begin(), icon_name.end(),
                                                    [](int ch) { return ch != '-'; }));
    icon_name.erase(
        std::find_if(icon_name.rbegin(), icon_name.rend(), [](int ch) { return ch != '-'; }).base(),
        icon_name.end());

    // Remove double dashes
    icon_name.erase(std::unique(icon_name.begin(), icon_name.end(),
                                [](char a, char b) { return a == '-' && b == '-'; }),
                    icon_name.end());

    return icon_name;
}

static const QString DEFAULT_DISCORD_TEXT =
    QStringLiteral("Eden is an emulator for the Nintendo Switch");
static const QString DEFAULT_DISCORD_IMAGE = QStringLiteral(
    "https://git.eden-emu.dev/eden-emu/eden/raw/branch/master/dist/qt_themes/default/icons/256x256/"
    "eden.png");

void DiscordImpl::UpdateGameStatus(const QString& image_url) {
    const auto title = QString::fromStdString(game_title);
    const QJsonObject assets{{QStringLiteral("large_image"), image_url},
                             {QStringLiteral("large_text"), title},
                             {QStringLiteral("small_image"), DEFAULT_DISCORD_IMAGE},
                             {QStringLiteral("small_text"), DEFAULT_DISCORD_TEXT}};
    const QJsonObject presence{
        {QStringLiteral("assets"), assets},
        {QStringLiteral("state"), title},
        {QStringLiteral("status_display_type"), 1},
        {QStringLiteral("details"), QStringLiteral("Currently in game")},
        {QStringLiteral("timestamps"), QJsonObject{{QStringLiteral("start"), start_time}}}};
    client.SetActivity(presence);
}

void DiscordImpl::Update() {
    CancelArtworkRequest();
    if (system.IsPoweredOn()) {
        std::string new_title;
        system.GetAppLoader().ReadTitle(new_title);
        if (start_time == 0 || new_title != game_title) {
            start_time = QDateTime::currentSecsSinceEpoch();
        }
        game_title = std::move(new_title);

        UpdateGameStatus(DEFAULT_DISCORD_IMAGE);
        const auto game_url =
            QUrl{QStringLiteral("https://raw.githubusercontent.com/eden-emulator/boxart/"
                                "refs/heads/master/img/%1.png")
                     .arg(QString::fromStdString(GetGameString(game_title)))};
        QNetworkRequest request{game_url};
        request.setTransferTimeout(5000);
        artwork_reply = network.head(request);
        auto* reply = artwork_reply.data();
        connect(reply, &QNetworkReply::finished, this, [this, reply, game_url] {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
                UpdateGameStatus(game_url.toString());
            }
            artwork_reply.clear();
            reply->deleteLater();
        });
        return;
    }

    if (start_time == 0 || !game_title.empty()) {
        start_time = QDateTime::currentSecsSinceEpoch();
    }
    game_title.clear();
    const QJsonObject assets{{QStringLiteral("large_image"), DEFAULT_DISCORD_IMAGE},
                             {QStringLiteral("large_text"), DEFAULT_DISCORD_TEXT}};
    const QJsonObject presence{
        {QStringLiteral("assets"), assets},
        {QStringLiteral("status_display_type"), 0},
        {QStringLiteral("details"), QStringLiteral("Currently not in game")},
        {QStringLiteral("timestamps"), QJsonObject{{QStringLiteral("start"), start_time}}}};
    client.SetActivity(presence);
}
} // namespace Discord
