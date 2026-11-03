// Operaciones de Exit Node mediante el proveedor OS controlado por Boss.
// La IP elegida procede exclusivamente del puente de configuracion del
// ejecutable canonico Client, que consulta su boveda Secrets en cada solicitud.
// El proveedor root fija la tailnet; el UID procede de la identidad OS
// gestionada por Boss en /etc/neebles/runtime.env.
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QTextStream>
#include <algorithm>
#include <fcntl.h>
#include <optional>
#include <QByteArray>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
constexpr qint64 kTotalBudgetMs = 8500;
constexpr int kMaxReplyBytes = 4096;
const QString kCanonicalClient = QStringLiteral(
    "/opt/neebles/modules/neebles-proxy-client/runtime/neebles-proxy-client");
QElapsedTimer clockBudget;

int remaining(int cap) {
    const qint64 left = kTotalBudgetMs - clockBudget.elapsed();
    return left > 0 ? int(std::min<qint64>(left, cap)) : 0;
}

bool validIp(const QString &ip) {
    QHostAddress address;
    return address.setAddress(ip) &&
           address.protocol() == QAbstractSocket::IPv4Protocol &&
           (address.toIPv4Address() & 0xffc00000U) == 0x64400000U;
}

bool trustedCanonical(const pid_t pid) {
    // Comparar INODO fisico en vez de confiar en un texto enviado por IPC.
    if (pid <= 1) return false;
    struct stat installed{}, observed{};
    // Verificar propietario de la ruta instalada y prohibir symlinks.
    QString partial;
    for (const auto &part : kCanonicalClient.split('/', Qt::SkipEmptyParts)) {
        partial += "/" + part;
        struct stat component{};
        const QByteArray bytes = partial.toLocal8Bit();
        if (::lstat(bytes.constData(), &component) != 0 ||
            component.st_uid != 0 || (component.st_mode & 0022) != 0 ||
            S_ISLNK(component.st_mode)) return false;
    }
    const QByteArray executable = kCanonicalClient.toLocal8Bit();
    if (::stat(executable.constData(), &installed) != 0 ||
        !S_ISREG(installed.st_mode) || !(installed.st_mode & 0111)) return false;
    const QByteArray procExe = QStringLiteral("/proc/%1/exe").arg(pid).toLocal8Bit();
    if (::stat(procExe.constData(), &observed) != 0) return false;
    return installed.st_dev == observed.st_dev && installed.st_ino == observed.st_ino;
}

// The desktop identity is taken only from the root-owned Boss installation
// authority, never from caller-supplied IPC, CLI arguments or the environment.
// Path components are opened with O_NOFOLLOW to prevent symlink substitution.
std::optional<uid_t> desktopUidFromOs() {
    int directory = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) return std::nullopt;
    for (const char *component : {"etc", "neebles"}) {
        const int next = ::openat(directory, component,
                                  O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        ::close(directory);
        if (next < 0) return std::nullopt;
        directory = next;
        struct stat meta{};
        if (::fstat(directory, &meta) != 0 || !S_ISDIR(meta.st_mode) ||
            meta.st_uid != 0 || (meta.st_mode & 0022) != 0) {
            ::close(directory);
            return std::nullopt;
        }
    }

    const int descriptor = ::openat(directory, "runtime.env",
                                    O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    ::close(directory);
    if (descriptor < 0) return std::nullopt;
    struct stat info{};
    if (::fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_uid != 0 || (info.st_mode & 0022) != 0 ||
        info.st_nlink != 1 || info.st_size <= 0 || info.st_size > 4096) {
        ::close(descriptor);
        return std::nullopt;
    }

    QByteArray contents;
    char buffer[4096];
    while (contents.size() < 4096) {
        const ssize_t size = ::read(descriptor, buffer, sizeof(buffer));
        if (size < 0) {
            ::close(descriptor);
            return std::nullopt;
        }
        if (size == 0) break;
        contents.append(buffer, qsizetype(size));
    }
    ::close(descriptor);
    if (contents.size() != info.st_size || contents.contains('\0')) return std::nullopt;

    bool haveUid = false;
    bool haveGid = false;
    uid_t uid = 0;
    for (const QByteArray &line : contents.split('\n')) {
        if (line.startsWith("NEEBLES_DESKTOP_UID=")) {
            if (haveUid) return std::nullopt;
            bool parsed = false;
            const uint number = line.mid(sizeof("NEEBLES_DESKTOP_UID=") - 1)
                                    .toUInt(&parsed);
            if (!parsed || number == 0 || number > 60000) return std::nullopt;
            uid = uid_t(number);
            haveUid = true;
        } else if (line.startsWith("NEEBLES_DESKTOP_GID=")) {
            if (haveGid) return std::nullopt;
            bool parsed = false;
            const uint number = line.mid(sizeof("NEEBLES_DESKTOP_GID=") - 1)
                                    .toUInt(&parsed);
            if (!parsed || number == 0 || number > 60000) return std::nullopt;
            haveGid = true;
        }
    }
    if (!haveUid || !haveGid) return std::nullopt;
    return uid;
}

// Read-only presentation notification: independent from routing authority.
// Only the already authenticated canonical Bridge may receive this event.
// Failure to display the splash does not invalidate a verified route change.
void notifyConfirmedState(uid_t uid, bool active) {
    const QString path = QStringLiteral("/run/user/%1/neebles/proxy-client/bridge.sock")
                             .arg(qulonglong(uid));
    struct stat info{};
    if (::lstat(path.toLocal8Bit().constData(), &info) != 0 ||
        !S_ISSOCK(info.st_mode) || info.st_uid != uid ||
        (info.st_mode & 0077) != 0) return;
    QLocalSocket socket;
    socket.connectToServer(path);
    if (!socket.waitForConnected(500)) return;
    struct ucred cred{};
    socklen_t size = sizeof(cred);
    if (::getsockopt(int(socket.socketDescriptor()), SOL_SOCKET,
                     SO_PEERCRED, &cred, &size) != 0 ||
        size != sizeof(cred) || cred.uid != uid ||
        !trustedCanonical(cred.pid)) return;
    const QByteArray request = QJsonDocument(QJsonObject{
        {"schema", "1"}, {"op", "state.confirmed"}, {"active", active}
    }).toJson(QJsonDocument::Compact) + '\n';
    if (socket.write(request) != request.size()) return;
    socket.waitForBytesWritten(500);
}

bool configuredServer(uid_t uid, QString &serverIp) {
    const QString path = QStringLiteral("/run/user/%1/neebles/proxy-client/bridge.sock").arg(qulonglong(uid));
    struct stat info{};
    if (::lstat(path.toLocal8Bit().constData(), &info) != 0 ||
        !S_ISSOCK(info.st_mode) || info.st_uid != uid ||
        (info.st_mode & 0077) != 0) return false;
    QLocalSocket socket;
    socket.connectToServer(path);
    const int connectMs = remaining(900);
    if (!connectMs || !socket.waitForConnected(connectMs)) return false;
    struct ucred cred{};
    socklen_t size = sizeof(cred);
    if (::getsockopt(int(socket.socketDescriptor()), SOL_SOCKET,
                     SO_PEERCRED, &cred, &size) != 0 ||
        size != sizeof(cred) || cred.uid != uid ||
        !trustedCanonical(cred.pid)) return false;
    const QByteArray request =
        "{\"schema\":\"1\",\"op\":\"server-ip.get\"}\n";
    if (socket.write(request) != request.size()) return false;
    const int flushMs = remaining(900);
    if (!flushMs || !socket.waitForBytesWritten(flushMs)) return false;
    QByteArray answer;
    while (!answer.contains('\n')) {
        if (socket.bytesAvailable()) {
            answer += socket.readAll();
            if (answer.size() > kMaxReplyBytes) return false;
            continue;
        }
        const int ms = remaining(1000);
        if (!ms || !socket.waitForReadyRead(ms)) return false;
    }
    if (answer.indexOf('\n') != answer.size() - 1) return false;
    QJsonParseError err{};
    const QJsonDocument reply = QJsonDocument::fromJson(answer.trimmed(), &err);
    if (err.error != QJsonParseError::NoError || !reply.isObject()) return false;
    const QJsonObject value = reply.object();
    if (value.size() != 3 || value.value("schema").toString() != "1" ||
        value.value("ok").toBool() != true ||
        !validIp(value.value("server_ipv4").toString())) return false;
    serverIp = value.value("server_ipv4").toString();
    return true;
}

bool tailscale(const QStringList &args, QJsonObject *status = nullptr) {
    QProcess process;
    process.start(QStringLiteral("/usr/bin/tailscale"), args);
    const int startup = remaining(800);
    if (!startup || !process.waitForStarted(startup)) {
        if (process.state() != QProcess::NotRunning) process.kill();
        return false;
    }
    const int duration = remaining(2200);
    if (!duration || !process.waitForFinished(duration) ||
        process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (process.state() != QProcess::NotRunning) {
            process.kill(); process.waitForFinished(100);
        }
        return false;
    }
    if (status) {
        const auto parsed = QJsonDocument::fromJson(process.readAllStandardOutput());
        if (!parsed.isObject()) return false;
        *status = parsed.object();
    }
    return true;
}

// The privileged provider is deliberately restricted to AF_UNIX. Remote
// HTTP health checks belong to the unprivileged preflight executable.
// The privileged activation authority is Tailscale's current tailnet, the
// peer's online/approved Exit Node status, and the verified post-action state.
bool anyExitNodeSelected(const QJsonObject &status) {
    const QJsonValue value = status.value(QStringLiteral("ExitNodeStatus"));
    return value.isObject() && !value.toObject().isEmpty();
}

bool ownTailnet(const QJsonObject &state, const QString &tailnet) {
    return state.value("BackendState").toString() == "Running" &&
           state.value("CurrentTailnet").toObject().value("Name").toString() == tailnet;
}

bool selected(const QJsonObject &status, const QString &ip) {
    const auto selection = status.value("ExitNodeStatus").toObject();
    for (const auto &item : selection.value("TailscaleIPs").toArray()) {
        if (item.toString() == ip || item.toString() == ip + "/32") return true;
    }
    return false;
}

bool approved(const QJsonObject &status, const QString &ip) {
    for (const auto &item : status.value("Self").toObject().value("TailscaleIPs").toArray())
        if (item.toString() == ip) return false;
    const auto peers = status.value("Peer").toObject();
    for (auto it = peers.constBegin(); it != peers.constEnd(); ++it) {
        const QJsonObject peer = it.value().toObject();
        if (!peer.value("Online").toBool() ||
            !peer.value("ExitNodeOption").toBool()) continue;
        for (const auto &address : peer.value("TailscaleIPs").toArray())
            if (address.toString() == ip) return true;
    }
    return false;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser cli;
    cli.setApplicationDescription("NEEBLES Proxy Client native Exit Node operation - OS provider only");
    cli.addHelpOption();
    cli.addOption({"activate", "Select Secrets-configured Exit Node"});
    cli.addOption({"deactivate", "Clear the currently selected Exit Node"});
    cli.addOption({"inspect", "Inspect matching Exit Node"});
    cli.addOption({"tailnet", "Root-policy-supplied expected tailnet", "name"});
    cli.process(app);
    auto deny = [](const char *reason, int code) {
        QTextStream(stderr) << "DENIED: " << reason << '\n';
        return code;
    };
    const int count = int(cli.isSet("activate")) + int(cli.isSet("deactivate")) +
                      int(cli.isSet("inspect"));
    if (count != 1 || !cli.isSet("tailnet"))
        return deny("invalid OS policy arguments", 2);
    if (::geteuid() != 0) return deny("root OS provider required", 3);
    const std::optional<uid_t> user = desktopUidFromOs();
    const QString tailnet = cli.value("tailnet").trimmed();
    if (!user.has_value() || tailnet.isEmpty() ||
        tailnet.size() > 255 || tailnet.contains('\n'))
        return deny("invalid OS policy identity", 4);

    clockBudget.start();
    QJsonObject before;
    if (!tailscale({"status", "--json"}, &before) || !ownTailnet(before, tailnet))
        return deny("Tailscale state or tailnet invalid", 6);

    // Deactivation never depends on a reachable Server or an unlocked vault.
    // This permits recovery when Secrets is locked, the IP has changed, or the
    // previously selected Exit Node is offline. The operation only clears routing.
    if (cli.isSet("deactivate")) {
        if (!anyExitNodeSelected(before)) {
            QTextStream(stdout) << "OK\n";
            return 0;
        }
        if (!tailscale({"set", "--exit-node="}))
            return deny("Tailscale deactivation failed", 9);
        QJsonObject after;
        if (!tailscale({"status", "--json"}, &after) ||
            !ownTailnet(after, tailnet) || anyExitNodeSelected(after))
            return deny("deactivation could not be verified", 10);
        notifyConfirmedState(*user, false);
        QTextStream(stdout) << "OK\n";
        return 0;
    }

    QString ip;
    if (!configuredServer(*user, ip))
        return deny("canonical Secrets configuration bridge unavailable", 5);
    const bool active = selected(before, ip);
    if (anyExitNodeSelected(before) && !active)
        return deny("a different Exit Node is already selected", 7);
    if (cli.isSet("inspect")) {
        QTextStream(stdout) << (active ? "active" : "inactive") << '\n';
        return 0;
    }
    if (!approved(before, ip))
        return deny("configured peer is not an online approved Exit Node", 8);
    // Do not open AF_INET sockets in the root-owned platform provider.
    // A separate unprivileged preflight can verify Server HTTP Health.
    // A Tailscale-layer ping uses the local daemon API over AF_UNIX; the
    // privileged helper does not open a direct AF_INET HTTP connection.
    if (!tailscale({"ping", "--c=1", "--timeout=2s", "--until-direct=false", ip}))
        return deny("configured Exit Node unreachable over Tailscale", 11);
    if (active) {
        QTextStream(stdout) << "OK\n";
        return 0;
    }
    if (!tailscale({"set", "--exit-node=" + ip}))
        return deny("Tailscale activation failed", 9);
    QJsonObject after;
    if (!tailscale({"status", "--json"}, &after) ||
        !ownTailnet(after, tailnet) || !selected(after, ip)) {
        // Do not retain an ambiguously selected Exit Node after a failed gate.
        tailscale({"set", "--exit-node="});
        return deny("activation could not be verified", 10);
    }
    notifyConfirmedState(*user, true);
    QTextStream(stdout) << "OK\n";
    return 0;
}
