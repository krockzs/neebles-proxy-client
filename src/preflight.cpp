#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QTextStream>
static bool validTailIp(const QString &s) {
    QHostAddress a;
    if (!a.setAddress(s) || a.protocol()!=QAbstractSocket::IPv4Protocol) return false;
    return (a.toIPv4Address() & 0xffc00000U)==0x64400000U;
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QCommandLineParser cli;
    cli.setApplicationDescription("NEEBLES Proxy Client read-only preflight; does not change routes");
    cli.addHelpOption();
    cli.addOption({{"s","server"},"Server Tailscale IPv4","ipv4"});

    cli.addOption({{"t", "tailnet"}, "Expected Tailscale tailnet", "name"});
    cli.process(app);
    auto fail=[](const QString &reason,int rc) {
        const QJsonObject result{{"ok",false},{"reason",reason},{"routes_modified",false}};
        QTextStream(stdout)<<QJsonDocument(result).toJson(QJsonDocument::Compact)<<'\n';
        return rc;
    };

    const QString ip=cli.value("server");
    const QString tailnet=cli.value("tailnet").trimmed();

    if (!validTailIp(ip)) return fail("invalid_server_ip",2);
    if (tailnet.isEmpty() || tailnet.size() > 255 || tailnet.contains('\n'))
        return fail("expected_tailnet_required",2);
    QProcess tailscale;
    tailscale.start("/usr/bin/tailscale",{"status","--json"});
    if (!tailscale.waitForStarted(2000) || !tailscale.waitForFinished(5000) || tailscale.exitStatus()!=QProcess::NormalExit || tailscale.exitCode()!=0)
        return fail("tailscale_unavailable",3);
    const auto doc=QJsonDocument::fromJson(tailscale.readAllStandardOutput());
    if (!doc.isObject()) return fail("tailscale_invalid_status",3);
    const auto data=doc.object();
    if (data.value("BackendState").toString()!="Running" ||
        data.value("CurrentTailnet").toObject().value("Name").toString()!=tailnet)
        return fail("wrong_or_inactive_tailnet",4);
    QNetworkAccessManager network;
    QNetworkRequest request{QUrl(QString("http://%1:18879/health").arg(ip))};
    QNetworkReply *reply=network.get(request);
    QEventLoop loop;
    QTimer timeout; timeout.setSingleShot(true);
    QObject::connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);
    QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
    timeout.start(7000);
    if (!reply->isFinished()) loop.exec();
    if (!reply->isFinished()) {reply->abort();reply->deleteLater();return fail("health_timeout",5);}
    const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray response=reply->readAll(); reply->deleteLater();
    const auto health=QJsonDocument::fromJson(response);
    if (code!=200 || !health.isObject()) return fail("health_unavailable",5);
    const auto state=health.object();
    if (state.value("service").toString()!="neebles-proxy-server" ||
        state.value("tailscale_ipv4").toString()!=ip ||
        state.value("health").toString()!="reachable") return fail("health_identity_mismatch",6);
    // Tailscale's control plane is the authority for advertised/approved exit nodes.
    // Server Health indicates service reachability only: its legacy flags do not
    // establish whether the node is approved in the Tailscale admin console.
    const QJsonObject peers=data.value("Peer").toObject();
    bool exitAvailable=false;
    bool exitOnline=false;
    for (auto it=peers.begin();it!=peers.end();++it) {
        const QJsonObject peer=it.value().toObject();
        const QJsonArray addresses=peer.value("TailscaleIPs").toArray();
        bool matched=false;
        for (const QJsonValue &address:addresses) {
            if (address.toString()==ip) {matched=true;break;}
        }
        if (matched) {
            exitOnline=peer.value("Online").toBool();
            exitAvailable=peer.value("ExitNodeOption").toBool() && exitOnline;
            break;
        }
    }
    const QJsonObject result{{"ok",true},{"server",ip},{"tailnet",tailnet},
        {"health_reachable",true},{"exit_node_advertised",exitAvailable},
        {"exit_node_online",exitOnline},
        {"exit_ready_legacy",state.value("exit_ready").toBool()},
        {"authorization_enforced_legacy",state.value("authorization_enforced").toBool()},
        {"activation_allowed",exitAvailable},
        {"authorization_note","tailscale_acl_verified_only_by_control_plane_when_activated"},
        {"routes_modified",false}};
    QTextStream(stdout)<<QJsonDocument(result).toJson(QJsonDocument::Compact)<<'\n';
    // A reachable Health endpoint is not sufficient authorization to activate.
    // Return failure when the Exit Node is not advertised and online.
    return exitAvailable ? 0 : 7;
}
