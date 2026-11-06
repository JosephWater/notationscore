// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QString>
namespace jianpu
{
struct AiConfig
{
    QString endpoint = "https://www.sophnet.com/api/open-apis/v1/chat/completions";
    QString model = "DeepSeek-Flash";
    QString key;
    int checks = 2; // review, then one conditional repair
    int idleTimeoutMs = 60000, totalTimeoutMs = 300000;
    static QString path();
    static AiConfig load();
    bool save(QString *error = nullptr) const;
    QString validate() const;
    QString redact(QString text) const;
};
} // namespace jianpu
