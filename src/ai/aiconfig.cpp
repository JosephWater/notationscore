// SPDX-License-Identifier: GPL-3.0-only
#include "aiconfig.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
namespace jianpu
{
QString AiConfig::path()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/ai.json";
}
AiConfig AiConfig::load()
{
    AiConfig c;
    QFile f(path());
    if (f.open(QIODevice::ReadOnly))
    {
        auto o = QJsonDocument::fromJson(f.readAll()).object();
        c.endpoint = o["endpoint"].toString(c.endpoint);
        c.model = o["model"].toString(c.model);
        c.key = o["key"].toString();
        c.checks = std::clamp(o["checks"].toInt(2), 0, 2);
    }
    if (qEnvironmentVariableIsSet("JIANPU_AI_API_KEY"))
        c.key = qEnvironmentVariable("JIANPU_AI_API_KEY");
    return c;
}
QString AiConfig::validate() const
{
    QUrl url(endpoint);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() ||
        !url.query().isEmpty() || !url.fragment().isEmpty())
        return QStringLiteral("请输入完整、不含凭据或查询参数的请求地址");
    if (url.scheme() != "https" &&
        !(url.scheme() == "http" &&
          (url.host() == "127.0.0.1" || url.host() == "localhost" || url.host() == "::1")))
        return QStringLiteral("远程接口需要 HTTPS");
    if (model.trimmed().isEmpty())
        return QStringLiteral("请填写模型名称");
    if (key.trimmed().isEmpty())
        return QStringLiteral("请配置 API key");
    return {};
}
QString AiConfig::redact(QString s) const
{
    if (!key.isEmpty())
        s.replace(key, "[已隐藏]");
    return s;
}
bool AiConfig::save(QString *error) const
{
    auto message = validate();
    if (!message.isEmpty())
    {
        if (error)
            *error = message;
        return false;
    }
    QDir().mkpath(QFileInfo(path()).absolutePath());
    QSaveFile f(path());
    if (!f.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    auto bytes =
        QJsonDocument(
            QJsonObject{{"endpoint", endpoint}, {"model", model}, {"key", key}, {"checks", checks}})
            .toJson();
    if (f.write(bytes) != bytes.size() || !f.commit())
    {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}
} // namespace jianpu
