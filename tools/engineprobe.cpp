// SPDX-License-Identifier: GPL-3.0-only
#include "core/runtime.h"
#include "core/scoredocument.h"
#include <QApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QTemporaryDir>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("JianpuLabProbe");
    QCoreApplication::setOrganizationName("JianpuLab");
    jianpu::EngineRuntime runtime;
    jianpu::ScoreDocument document;
    if (argc < 2 || !document.open(QString::fromLocal8Bit(argv[1])))
    {
        qCritical().noquote() << document.error();
        return 1;
    }
    const auto before = document.snapshot();
    QTemporaryDir dir;
    if (!document.save(dir.filePath(QStringLiteral("验证.jianpu"))) ||
        !document.exportFile(dir.filePath("roundtrip.musicxml")) ||
        !document.exportFile(dir.filePath("roundtrip.mid")))
    {
        qCritical().noquote() << document.error();
        return 2;
    }
    jianpu::ScoreDocument reopened;
    if (!reopened.open(document.filePath()))
    {
        qCritical().noquote() << reopened.error();
        return 3;
    }
    const auto after = reopened.snapshot();
    if (before.noteCount() != after.noteCount() || before.measures.size() != after.measures.size())
        return 4;
    qInfo().noquote() << QJsonDocument(QJsonObject{{"notes", after.noteCount()},
                                                   {"measures", after.measures.size()},
                                                   {"voices", after.voices.size()},
                                                   {"nativeRoundTrip", true}})
                             .toJson(QJsonDocument::Compact);
    return 0;
}
