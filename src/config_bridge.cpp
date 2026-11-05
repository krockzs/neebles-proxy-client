#include "config_bridge.h"
#include "config_secrets.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStringList>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTextStream>
#include <QTimer>
#include <sys/socket.h>
#include <sys/stat.h>
#include <cerrno>
#include <sys/types.h>
#include <unistd.h>

namespace ProxyConfigBridge {

// Presentation-only recovery. The long-lived Bridge may start before KDE
// imports DISPLAY/XAUTHORITY into the systemd --user manager. Query the
// current authenticated user manager at presentation time; do not touch
// privileged routing, global environment, vault data or the service state.
static bool recoverGraphicalEnvironment(QProcessEnvironment &environment) {
    QProcess manager;
    manager.start(QStringLiteral("/usr/bin/systemctl"),
                  {QStringLiteral("--user"), QStringLiteral("show-environment")});
    if (!manager.waitForStarted(400) || !manager.waitForFinished(1200) ||
        manager.exitStatus() != QProcess::NormalExit || manager.exitCode() != 0) {
        if (manager.state() != QProcess::NotRunning) {
            manager.kill();
            manager.waitForFinished(150);
        }
        return false;
    }
    const QByteArray bytes = manager.readAllStandardOutput();
    if (bytes.size() > 65536) return false;
    QString display;
    QString xauthority;
    QString wayland;
    for (const QString &line : QString::fromUtf8(bytes).split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("DISPLAY=")))
            display = line.mid(sizeof("DISPLAY=") - 1);
        else if (line.startsWith(QStringLiteral("XAUTHORITY=")))
            xauthority = line.mid(sizeof("XAUTHORITY=") - 1);
        else if (line.startsWith(QStringLiteral("WAYLAND_DISPLAY=")))
            wayland = line.mid(sizeof("WAYLAND_DISPLAY=") - 1);
    }
    // Local desktop presentation only. Never trust remote X11 display strings.
    const bool x11 = QRegularExpression(
        QStringLiteral(R"(^:[0-9]{1,3}(\.[0-9]{1,2})?$)")).match(display).hasMatch();
    if (x11) {
        QFileInfo auth(xauthority);
        if (!auth.isAbsolute() || !auth.isFile() || auth.isSymLink() ||
            auth.ownerId() != ::geteuid() ||
            (auth.permissions() & (QFileDevice::WriteGroup | QFileDevice::WriteOther)))
            return false;
        environment.insert(QStringLiteral("DISPLAY"), display);
        environment.insert(QStringLiteral("XAUTHORITY"), xauthority);
        environment.remove(QStringLiteral("WAYLAND_DISPLAY"));
        return true;
    }
    if (QRegularExpression(QStringLiteral(R"(^wayland-[0-9]{1,3}$)"))
            .match(wayland).hasMatch()) {
        environment.insert(QStringLiteral("WAYLAND_DISPLAY"), wayland);
        environment.remove(QStringLiteral("DISPLAY"));
        environment.remove(QStringLiteral("XAUTHORITY"));
        return true;
    }
    return false;
}

// Secrets error strings are untrusted; only explicitly allowed fixed codes
// may appear in the journal. No vault values, server IPs or raw IPC replies.
static QString safeSecretsFailureCode(const QString &error) {
    static const QStringList permitted = {
        QStringLiteral("module_identity_mismatch"),
        QStringLiteral("invalid_runtime_directory"),
        QStringLiteral("invalid_ipc_request"),
        QStringLiteral("secrets_unavailable"),
        QStringLiteral("secrets_ipc_write_failed"),
        QStringLiteral("secrets_ipc_write_timeout"),
        QStringLiteral("secrets_ipc_read_timeout"),
        QStringLiteral("secrets_invalid_response_size"),
        QStringLiteral("secrets_invalid_response"),
        QStringLiteral("secrets_operation_denied"),
        QStringLiteral("secrets_response_missing_data"),
        QStringLiteral("config_identity_mismatch"),
        QStringLiteral("invalid_stored_server_ip"),
        QStringLiteral("entity_not_found")
    };
    return permitted.contains(error) ? error : QStringLiteral("other");
}


int serve(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    // Solo se permite el directorio de runtime correspondiente al usuario.
    if (runtime != QStringLiteral("/run/user/%1").arg(::geteuid())) {
        QTextStream(stderr) << "CONFIG_BRIDGE_DENIED=INVALID_RUNTIME_IDENTITY\n";
        return 3;
    }
    const QString directory = runtime + QStringLiteral("/neebles/proxy-client");
    if (!QDir().mkpath(directory)) return 4;
    // Both path components must be real, owned, private directories.
    for (const QString &checked : {runtime + QStringLiteral("/neebles"), directory}) {
        struct stat info{};
        const QByteArray native = checked.toLocal8Bit();
        if (::lstat(native.constData(), &info) != 0 || !S_ISDIR(info.st_mode) ||
            info.st_uid != ::geteuid() || (info.st_mode & 0022) != 0) {
            QTextStream(stderr) << "CONFIG_BRIDGE_DENIED=UNSAFE_RUNTIME_DIRECTORY\n";
            return 4;
        }
        if (!QFile::setPermissions(checked,
                QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                QFileDevice::ExeOwner)) return 4;
    }
    const QString path = directory + QStringLiteral("/bridge.sock");
    // Reiniciar exclusivamente el socket abandonado que pertenece a este UID.
    // Una instancia activa o un archivo no-socket nunca se sustituye.
    struct stat socketInfo{};
    const QByteArray nativePath = path.toLocal8Bit();
    if (::lstat(nativePath.constData(), &socketInfo) == 0) {
        if (!S_ISSOCK(socketInfo.st_mode) || socketInfo.st_uid != ::geteuid() ||
            (socketInfo.st_mode & 0077) != 0) {
            QTextStream(stderr) << "CONFIG_BRIDGE_DENIED=FOREIGN_SOCKET_PATH\n";
            return 5;
        }
        QLocalSocket probe;
        probe.connectToServer(path);
        if (probe.waitForConnected(300)) {
            probe.disconnectFromServer();
            QTextStream(stderr) << "CONFIG_BRIDGE_DENIED=ALREADY_RUNNING\n";
            return 5;
        }
        // A timeout or unrelated socket error is not evidence of a stale socket.
        // Only an explicitly refused/absent endpoint may be reclaimed.
        if (probe.error() != QLocalSocket::ConnectionRefusedError &&
            probe.error() != QLocalSocket::ServerNotFoundError) {
            QTextStream(stderr) << "CONFIG_BRIDGE_DENIED=SOCKET_STATE_AMBIGUOUS\n";
            return 5;
        }
        struct stat current{};
        if (::lstat(nativePath.constData(), &current) != 0 ||
            !S_ISSOCK(current.st_mode) || current.st_uid != ::geteuid() ||
            current.st_dev != socketInfo.st_dev ||
            current.st_ino != socketInfo.st_ino ||
            (current.st_mode & 0077) != 0) {
            QTextStream(stderr) << "CONFIG_BRIDGE_DENIED=SOCKET_CHANGED\n";
            return 5;
        }
        if (!QLocalServer::removeServer(path)) {
            QTextStream(stderr) << "CONFIG_BRIDGE_DENIED=STALE_SOCKET_REMOVE_FAILED\n";
            return 5;
        }
    } else if (errno != ENOENT) {
        QTextStream(stderr) << "CONFIG_BRIDGE_DENIED=SOCKET_LSTAT_FAILED\n";
        return 5;
    }
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(path)) {
        QTextStream(stderr) << "CONFIG_BRIDGE_DENIED=LISTEN_FAILED\n";
        return 6;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    QString knownServerIp; // RAM only; updated on privileged bridge requests.
    bool initialized = false;
    bool ownActive = false;
    // A confirmed OS-provider notification avoids missing rapid changes between
    // one-second polls. Polling remains a read-only fallback and reconciler.
    auto present = [&app, &initialized, &ownActive](bool state, bool confirmed) {
        if (!initialized) {
            initialized = true;
            ownActive = state;
            if (!confirmed) return; // Do not animate the pre-existing login state.
        } else {
            if (ownActive == state) return;
            ownActive = state;
        }
        const QString action = state ? QStringLiteral("--show-active")
                                     : QStringLiteral("--show-inactive");
        const QString installed = QCoreApplication::applicationFilePath();
        bool launched = false;
        // The current process may be older than the desktop-session import.
        // Preserve the existing direct launch path whenever it is complete.
        if (!qEnvironmentVariableIsEmpty("DISPLAY") &&
            !qEnvironmentVariableIsEmpty("XAUTHORITY")) {
            launched = QProcess::startDetached(installed, {action},
                                                QCoreApplication::applicationDirPath());
        } else {
            QProcessEnvironment desktop = QProcessEnvironment::systemEnvironment();
            if (!recoverGraphicalEnvironment(desktop)) {
                QTextStream(stderr) << "PROXY_CLIENT_ANIMATION_NO_DESKTOP_SESSION\n";
                return;
            }
            QProcess presentation;
            presentation.setProgram(installed);
            presentation.setArguments({action});
            presentation.setWorkingDirectory(QCoreApplication::applicationDirPath());
            presentation.setProcessEnvironment(desktop);
            launched = presentation.startDetached();
        }
        if (!launched) {
            QTextStream(stderr) << "PROXY_CLIENT_ANIMATION_LAUNCH_FAILED\n";
        }
    };
    QObject::connect(&server, &QLocalServer::newConnection, &app,
                     [&server, &knownServerIp, &present, &app]() {
        while (server.hasPendingConnections()) {
            QLocalSocket *peer = server.nextPendingConnection();
            struct ucred cred{};
            socklen_t length = sizeof(cred);
            const bool root = ::getsockopt(int(peer->socketDescriptor()), SOL_SOCKET,
                                           SO_PEERCRED, &cred, &length) == 0 &&
                              length == sizeof(cred) && cred.uid == 0 && cred.pid > 1;
            if (!root) {
                peer->disconnectFromServer();
                peer->deleteLater();
                continue;
            }
            QObject::connect(peer, &QLocalSocket::readyRead, peer,
                             [peer, &knownServerIp, &present, &app]() {
                if (peer->bytesAvailable() > 256) {
                    peer->disconnectFromServer();
                    return;
                }
                if (!peer->canReadLine()) return;
                const QByteArray line = peer->readLine(256);
                QJsonParseError parse{};
                const QJsonDocument request = QJsonDocument::fromJson(line, &parse);
                QJsonObject response{{"schema", "1"}, {"ok", false}};
                if (parse.error == QJsonParseError::NoError && request.isObject() &&
                    request.object().size() == 2 &&
                    request.object().value("schema").toString() == QStringLiteral("1") &&
                    request.object().value("op").toString() == QStringLiteral("server-ip.get")) {
                    QString ip;
                    QString error;
                    if (ProxySecrets::readServerIp(ip, error)) {
                        response.insert("ok", true);
                        response.insert("server_ipv4", ip);
                        knownServerIp = ip;
                    } else {
                        QTextStream(stderr) << "PROXY_CLIENT_SECRETS_DIAG phase=read_server_ip status="
                                            << safeSecretsFailureCode(error) << '\n';
                        response.insert("error", "configuration_unavailable");
                    }
                } else if (parse.error == QJsonParseError::NoError && request.isObject() &&
                           request.object().size() == 3 &&
                           request.object().value("schema").toString() == QStringLiteral("1") &&
                           request.object().value("op").toString() == QStringLiteral("state.confirmed") &&
                           request.object().value("active").isBool()) {
                    response.insert("ok", true);
                    const bool active = request.object().value("active").toBool();
                    // This channel has already verified SO_PEERCRED uid=0. The
                    // privileged helper sends only AFTER Tailscale post-check.
                    QTimer::singleShot(0, &app, [&present, active]() {
                        present(active, true);
                    });
                } else {
                    response.insert("error", "invalid_request");
                }
                peer->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + "\n");
                peer->flush();
                peer->disconnectFromServer();
            });
            QObject::connect(peer, &QLocalSocket::disconnected,
                             peer, &QObject::deleteLater);
        }
    });
    // Post-operation animation follows observed Tailscale state, not an
    // untrusted UI intent. This process remains the user-owned Bridge service;
    // no privileged command or route modification is performed here.
    auto *monitor = new QProcess(&app);
    auto *tick = new QTimer(&app);
    tick->setInterval(1000);
    QObject::connect(monitor,
                     QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &app,
                     [monitor, &knownServerIp, &present]
                     (int exitCode, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || exitCode != 0) return;
        const QJsonDocument document = QJsonDocument::fromJson(monitor->readAllStandardOutput());
        if (!document.isObject()) return;
        const QJsonObject tailscale = document.object();
        if (tailscale.value(QStringLiteral("BackendState")).toString()
                != QStringLiteral("Running")) return;
        // The bridge caches the Secrets-verified IP only after a canonical
        // privileged request. Polling never contacts Secrets or blocks IPC.
        const QJsonObject selected =
            tailscale.value(QStringLiteral("ExitNodeStatus")).toObject();
        bool currentActive = false;
        if (!selected.isEmpty()) {
            if (knownServerIp.isEmpty()) return;
            for (const QJsonValue &address : selected.value("TailscaleIPs").toArray()) {
                const QString ip = address.toString();
                if (ip == knownServerIp || ip == knownServerIp + "/32") {
                    currentActive = true;
                    break;
                }
            }
            if (!currentActive) return; // A foreign node is not Proxy Client.
        }
        present(currentActive, false);
    });
    QObject::connect(tick, &QTimer::timeout, &app, [monitor]() {
        if (monitor->state() != QProcess::NotRunning) return;
        monitor->start(QStringLiteral("/usr/bin/tailscale"),
                       {QStringLiteral("status"), QStringLiteral("--json")});
        // Prevent a stalled Tailscale CLI from delaying later observations.
        if (!monitor->waitForStarted(300)) return;
        const qint64 pid = monitor->processId();
        QTimer::singleShot(2700, monitor, [monitor, pid]() {
            if (monitor->state() != QProcess::NotRunning &&
                monitor->processId() == pid) monitor->kill();
        });
    });
    tick->start();
    QTextStream(stdout) << "PROXY_CLIENT_CONFIG_BRIDGE_READY\n";
    return app.exec();
}
} // namespace ProxyConfigBridge
