// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "aiconfig.h"
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
namespace jianpu
{
// Byte-oriented SSE framing: UTF-8 is decoded only after the complete JSON event.
class CompletionStream
{
  public:
    void feed(const QByteArray &);
    void finish();
    QString content, error;
    QString finishReason;
    QJsonObject usage;
    bool done = false;

  private:
    QByteArray pending, event;
    void dispatch();
};
class AiClient final : public QObject
{
    Q_OBJECT
  public:
    explicit AiClient(QObject *parent = nullptr);
    ~AiClient() override;
    void start(const AiConfig &, QJsonObject payload);
    void cancel();
    bool running() const { return !m_reply.isNull(); }
  signals:
    void contentChanged(const QString &);
    void completed(const QString &, const QJsonObject &usage);
    void failed(const QString &);

  private:
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_idle, m_total;
    AiConfig m_config;
    CompletionStream m_stream;
    QByteArray m_body;
    QString m_abortReason;
    bool m_streaming = true;
    void read();
    void abort(const QString &);
};
} // namespace jianpu
