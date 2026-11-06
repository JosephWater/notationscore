// SPDX-License-Identifier: GPL-3.0-only
#include "aiclient.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
namespace jianpu
{
void CompletionStream::dispatch()
{
    if (event.isEmpty())
        return;
    auto data = event.trimmed();
    event.clear();
    if (data == "[DONE]")
    {
        done = true;
        return;
    }
    if (done)
    {
        error = QStringLiteral("流结束后收到额外数据");
        return;
    }
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(data, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
    {
        error = QStringLiteral("接口返回了损坏的流事件");
        return;
    }
    auto o = doc.object();
    if (o.contains("error"))
    {
        error = QStringLiteral("AI 接口错误：%1")
                    .arg(o["error"].toObject()["message"].toString("服务未返回错误详情"));
        return;
    }
    if (o["usage"].isObject())
        usage = o["usage"].toObject();
    for (auto v : o["choices"].toArray())
    {
        auto c = v.toObject();
        if (c["index"].toInt() != 0)
            continue;
        auto delta = c["delta"].toObject();
        content += delta["content"].toString();
        if (c["finish_reason"].isString())
            finishReason = c["finish_reason"].toString();
    }
}
void CompletionStream::feed(const QByteArray &bytes)
{
    pending += bytes;
    if (pending.size() + event.size() > 16 * 1024 * 1024 || content.size() > 8 * 1024 * 1024)
    {
        error = QStringLiteral("AI 响应超过大小限制");
        return;
    }
    qsizetype pos;
    while ((pos = pending.indexOf('\n')) >= 0 && error.isEmpty())
    {
        auto line = pending.left(pos);
        pending.remove(0, pos + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty())
            dispatch();
        else if (line.startsWith("data:"))
        {
            if (!event.isEmpty())
                event += '\n';
            auto part = line.mid(5);
            if (part.startsWith(' '))
                part.remove(0, 1);
            event += part;
        }
    }
}
void CompletionStream::finish()
{
    if (!pending.isEmpty())
        feed("\n");
    dispatch();
    if (!error.isEmpty())
        return;
    if (finishReason != "stop")
        error = finishReason == "length"
                    ? QStringLiteral("模型输出被长度限制截断，请减少图片或调整模型")
                    : QStringLiteral("AI 输出未正常完成，已保留草稿");
    if (content.trimmed().isEmpty() && error.isEmpty())
        error = QStringLiteral("AI 未返回文字稿");
}
AiClient::AiClient(QObject *p) : QObject(p)
{
    m_idle.setSingleShot(true);
    m_total.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, this, [this] { abort(tr("AI 接口长时间未返回数据")); });
    connect(&m_total, &QTimer::timeout, this, [this] { abort(tr("AI 请求超时")); });
}
AiClient::~AiClient()
{
    if (m_reply)
    {
        m_reply->disconnect(this);
        m_reply->abort();
    }
}
void AiClient::start(const AiConfig &c, QJsonObject payload)
{
    if (running())
        return;
    auto error = c.validate();
    if (!error.isEmpty())
    {
        emit failed(error);
        return;
    }
    m_config = c;
    m_stream = {};
    m_body.clear();
    m_abortReason.clear();
    payload["model"] = c.model;
    m_streaming = payload["stream"].toBool(true);
    QNetworkRequest request{QUrl(c.endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + c.key).toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    m_reply = m_network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &AiClient::read);
    connect(
        m_reply, &QNetworkReply::finished, this,
        [this]
        {
            read();
            auto *reply = m_reply.data();
            if (!reply)
                return;
            m_idle.stop();
            m_total.stop();
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString error = m_abortReason;
            if (error.isEmpty() && status != 200)
            {
                if (status == 401 || status == 403)
                    error = tr("API key 无效或没有模型访问权限（HTTP %1）").arg(status);
                else if (status == 429)
                    error = tr("接口限流或额度不足（HTTP 429）");
                else
                    error =
                        tr("AI 接口请求失败（HTTP %1）：%2").arg(status).arg(reply->errorString());
            }
            if (error.isEmpty() && reply->error() != QNetworkReply::NoError)
                error = reply->errorString();
            if (error.isEmpty())
            {
                if (m_streaming)
                {
                    m_stream.finish();
                    error = m_stream.error;
                }
                else
                {
                    auto o = QJsonDocument::fromJson(m_body).object();
                    auto choices = o["choices"].toArray();
                    auto choice = choices.isEmpty() ? QJsonObject() : choices.at(0).toObject();
                    m_stream.content = choice["message"].toObject()["content"].toString();
                    m_stream.usage = o["usage"].toObject();
                    if (choice["finish_reason"] != "stop" || m_stream.content.isEmpty())
                        error = tr("接口未返回完整结果");
                }
            }
            m_reply.clear();
            reply->deleteLater();
            if (!error.isEmpty())
                emit failed(m_config.redact(error));
            else
                emit completed(m_stream.content, m_stream.usage);
        });
    m_idle.start(c.idleTimeoutMs);
    m_total.start(c.totalTimeoutMs);
}
void AiClient::read()
{
    if (!m_reply)
        return;
    auto bytes = m_reply->readAll();
    if (bytes.isEmpty())
        return;
    m_idle.start(m_config.idleTimeoutMs);
    if (m_streaming)
    {
        m_stream.feed(bytes);
        emit contentChanged(m_stream.content);
        if (!m_stream.error.isEmpty())
            abort(m_stream.error);
    }
    else
    {
        m_body += bytes;
        if (m_body.size() > 16 * 1024 * 1024)
            abort(tr("接口响应过大"));
    }
}
void AiClient::abort(const QString &why)
{
    m_abortReason = why;
    if (m_reply)
        m_reply->abort();
}
void AiClient::cancel() { abort(tr("已取消识别，已接收的文字保留为草稿")); }
} // namespace jianpu
