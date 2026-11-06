// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "aiclient.h"
#include <QJsonArray>
#include <QStringList>
namespace jianpu
{
class RecognitionTask final : public QObject
{
    Q_OBJECT
  public:
    explicit RecognitionTask(QObject *parent = nullptr);
    void start(const AiConfig &, const QStringList &paths);
    void cancel();
    bool running() const { return m_running; }
    static QStringList validateSource(const QString &);
    static QString systemPrompt();
  signals:
    void stageChanged(int stage, const QString &label);
    void sourceChanged(const QString &);
    void finished(const QString &source, bool success, const QString &message);

  private:
    AiClient m_client;
    AiConfig m_config;
    QJsonArray m_images;
    QString m_source, m_previous;
    int m_stage = 0;
    bool m_running = false;
    void request();
    void finish(bool, const QString &);
};
} // namespace jianpu
