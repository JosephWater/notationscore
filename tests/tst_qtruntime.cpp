#include <QApplication>
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>
#include <memory>

class QtRuntimeTest : public QObject
{
    Q_OBJECT
private slots:
    void exactSdkVersion() { QCOMPARE(QString::fromLatin1(qVersion()), QStringLiteral("6.10.2")); }

    void qmlQuickControlsCanLoad()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            Rectangle {
                width: 240; height: 80
                Button { objectName: "probeButton"; text: "Qt Quick 可用" }
            }
        )", QUrl());
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 10000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object != nullptr, qPrintable(component.errorString()));
        QVERIFY(object->findChild<QObject *>(QStringLiteral("probeButton")) != nullptr);
    }

    void xmlSvgAndPdfRuntime()
    {
        QXmlStreamReader xml(QByteArrayLiteral("<probe><name>简谱</name></probe>"));
        QString name;
        while (!xml.atEnd())
        {
            if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == u"name")
                name = xml.readElementText();
        }
        QVERIFY(!xml.hasError());
        QCOMPARE(name, QStringLiteral("简谱"));

        QSvgRenderer svg(QByteArrayLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' width='32' height='32'>"
            "<rect width='32' height='32' fill='#2878c8'/></svg>"));
        QVERIFY(svg.isValid());
        QImage image(32, 32, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        svg.render(&painter);
        painter.end();
        QCOMPARE(image.pixelColor(16, 16), QColor("#2878c8"));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto filename = directory.filePath(QStringLiteral("probe.pdf"));
        {
            QPdfWriter pdf(filename);
            QPainter pdfPainter(&pdf);
            QVERIFY(pdfPainter.isActive());
            pdfPainter.drawText(100, 100, QStringLiteral("简谱工具链验证"));
        }
        QFile file(filename);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.read(5) == QByteArrayLiteral("%PDF-"));
    }
};

QTEST_MAIN(QtRuntimeTest)
#include "tst_qtruntime.moc"
