#pragma once

#include <QByteArray>
#include <QDialog>
#include <QJsonObject>
#include <QLocalSocket>
#include <QString>

// The Config window owns a normal authenticated modules.runtime session.
// This transport never invokes OS platform operations or stores credentials.
class ProxyRuntimeSession final {
public:
    ProxyRuntimeSession() = default;
    ~ProxyRuntimeSession();

    bool registerConfig(QDialog *window, QString &error);

    ProxyRuntimeSession(const ProxyRuntimeSession &) = delete;
    ProxyRuntimeSession &operator=(const ProxyRuntimeSession &) = delete;

private:
    static constexpr quint32 kMaxFrameBytes = 1024u * 1024u;
    static constexpr quint32 kProtocol = 1u;

    bool sendFrame(const QJsonObject &message);
    bool readExactly(QByteArray &data, qsizetype count, QString &error);
    bool readRegistration(QJsonObject &message, QString &error);
    void consumeFrames();
    void handleFrame(const QJsonObject &message);
    void terminateSession();

    QLocalSocket socket_;
    QDialog *window_ = nullptr;
    QString sessionId_;
    QByteArray pending_;
    bool registered_ = false;
    bool closing_ = false;
};
