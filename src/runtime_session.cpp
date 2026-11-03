#include "runtime_session.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>
#include <QtGlobal>

namespace {
const QString kModule = QStringLiteral("neebles-proxy-client");
const QString kCanonicalSocket = QStringLiteral("/run/neebles/modules.sock");

quint32 frameLength(const char *bytes) {
    const auto *p = reinterpret_cast<const unsigned char *>(bytes);
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16)
         | (quint32(p[2]) << 8) | quint32(p[3]);
}
}

ProxyRuntimeSession::~ProxyRuntimeSession() {
    terminateSession();
}

bool ProxyRuntimeSession::sendFrame(const QJsonObject &message) {
    if (socket_.state() != QLocalSocket::ConnectedState) return false;

    const QByteArray json = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (json.isEmpty() || json.size() > qsizetype(kMaxFrameBytes)) return false;

    const quint32 length = quint32(json.size());
    QByteArray frame;
    frame.reserve(4 + json.size());
    frame.append(char((length >> 24) & 0xffu));
    frame.append(char((length >> 16) & 0xffu));
    frame.append(char((length >> 8) & 0xffu));
    frame.append(char(length & 0xffu));
    frame.append(json);

    if (socket_.write(frame) != frame.size()) return false;
    socket_.flush();
    return true;
}

bool ProxyRuntimeSession::readExactly(QByteArray &data, qsizetype count,
                                      QString &error) {
    while (data.size() < count) {
        if (socket_.bytesAvailable() == 0 && !socket_.waitForReadyRead(3000)) {
            error = QStringLiteral("module IPC registration timed out or disconnected: ")
                  + socket_.errorString();
            return false;
        }
        const QByteArray part = socket_.read(count - data.size());
        if (part.isEmpty()) {
            error = QStringLiteral("module IPC registration returned an empty frame");
            return false;
        }
        data.append(part);
    }
    return true;
}

bool ProxyRuntimeSession::readRegistration(QJsonObject &message, QString &error) {
    QByteArray header;
    if (!readExactly(header, 4, error)) return false;
    const quint32 length = frameLength(header.constData());
    if (length == 0u || length > kMaxFrameBytes) {
        error = QStringLiteral("invalid module IPC registration frame length");
        return false;
    }
    QByteArray bytes;
    if (!readExactly(bytes, qsizetype(length), error)) return false;
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("module IPC registration response is invalid JSON");
        return false;
    }
    message = document.object();
    return true;
}

bool ProxyRuntimeSession::registerConfig(QDialog *window, QString &error) {
    if (window == nullptr || registered_ || window_ != nullptr) {
        error = QStringLiteral("invalid runtime session lifecycle");
        return false;
    }
    window_ = window;
    sessionId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString socketPath = qEnvironmentVariable("NEEBLES_MODULES_SOCKET");
    if (socketPath.isEmpty()) socketPath = kCanonicalSocket;
    if (!socketPath.startsWith(QLatin1Char('/'))) {
        error = QStringLiteral("module IPC socket path must be absolute");
        return false;
    }
    socket_.connectToServer(socketPath);
    if (!socket_.waitForConnected(3000)) {
        error = QStringLiteral("cannot connect to Boss Module IPC: ") + socket_.errorString();
        return false;
    }

    const QJsonObject registration{
        {QStringLiteral("type"), QStringLiteral("register")},
        {QStringLiteral("protocol"), int(kProtocol)},
        {QStringLiteral("module"), kModule},
        {QStringLiteral("session_id"), sessionId_},
        {QStringLiteral("endpoints"), QJsonObject{}}
    };
    if (!sendFrame(registration) ||
        (socket_.bytesToWrite() > 0 && !socket_.waitForBytesWritten(3000))) {
        error = QStringLiteral("could not send module runtime registration");
        return false;
    }
    QJsonObject reply;
    if (!readRegistration(reply, error)) return false;
    if (reply.value(QStringLiteral("type")).toString()
          != QStringLiteral("registered") ||
        reply.value(QStringLiteral("protocol")).toInt(-1) != int(kProtocol) ||
        reply.value(QStringLiteral("module")).toString() != kModule ||
        reply.value(QStringLiteral("session_id")).toString() != sessionId_) {
        error = QStringLiteral("Boss rejected or mismatched module runtime registration: ")
              + QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact));
        return false;
    }
    registered_ = true;

    QObject::connect(&socket_, &QLocalSocket::readyRead, window_,
                     [this]() { consumeFrames(); });
    QObject::connect(&socket_, &QLocalSocket::disconnected, window_, [this]() {
        if (!closing_ && window_) {
            closing_ = true;
            window_->reject();
        }
    });
    QObject::connect(window_, &QDialog::finished, window_,
                     [this](int) { terminateSession(); });
    if (socket_.bytesAvailable() > 0) consumeFrames();
    return true;
}

void ProxyRuntimeSession::handleFrame(const QJsonObject &message) {
    const QString type = message.value(QStringLiteral("type")).toString();
    if (message.value(QStringLiteral("module")).toString() != kModule ||
        message.value(QStringLiteral("session_id")).toString() != sessionId_) {
        if (window_) window_->reject();
        return;
    }
    if (type == QStringLiteral("ping")) {
        if (!sendFrame({
                {QStringLiteral("type"), QStringLiteral("pong")},
                {QStringLiteral("module"), kModule},
                {QStringLiteral("session_id"), sessionId_}
            }) && window_) window_->reject();
        return;
    }
    if (type == QStringLiteral("shutdown")) {
        sendFrame({
            {QStringLiteral("type"), QStringLiteral("shutdown_ack")},
            {QStringLiteral("module"), kModule},
            {QStringLiteral("session_id"), sessionId_}
        });
        if (socket_.bytesToWrite() > 0) socket_.waitForBytesWritten(500);
        closing_ = true;
        if (window_) window_->reject();
        return;
    }
    // No Commands endpoints were advertised; unexpected runtime requests
    // must fail closed rather than acquiring authority implicitly.
    if (window_) window_->reject();
}

void ProxyRuntimeSession::consumeFrames() {
    pending_.append(socket_.readAll());
    if (pending_.size() > qsizetype(kMaxFrameBytes) + 4) {
        if (window_) window_->reject();
        return;
    }
    while (pending_.size() >= 4) {
        const quint32 length = frameLength(pending_.constData());
        if (length == 0u || length > kMaxFrameBytes) {
            if (window_) window_->reject();
            return;
        }
        if (pending_.size() < qsizetype(length) + 4) break;
        const QByteArray body = pending_.mid(4, qsizetype(length));
        pending_.remove(0, qsizetype(length) + 4);
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(body, &parse);
        if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
            if (window_) window_->reject();
            return;
        }
        handleFrame(doc.object());
        if (closing_) return;
    }
}

void ProxyRuntimeSession::terminateSession() {
    if (!registered_) return;
    const bool alreadyClosing = closing_;
    closing_ = true;
    registered_ = false;
    if (!alreadyClosing && socket_.state() == QLocalSocket::ConnectedState) {
        sendFrame({
            {QStringLiteral("type"), QStringLiteral("unregister")},
            {QStringLiteral("module"), kModule},
            {QStringLiteral("session_id"), sessionId_},
            {QStringLiteral("reason"), QStringLiteral("config_closed")}
        });
        if (socket_.bytesToWrite() > 0) socket_.waitForBytesWritten(500);
    }
    socket_.disconnectFromServer();
}
