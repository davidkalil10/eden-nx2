// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "discord_client.h"

#include <array>
#include <chrono>
#include <deque>
#include <memory>
#include <thread>
#include <utility>

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

#include <boost/asio.hpp>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Discord {
namespace {
namespace asio = boost::asio;
using Error = boost::system::error_code;
using namespace std::chrono_literals;

constexpr auto ApplicationId = "1397286652128264252";
constexpr std::size_t HeaderSize = 8;
constexpr std::size_t MaxFrameSize = 64 * 1024; // Defensive limit, header included.
constexpr std::size_t MaxQueuedFrames = 8;      // Bounds queued output to 512 KiB.
constexpr auto ConnectTimeout = 3s;
constexpr auto RetryInterval = 5s;
constexpr auto ActivityInterval = 4100ms; // Client-side pacing, not an IPC requirement.
constexpr int EndpointCount = 10;

enum class Opcode : quint32 { Handshake, Frame, Close, Ping, Pong };

#ifdef _WIN32
using Socket = asio::windows::stream_handle;
#else
using Socket = asio::local::stream_protocol::socket;
#endif

QStringList FindEndpoints() {
    QStringList paths;
#ifdef _WIN32
    for (int i = 0; i < EndpointCount; ++i) {
        paths.append(QStringLiteral(R"(\\.\pipe\discord-ipc-%1)").arg(i));
    }
#else
    QStringList dirs;
    for (const char* variable : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
        const auto value = qEnvironmentVariable(variable);
        if (!value.isEmpty())
            dirs.append(value);
    }
    dirs.append(QStringLiteral("/tmp"));
    dirs.removeDuplicates();
    for (const auto& dir : dirs) {
        for (const auto& suffix : {QString{}, QStringLiteral("app/com.discordapp.Discord"),
                                   QStringLiteral("snap.discord")}) {
            const QDir base{QDir{dir}.filePath(suffix)};
            for (int i = 0; i < EndpointCount; ++i) {
                paths.append(base.filePath(QStringLiteral("discord-ipc-%1").arg(i)));
            }
        }
    }
#endif
    return paths;
}

QByteArray MakeFrame(Opcode opcode, const QByteArray& data) {
    QByteArray frame(HeaderSize, '\0');
    qToLittleEndian(static_cast<quint32>(opcode), frame.data());
    qToLittleEndian(static_cast<quint32>(data.size()), frame.data() + 4);
    frame.append(data);
    return frame;
}

struct Connection {
    explicit Connection(asio::io_context& io) : socket{io}, timeout{io} {}

    Socket socket;
    asio::steady_timer timeout;
    std::array<char, HeaderSize> header{};
    QByteArray payload;
    std::deque<QByteArray> output; // Keep write buffers alive until completion.
    bool ready = false;
};
} // namespace

struct Client::Impl {
    explicit Impl(QStringList paths)
        : endpoints{paths.isEmpty() ? FindEndpoints() : std::move(paths)} {
        asio::post(io, [this] { Connect(); });
        worker = std::thread{[this] { io.run(); }};
    }

    ~Impl() {
        io.stop();
        worker.join();
    }

    void Connect() {
        auto session = std::make_shared<Connection>(io);
        current = session;
        session->timeout.expires_after(ConnectTimeout);
        session->timeout.async_wait([this, session](Error ec) {
            if (!ec)
                Reconnect(session);
        });

        const auto path = endpoints.at(next_endpoint++);
#ifdef _WIN32
        const HANDLE handle = CreateFileW(reinterpret_cast<const wchar_t*>(path.utf16()),
                                          GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                          FILE_FLAG_OVERLAPPED, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return Reconnect(session);
        Error ec;
        session->socket.assign(handle, ec);
        if (ec) {
            CloseHandle(handle);
            Reconnect(session);
        } else {
            Handshake(session);
        }
#else
        session->socket.async_connect(
            asio::local::stream_protocol::endpoint{path.toUtf8().toStdString()},
            [this, session](Error ec) {
                if (current != session)
                    return;
                if (ec)
                    Reconnect(session);
                else
                    Handshake(session);
            });
#endif
    }

    // By value: callers may pass `current`, which is reset here.
    void Reconnect(std::shared_ptr<Connection> session) {
        if (current != session)
            return;
        const bool wait = session->ready || next_endpoint == endpoints.size();
        current.reset();
        sent_activity = QJsonValue::Undefined;
        session->timeout.cancel();
        Error ignored;
        session->socket.close(ignored);
        if (wait)
            next_endpoint = 0;
        retry.expires_after(wait ? RetryInterval : 0s);
        retry.async_wait([this](Error ec) {
            if (!ec)
                Connect();
        });
    }

    void Handshake(const std::shared_ptr<Connection>& session) {
        const QJsonObject json{{QStringLiteral("v"), 1},
                               {QStringLiteral("client_id"), QString::fromLatin1(ApplicationId)}};
        if (!Send(session, Opcode::Handshake, QJsonDocument{json}.toJson(QJsonDocument::Compact))) {
            return Reconnect(session);
        }
        Read(session);
    }

    void Read(const std::shared_ptr<Connection>& session) {
        asio::async_read(
            session->socket, asio::buffer(session->header), [this, session](Error ec, std::size_t) {
                if (current != session)
                    return;
                if (ec)
                    return Reconnect(session);
                const auto size = qFromLittleEndian<quint32>(session->header.data() + 4);
                if (size > MaxFrameSize - HeaderSize)
                    return Reconnect(session);
                session->payload.resize(size);
                asio::async_read(session->socket, asio::buffer(session->payload.data(), size),
                                 [this, session](Error read_error, std::size_t) {
                                     if (current != session)
                                         return;
                                     if (read_error || !Handle(session))
                                         return Reconnect(session);
                                     Read(session);
                                 });
            });
    }

    bool Handle(const std::shared_ptr<Connection>& session) {
        switch (static_cast<Opcode>(qFromLittleEndian<quint32>(session->header.data()))) {
        case Opcode::Frame: {
            const auto doc = QJsonDocument::fromJson(session->payload);
            if (!doc.isObject())
                return false;
            const auto msg = doc.object();
            if (msg.value(QStringLiteral("evt")) == QStringLiteral("ERROR")) {
                qWarning("Discord IPC: %s", session->payload.constData());
                return session->ready;
            }
            if (!session->ready && msg.value(QStringLiteral("cmd")) == QStringLiteral("DISPATCH") &&
                msg.value(QStringLiteral("evt")) == QStringLiteral("READY")) {
                session->ready = true;
                session->timeout.cancel();
                Flush();
            }
            return true;
        }
        case Opcode::Ping:
            return Send(session, Opcode::Pong, session->payload);
        case Opcode::Pong:
            return true;
        default:
            return false;
        }
    }

    bool Send(const std::shared_ptr<Connection>& session, Opcode opcode, const QByteArray& data) {
        if (static_cast<std::size_t>(data.size()) > MaxFrameSize - HeaderSize ||
            session->output.size() >= MaxQueuedFrames) {
            return false;
        }
        session->output.push_back(MakeFrame(opcode, data));
        if (session->output.size() == 1)
            Write(session);
        return true;
    }

    void Write(const std::shared_ptr<Connection>& session) {
        const auto& data = session->output.front();
        asio::async_write(session->socket, asio::buffer(data.constData(), data.size()),
                          [this, session](Error ec, std::size_t) {
                              if (current != session)
                                  return;
                              if (ec)
                                  return Reconnect(session);
                              session->output.pop_front();
                              if (!session->output.empty())
                                  Write(session);
                          });
    }

    void SetActivity(const QJsonValue& value) {
        if (activity == value)
            return;
        activity = value;
        Flush();
    }

    void Flush() {
        if (limited || !current || !current->ready || activity.isUndefined() ||
            activity == sent_activity)
            return;
        const QJsonObject msg{
            {QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")},
            {QStringLiteral("args"),
             QJsonObject{{QStringLiteral("pid"), QCoreApplication::applicationPid()},
                         {QStringLiteral("activity"), activity}}},
            {QStringLiteral("nonce"), QString::number(++nonce)}};
        if (!Send(current, Opcode::Frame, QJsonDocument{msg}.toJson(QJsonDocument::Compact))) {
            return Reconnect(current);
        }
        sent_activity = activity;
        limited = true;
        cooldown.expires_after(ActivityInterval);
        cooldown.async_wait([this](Error ec) {
            if (!ec) {
                limited = false;
                Flush();
            }
        });
    }

    asio::io_context io;
    asio::steady_timer retry{io}, cooldown{io};
    QStringList endpoints;
    qsizetype next_endpoint = 0;
    std::shared_ptr<Connection> current;
    QJsonValue activity{QJsonValue::Undefined}, sent_activity{QJsonValue::Undefined};
    quint64 nonce = 0;
    bool limited = false;
    std::thread worker;
};

Client::Client(QStringList endpoints) : impl{std::make_unique<Impl>(std::move(endpoints))} {}
Client::~Client() = default;

void Client::SetActivity(const QJsonValue& activity) {
    if (!activity.isObject() && !activity.isNull())
        return;
    asio::post(impl->io, [state = impl.get(), activity] { state->SetActivity(activity); });
}

} // namespace Discord
