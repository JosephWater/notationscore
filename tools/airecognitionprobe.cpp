// SPDX-License-Identifier: GPL-3.0-only
// Explicit opt-in live probe; normal CTest never calls an external AI service.
#include "ai/recognitiontask.h"
#include "core/runtime.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("JianpuLab");
    QCoreApplication::setOrganizationName("JianpuLab");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument("images", "Ordered score images", "images...");
    parser.addOption({"output", "Write the complete transcript to this file", "path"});
    parser.addOption({"checks", "Number of checking rounds (0–2)", "rounds", "2"});
    parser.process(app);
    if (parser.positionalArguments().isEmpty())
        parser.showHelp(1);
    jianpu::EngineRuntime runtime;
    jianpu::RecognitionTask task;
    auto config = jianpu::AiConfig::load();
    config.checks = parser.value("checks").toInt();
    QObject::connect(&task, &jianpu::RecognitionTask::stageChanged,
                     [](int stage, const QString &label)
                     { qInfo().noquote() << QString("stage %1: %2").arg(stage + 1).arg(label); });
    QObject::connect(
        &task, &jianpu::RecognitionTask::finished, &app,
        [&](const QString &source, bool success, const QString &message)
        {
            if (parser.isSet("output"))
            {
                QSaveFile out(parser.value("output"));
                if (!out.open(QIODevice::WriteOnly) ||
                    out.write(source.toUtf8()) != source.toUtf8().size() || !out.commit())
                {
                    qCritical() << "Cannot write output";
                    app.exit(3);
                    return;
                }
            }
            qInfo().noquote() << QJsonDocument(QJsonObject{{"success", success},
                                                           {"characters", source.size()},
                                                           {"message", message}})
                                     .toJson(QJsonDocument::Compact);
            app.exit(success ? 0 : 2);
        });
    QTimer::singleShot(0, &task, [&] { task.start(config, parser.positionalArguments()); });
    return app.exec();
}
