#include "config_secrets.h"
#include "config_bridge.h"
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QStringList>
#include <QDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QElapsedTimer>
#include <QPixmap>
#include <QCommandLineParser>
#include <QTextStream>
#include <cmath>

class Spiral : public QWidget {
public:
    explicit Spiral(bool activate): on(activate) {
        setWindowFlags(Qt::FramelessWindowHint|Qt::Dialog);
        setAttribute(Qt::WA_TranslucentBackground);
        resize(520,450);
        // Resolve artwork from the installed module, never from an arbitrary
        // working directory alone. The canonical icon is sufficient for both
        // animation states; a grayscale counterpart is derived in memory.
        const QStringList candidates = {
            qEnvironmentVariable("NEEBLES_ASSET_DIR"),
            QDir(QCoreApplication::applicationDirPath()).filePath("../assets"),
            QDir::current().filePath("assets")
        };
        for (const QString &directory : candidates) {
            if (directory.isEmpty()) continue;
            const QDir assets(directory);
            QPixmap candidate(assets.filePath("neebles-client-icon.png"));
            if (candidate.isNull()) continue;
            color = candidate;
            QImage desaturated = candidate.toImage().convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < desaturated.height(); ++y) {
                auto *pixels = reinterpret_cast<QRgb *>(desaturated.scanLine(y));
                for (int x = 0; x < desaturated.width(); ++x) {
                    const QRgb original = pixels[x];
                    const int luminance = qGray(original);
                    pixels[x] = qRgba(luminance, luminance, luminance, qAlpha(original));
                }
            }
            gray = QPixmap::fromImage(desaturated);
            break;
        }
        clock.start();
        auto *timer=new QTimer(this);
        connect(timer,&QTimer::timeout,this,[this,timer] {
            progress=qMin(1.0,clock.elapsed()/3000.0);
            update();
            if (progress>=1.0) {timer->stop();close();}
        });
        timer->start(16);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRect target(40,15,width()-80,height()-90);
        const QPixmap &bottom=on?gray:color;
        const QPixmap &top=on?color:gray;
        if(!bottom.isNull()) p.drawPixmap(target,bottom);
        if(!top.isNull()) {
            QPainterPath mask;
            const QPointF center=target.center();
            const qreal maxR=std::hypot(width(),height());
            mask.moveTo(center);
            const int steps=600;
            for(int i=0;i<=steps;++i){
                const qreal t=(static_cast<qreal>(i)/steps)*progress;
                const qreal angle=-M_PI/2+6*M_PI*t;
                const qreal r=maxR*t;
                mask.lineTo(center+QPointF(std::cos(angle)*r,std::sin(angle)*r));
            }
            // Reveal a rotating sweep sector as a preview of the proposed spiral effect.
            QPainterPath wedge; wedge.moveTo(center);
            const qreal angle=-M_PI/2+2*M_PI*progress;
            wedge.arcTo(QRectF(center.x()-maxR,center.y()-maxR,2*maxR,2*maxR),90,-360*progress);
            wedge.closeSubpath();
            p.save();p.setClipPath(wedge);p.drawPixmap(target,top);p.restore();
        }
        p.setPen(Qt::white);
        p.drawText(QRect(0,height()-65,width(),40),Qt::AlignCenter,on?"PROXY ACTIVE":"PROXY INACTIVE");
    }
private:
    bool on; double progress=0; QElapsedTimer clock; QPixmap color,gray;
};

// Install-time vault creation is headless and uses the same canonical ELF
// that Secrets authenticates through SO_PEERCRED and /proc/<pid>/exe.
static int installVault(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("NEEBLES Proxy Client - module vault initialization");
    parser.addHelpOption();
    QCommandLineOption intent("intent", "Governed module intent", "name");
    parser.addOption(intent);
    parser.process(app);
    if (parser.value(intent) != QStringLiteral("install-vault")) return 2;
    QString error;
    if (!ProxySecrets::ensureVaultAndSchema(error)) {
        QTextStream(stderr) << "PROXY_CLIENT_VAULT_INSTALL_FAILED: " << error << '\n';
        return 3;
    }
    QTextStream(stdout) << "PROXY_CLIENT_VAULT_READY\n";
    return 0;
}

int main(int argc, char **argv) {
    // Parsing text-only initialization before QApplication avoids display
    // requirements during the Boss installation transaction.
    QStringList arguments;
    for (int i = 1; i < argc; ++i) arguments.append(QString::fromLocal8Bit(argv[i]));
    if (arguments.contains(QStringLiteral("--intent"))) return installVault(argc, argv);
    if (arguments.size() == 1 && arguments.first() == QStringLiteral("--serve-config")) {
        return ProxyConfigBridge::serve(argc, argv);
    }

    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("NEEBLES Proxy Client - configuration and status UI");
    parser.addHelpOption();
    const QCommandLineOption activate("show-active", "Show an already confirmed active state");
    const QCommandLineOption deactivate("show-inactive", "Show an already confirmed inactive state");
    const QCommandLineOption config("config", "Open Secrets-backed server configuration");
    parser.addOption(activate);
    parser.addOption(deactivate);
    parser.addOption(config);
    parser.process(app);

    if (parser.isSet(activate) && parser.isSet(deactivate)) {
        QTextStream(stderr) << "Conflicting status options\n";
        return 2;
    }
    if (parser.isSet(activate) || parser.isSet(deactivate)) {
        Spiral animation(parser.isSet(activate));
        animation.show();
        return app.exec();
    }
    if (!parser.isSet(config)) {
        QTextStream(stderr) << "No interactive action requested. Use --help.\n";
        return 2;
    }

    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("NEEBLES Proxy Client - Config"));
    dialog.setMinimumWidth(440);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();
    auto *ip = new QLineEdit(&dialog);
    auto *confirm = new QLineEdit(&dialog);
    ip->setPlaceholderText(QStringLiteral("100.x.x.x"));
    confirm->setPlaceholderText(QStringLiteral("Repeat the server Tailscale IPv4"));
    form->addRow(QStringLiteral("Server Tailscale IPv4:"), ip);
    form->addRow(QStringLiteral("Confirm IPv4:"), confirm);
    layout->addLayout(form);
    auto *message = new QLabel(&dialog);
    message->setWordWrap(true);
    layout->addWidget(message);
    auto *actions = new QHBoxLayout();
    auto *save = new QPushButton(QStringLiteral("Save in Secrets"), &dialog);
    auto *cancel = new QPushButton(QStringLiteral("Close"), &dialog);
    actions->addWidget(save);
    actions->addWidget(cancel);
    layout->addLayout(actions);

    // Only the installed canonical executable can query its module-owned vault.
    QString current;
    QString error;
    if (ProxySecrets::readServerIp(current, error)) {
        ip->setText(current);
        message->setText(QStringLiteral("Stored server loaded from NEEBLES Secrets. Confirm it before changing."));
    } else if (error == QStringLiteral("entity_not_found")) {
        message->setText(QStringLiteral("No server configured. Enter the address twice."));
    } else {
        message->setText(QStringLiteral("Secrets configuration unavailable: ") + error);
    }
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(save, &QPushButton::clicked, &dialog, [&] {
        const QString address = ip->text().trimmed();
        if (!ProxySecrets::validTailIp(address) || address != confirm->text().trimmed()) {
            message->setText(QStringLiteral("Invalid or non-matching Tailscale IPv4 (100.64.0.0/10)."));
            return;
        }
        save->setEnabled(false);
        QString failure;
        const bool stored = ProxySecrets::saveServerIp(address, failure);
        save->setEnabled(true);
        if (!stored) {
            message->setText(QStringLiteral("Secrets rejected the configuration: ") + failure);
            return;
        }
        confirm->clear();
        message->setText(QStringLiteral("Verified: server IPv4 saved to the module-owned Secrets vault."));
    });
    dialog.show();
    return app.exec();
}
