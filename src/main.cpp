// SPDX-License-Identifier: GPL-3.0-only
#include "core/runtime.h"
#include "ui/mainwindow.h"
#include "ui/scorecanvas.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QImage>
#include <QJsonDocument>
#include <QPainter>
#include <QTimer>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("JianpuLab");
    QCoreApplication::setOrganizationName("JianpuLab");
    QCoreApplication::setApplicationVersion("0.2.0");
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("C++ 多声部简谱编辑器"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", QStringLiteral("打开 MusicXML、MIDI 或 .jianpu 工程"));
    parser.addOption({"render", QStringLiteral("将完整简谱画布输出 PNG 并退出"), "file"});
    parser.addOption({"screenshot", QStringLiteral("将主窗口输出 PNG 并退出"), "file"});
    parser.addOption(
        {"export", QStringLiteral("按扩展名导出 MusicXML、MXL 或 MIDI 并退出"), "file"});
    parser.addOption({"save-project", QStringLiteral("保存 .jianpu 工程并退出"), "file"});
    parser.addOption({"inspect", QStringLiteral("输出曲谱统计 JSON 并退出")});
    parser.addOption({"smoke", QStringLiteral("启动冒烟验证后自动退出")});
    parser.process(app);
    jianpu::EngineRuntime runtime;
    jianpu::MainWindow window;
    if (!parser.positionalArguments().isEmpty() &&
        !window.openPath(parser.positionalArguments().first(), false))
    {
        qCritical().noquote() << window.document()->error();
        return 1;
    }
    if (parser.isSet("export"))
    {
        if (!window.document()->exportFile(parser.value("export")))
        {
            qCritical().noquote() << window.document()->error();
            return 2;
        }
        return 0;
    }
    if (parser.isSet("save-project"))
    {
        if (!window.document()->save(parser.value("save-project")))
        {
            qCritical().noquote() << window.document()->error();
            return 2;
        }
        return 0;
    }
    if (parser.isSet("inspect"))
    {
        const auto s = window.document()->snapshot();
        qInfo().noquote() << QJsonDocument(QJsonObject{{"title", s.title},
                                                       {"notes", s.noteCount()},
                                                       {"measures", s.measures.size()},
                                                       {"voices", s.voices.size()},
                                                       {"marks", s.marks.size()}})
                                 .toJson(QJsonDocument::Compact);
        return 0;
    }
    window.show();
    app.processEvents();
    if (parser.isSet("render"))
    {
        const auto &layout = window.canvas()->layoutResult();
        QImage image(layout.size.toSize(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        window.canvas()->paintScore(painter, QRectF(QPointF(), layout.size));
        painter.end();
        if (!image.save(parser.value("render"), "PNG"))
        {
            qCritical() << "Cannot save rendered score";
            return 3;
        }
        return 0;
    }
    if (parser.isSet("screenshot"))
        return window.grab().save(parser.value("screenshot"), "PNG") ? 0 : 3;
    if (parser.isSet("smoke"))
    {
        window.setConfirmClose(false);
        QTimer::singleShot(200, &app, &QCoreApplication::quit);
    }
    window.canvas()->setFocus();
    return app.exec();
}
