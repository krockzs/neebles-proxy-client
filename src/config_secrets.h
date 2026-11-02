#pragma once

#include <QString>

namespace ProxySecrets {

bool validTailIp(const QString &value);
bool ensureVaultAndSchema(QString &error);
bool readServerIp(QString &ip, QString &error);
bool saveServerIp(const QString &ip, QString &error);

}  // namespace ProxySecrets
