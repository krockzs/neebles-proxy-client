// Module-owned configuration stored exclusively in NEEBLES Secrets 1.0.7.
// The canonical installed Proxy Client executable is the IPC principal.
#include "config_secrets.h"

#include <QDir>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QtEndian>

namespace ProxySecrets {
namespace {

const QString kModule = QStringLiteral("neebles-proxy-client");
const QString kSchema = QStringLiteral("proxy-endpoint");
constexpr int kDeadlineMs = 5000;
constexpr quint32 kMaxFrame = 160 * 1024;

bool remaining(QElapsedTimer &timer, int &milliseconds) {
    milliseconds = kDeadlineMs - int(timer.elapsed());
    return milliseconds > 0;
}

bool readExactly(QLocalSocket &socket, QByteArray &buffer, qsizetype count,
                 QElapsedTimer &timer, QString &error) {
    while (buffer.size() < count) {
        const QByteArray part = socket.read(count - buffer.size());
        if (!part.isEmpty()) {
            buffer.append(part);
            continue;
        }
        int timeout = 0;
        if (!remaining(timer, timeout) || !socket.waitForReadyRead(timeout)) {
            error = QStringLiteral("secrets_ipc_read_timeout");
            return false;
        }
    }
    return true;
}

bool exchange(const QJsonObject &request, QJsonObject &data, QString &error) {
    const QByteArray module = qgetenv("NEEBLES_MODULE");
    if (!module.isEmpty() && module != QByteArray("neebles-proxy-client")) {
        error = QStringLiteral("module_identity_mismatch");
        return false;
    }

    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty() || !QDir::isAbsolutePath(runtime)) {
        error = QStringLiteral("invalid_runtime_directory");
        return false;
    }

    const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
    if (payload.isEmpty() || payload.size() > kMaxFrame) {
        error = QStringLiteral("invalid_ipc_request");
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    QLocalSocket socket;
    socket.connectToServer(runtime + QStringLiteral("/neebles-secrets/secrets.sock"));
    int timeout = 0;
    if (!remaining(timer, timeout) || !socket.waitForConnected(timeout)) {
        error = QStringLiteral("secrets_unavailable");
        return false;
    }

    QByteArray frame(4, '\0');
    qToBigEndian<quint32>(quint32(payload.size()),
                          reinterpret_cast<uchar *>(frame.data()));
    frame.append(payload);
    if (socket.write(frame) != frame.size()) {
        error = QStringLiteral("secrets_ipc_write_failed");
        return false;
    }
    while (socket.bytesToWrite() > 0) {
        if (!remaining(timer, timeout) || !socket.waitForBytesWritten(timeout)) {
            error = QStringLiteral("secrets_ipc_write_timeout");
            return false;
        }
    }

    QByteArray header;
    if (!readExactly(socket, header, 4, timer, error)) return false;
    const quint32 length = qFromBigEndian<quint32>(
        reinterpret_cast<const uchar *>(header.constData()));
    if (length == 0 || length > kMaxFrame) {
        error = QStringLiteral("secrets_invalid_response_size");
        return false;
    }
    QByteArray response;
    if (!readExactly(socket, response, length, timer, error)) return false;

    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(response, &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
        error = QStringLiteral("secrets_invalid_response");
        return false;
    }
    const QJsonObject result = parsed.object();
    if (result.value(QStringLiteral("ok")).toBool() != true) {
        error = result.value(QStringLiteral("error")).toString(
            QStringLiteral("secrets_operation_denied"));
        return false;
    }
    if (!result.value(QStringLiteral("data")).isObject()) {
        error = QStringLiteral("secrets_response_missing_data");
        return false;
    }
    data = result.value(QStringLiteral("data")).toObject();
    return true;
}

QJsonObject getRequest() {
    return QJsonObject{{QStringLiteral("op"), QStringLiteral("structure.get")},
                       {QStringLiteral("module"), kModule},
                       {QStringLiteral("schema_id"), kSchema},
                       {QStringLiteral("id"), QStringLiteral("primary")}};
}

}  // namespace

bool validTailIp(const QString &value) {
    QHostAddress address;
    if (!address.setAddress(value) ||
        address.protocol() != QAbstractSocket::IPv4Protocol) return false;
    return (address.toIPv4Address() & 0xffc00000U) == 0x64400000U;
}

bool ensureVaultAndSchema(QString &error) {
    QJsonObject data;
    if (!exchange(QJsonObject{{QStringLiteral("op"), QStringLiteral("vault.ensure")},
                              {QStringLiteral("module"), kModule}}, data, error)) return false;
    if (data.value(QStringLiteral("module")).toString() != kModule ||
        data.value(QStringLiteral("exists")).toBool() != true) {
        error = QStringLiteral("vault_identity_not_confirmed");
        return false;
    }
    const QJsonObject ipField{
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("required"), true},
        {QStringLiteral("max_length"), 15}};
    const QJsonObject fields{{QStringLiteral("server-ip"), ipField}};
    const QJsonObject schema{
        {QStringLiteral("id"), kSchema},
        {QStringLiteral("version"), 1},
        {QStringLiteral("require_replica"), false},
        {QStringLiteral("fields"), fields},
        {QStringLiteral("unique"), QJsonArray{}}};
    if (!exchange(QJsonObject{{QStringLiteral("op"), QStringLiteral("structure.schema_define")},
                              {QStringLiteral("module"), kModule},
                              {QStringLiteral("schema"), schema}}, data, error)) return false;
    if (data.value(QStringLiteral("id")).toString() != kSchema) {
        error = QStringLiteral("schema_identity_not_confirmed");
        return false;
    }
    return true;
}

bool readServerIp(QString &ip, QString &error) {
    QJsonObject data;
    if (!exchange(getRequest(), data, error)) return false;
    if (data.value(QStringLiteral("id")).toString() != QStringLiteral("primary") ||
        data.value(QStringLiteral("schema_id")).toString() != kSchema) {
        error = QStringLiteral("config_identity_mismatch");
        return false;
    }
    const QString value = data.value(QStringLiteral("value")).toObject()
                              .value(QStringLiteral("server-ip")).toString();
    if (!validTailIp(value)) {
        error = QStringLiteral("invalid_stored_server_ip");
        return false;
    }
    ip = value;
    return true;
}

bool saveServerIp(const QString &ip, QString &error) {
    if (!validTailIp(ip)) {
        error = QStringLiteral("invalid_server_ip");
        return false;
    }

    QJsonObject current;
    const bool exists = exchange(getRequest(), current, error);
    if (!exists && error != QStringLiteral("entity_not_found")) return false;

    QJsonObject request{
        {QStringLiteral("op"), QStringLiteral("structure.put")},
        {QStringLiteral("module"), kModule},
        {QStringLiteral("schema_id"), kSchema},
        {QStringLiteral("id"), QStringLiteral("primary")},
        {QStringLiteral("value"), QJsonObject{{QStringLiteral("server-ip"), ip}}}};
    if (exists) {
        if (current.value(QStringLiteral("id")).toString() != QStringLiteral("primary") ||
            current.value(QStringLiteral("schema_id")).toString() != kSchema ||
            !current.value(QStringLiteral("revision")).isDouble() ||
            current.value(QStringLiteral("revision")).toInt() <= 0) {
            error = QStringLiteral("invalid_stored_revision");
            return false;
        }
        request.insert(QStringLiteral("expected_revision"),
                       current.value(QStringLiteral("revision")));
    }
    QJsonObject result;
    if (!exchange(request, result, error)) return false;
    if (result.value(QStringLiteral("id")).toString() != QStringLiteral("primary") ||
        !result.value(QStringLiteral("revision")).isDouble()) {
        error = QStringLiteral("write_not_confirmed");
        return false;
    }
    QString confirmed;
    if (!readServerIp(confirmed, error) || confirmed != ip) {
        if (error.isEmpty()) error = QStringLiteral("readback_mismatch");
        return false;
    }
    error.clear();
    return true;
}

}  // namespace ProxySecrets
