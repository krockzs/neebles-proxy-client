#include "config_secrets.h"
#include "config_bridge.h"
#include <QApplication>
#include <QStyleFactory>
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

// NEEBLES visual identity, shared by Config and the transition splash.
static QString neeblesTheme() {
    return QStringLiteral(R"QSS(
        QWidget {
            background-color: #10131d;
            color: #e7edf8;
            font-family: "Noto Sans", sans-serif;
            font-size: 12px;
        }
        QDialog {
            background-color: #10131d;
            border: 1px solid #313a55;
        }
        QLabel {
            background: transparent;
            color: #e7edf8;
        }
        QLineEdit {
            background-color: #131827;
            color: #edf5ff;
            selection-background-color: #7355c6;
            border: 1px solid #45516c;
            border-radius: 7px;
            padding: 8px 10px;
            min-height: 24px;
        }
        QLineEdit:focus {
            border: 1px solid #4fc9de;
        }
        QPushButton {
            background-color: #252c42;
            color: #eaf4ff;
            border: 1px solid #465377;
            border-radius: 7px;
            padding: 9px 16px;
            min-height: 22px;
        }
        QPushButton:hover {
            background-color: #30415c;
            border-color: #50d4e8;
        }
        QPushButton:pressed {
            background-color: #453a76;
        }
        QPushButton:disabled {
            background-color: #202630;
            color: #777f92;
            border-color: #3f4551;
        }
        QPushButton#saveButton {
            background-color: #102b3b;
            border: 1px solid #22d3ee;
            color: #67e8f9;
            font-weight: 600;
        }
        QPushButton#saveButton:hover {
            background-color: #16445b;
        }
        QLabel#title {
            color: #67e8f9;
            font-size: 18px;
            font-weight: 700;
        }
        QLabel#subtitle {
            color: #a78bfa;
            font-size: 12px;
        }
        QLabel#feedback {
            background-color: #191d2c;
            color: #cbd5e1;
            border: 1px solid #313a55;
            border-radius: 7px;
            padding: 10px;
        }
        QToolTip {
            background-color: #191d2c;
            color: #e7edf8;
            border: 1px solid #4fc9de;
        }
    )QSS");
}

// The splash is presentation only. It is launched by the authenticated user
// bridge after it observes a confirmed change in the selected Exit Node.
class Spiral final : public QWidget {
public:
    explicit Spiral(bool activate) : on(activate) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(650, 475);

        const QStringList candidates = {
            QDir(QCoreApplication::applicationDirPath()).filePath("../assets"),
            qEnvironmentVariable("NEEBLES_ASSET_DIR")
        };
        for (const QString &directory : candidates) {
            if (directory.isEmpty()) continue;
            const QDir assets(directory);
            const QPixmap originalOff(assets.filePath("neebles-client-logo-off.png"));
            const QPixmap originalOn(assets.filePath("neebles-client-logo-on.png"));
            if (originalOff.isNull() || originalOn.isNull()) continue;
            off = originalOff;
            active = originalOn;
            break;
        }

        if (off.isNull() || active.isNull()) {
            QTextStream(stderr) << "PROXY_CLIENT_SPLASH_ASSETS_UNAVAILABLE\n";
        }

        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this, timer] {
            progress = qMin(1.0, clock.elapsed() / 3000.0);
            update();
            if (progress >= 1.0) {
                timer->stop();
                close();
            }
        });
        clock.start();
        timer->start(16);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const QRect panel = rect().adjusted(8, 8, -8, -8);
        painter.setBrush(QColor("#10131d"));
        painter.setPen(QPen(QColor(on ? "#22d3ee" : "#a78bfa"), 2));
        painter.drawRoundedRect(panel, 18, 18);

        const QRect artwork = panel.adjusted(34, 24, -34, -79);
        const QPixmap &initial = on ? off : active;
        const QPixmap &final = on ? active : off;

        if (!initial.isNull()) painter.drawPixmap(artwork, initial);
        if (!final.isNull() && progress > 0.0) {
            const QPointF center = artwork.center();
            const qreal radius = std::hypot(qreal(artwork.width()), qreal(artwork.height()));
            QPainterPath reveal;
            reveal.moveTo(center);
            reveal.arcTo(QRectF(center.x() - radius, center.y() - radius,
                                2 * radius, 2 * radius),
                         90, -360 * progress);
            reveal.closeSubpath();
            painter.save();
            painter.setClipPath(reveal);
            painter.drawPixmap(artwork, final);
            painter.restore();
        }

        painter.setPen(QColor("#67e8f9"));
        const QString status = on ? QStringLiteral("PROXY ACTIVE")
                                  : QStringLiteral("PROXY INACTIVE");
        painter.drawText(panel.adjusted(22, panel.height() - 63, -22, -24),
                         Qt::AlignCenter, status);
    }

private:
    bool on;
    double progress = 0.0;
    QElapsedTimer clock;
    QPixmap off;
    QPixmap active;
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
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setStyleSheet(neeblesTheme());
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
    dialog.setMinimumWidth(540);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(14);
    auto *title = new QLabel(QStringLiteral("NEEBLES PROXY CLIENT"), &dialog);
    title->setObjectName(QStringLiteral("title"));
    layout->addWidget(title);
    auto *subtitle = new QLabel(QStringLiteral("Tailscale · Configuración del servidor"), &dialog);
    subtitle->setObjectName(QStringLiteral("subtitle"));
    layout->addWidget(subtitle);
    auto *form = new QFormLayout();
    auto *ip = new QLineEdit(&dialog);
    auto *confirm = new QLineEdit(&dialog);
    ip->setPlaceholderText(QStringLiteral("100.x.x.x"));
    confirm->setPlaceholderText(QStringLiteral("Repeat the server Tailscale IPv4"));
    form->addRow(QStringLiteral("Server Tailscale IPv4:"), ip);
    form->addRow(QStringLiteral("Confirm IPv4:"), confirm);
    layout->addLayout(form);
    auto *message = new QLabel(&dialog);
    message->setObjectName(QStringLiteral("feedback"));
    message->setWordWrap(true);
    layout->addWidget(message);
    auto *actions = new QHBoxLayout();
    auto *save = new QPushButton(QStringLiteral("Save in Secrets"), &dialog);
    save->setObjectName(QStringLiteral("saveButton"));
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
