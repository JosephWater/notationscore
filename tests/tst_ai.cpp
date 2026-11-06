// SPDX-License-Identifier: GPL-3.0-only
#include "ai/recognitiontask.h"
#include "core/runtime.h"
#include "input/inputcontroller.h"
#include "ui/airecognition.h"
#include "ui/mainwindow.h"
#include "ui/sourceeditor.h"
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
using namespace jianpu;
namespace
{
QByteArray sseEvent(const QString &text, const QString &finish = {})
{
    QJsonObject choice{
        {"index", 0},
        {"delta", QJsonObject{{"content", text}, {"reasoning_content", "PRIVATE_REASONING"}}},
        {"finish_reason", finish.isEmpty() ? QJsonValue() : QJsonValue(finish)}};
    return "data: " +
           QJsonDocument(QJsonObject{{"choices", QJsonArray{choice}}})
               .toJson(QJsonDocument::Compact) +
           "\r\n\r\n";
}
class Server : public QObject
{
  public:
    QTcpServer server;
    QVector<QJsonObject> requests;
    QStringList outputs;
    int status = 200;
    bool hold = false;
    Server()
    {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this,
                [this]
                {
                    while (server.hasPendingConnections())
                    {
                        auto *socket = server.nextPendingConnection();
                        socket->setParent(this);
                        connect(
                            socket, &QTcpSocket::readyRead, this,
                            [this, socket]
                            {
                                auto bytes =
                                    socket->property("request").toByteArray() + socket->readAll();
                                socket->setProperty("request", bytes);
                                if (socket->property("handled").toBool())
                                    return;
                                int split = bytes.indexOf("\r\n\r\n");
                                if (split < 0)
                                    return;
                                auto header = bytes.left(split);
                                int size = 0;
                                for (auto line : header.split('\n'))
                                    if (line.toLower().startsWith("content-length:"))
                                        size = line.mid(15).trimmed().toInt();
                                if (bytes.size() < split + 4 + size)
                                    return;
                                socket->setProperty("handled", true);
                                requests
                                    << QJsonDocument::fromJson(bytes.mid(split + 4, size)).object();
                                auto content =
                                    outputs.value(requests.size() - 1, "V: 1.1\nQ: 1 2 3 4 |\n");
                                auto response = QByteArray("HTTP/1.1 ") +
                                                QByteArray::number(status) +
                                                " Test\r\nContent-Type: "
                                                "text/event-stream\r\nConnection: close\r\n\r\n";
                                if (status == 200)
                                {
                                    response += sseEvent(content);
                                    if (!hold)
                                        response += sseEvent({}, "stop") + "data: [DONE]\n\n";
                                }
                                else
                                    response += "{}";
                                socket->setProperty("out", response);
                                auto *timer = new QTimer(socket);
                                timer->setInterval(1);
                                connect(timer, &QTimer::timeout, socket,
                                        [socket, timer, this]
                                        {
                                            auto data = socket->property("out").toByteArray();
                                            socket->write(data.left(13));
                                            data.remove(0, 13);
                                            socket->setProperty("out", data);
                                            if (data.isEmpty())
                                            {
                                                timer->stop();
                                                if (!hold)
                                                    socket->disconnectFromHost();
                                            }
                                        });
                                timer->start();
                            });
                    }
                });
    }
    AiConfig config(int checks = 2)
    {
        AiConfig c;
        c.endpoint = QString("http://127.0.0.1:%1/chat/completions").arg(server.serverPort());
        c.key = "unit-test-key";
        c.checks = checks;
        return c;
    }
};
QString image(const QString &path, Qt::GlobalColor color = Qt::white)
{
    QImage img(64, 48, QImage::Format_RGB32);
    img.fill(color);
    img.save(path);
    return path;
}
} // namespace
class AiTest : public QObject
{
    Q_OBJECT
  private slots:
    void splitUtf8AndReasoning()
    {
        CompletionStream stream;
        auto bytes =
            sseEvent(QStringLiteral("Q: 春夏秋冬\n")) + sseEvent({}, "stop") + "data: [DONE]\n\n";
        for (char ch : bytes)
            stream.feed(QByteArray(1, ch));
        stream.finish();
        QVERIFY2(stream.error.isEmpty(), qPrintable(stream.error));
        QCOMPARE(stream.content, QStringLiteral("Q: 春夏秋冬\n"));
        QVERIFY(stream.done);
    }
    void rejectsTruncationAndBadJson()
    {
        CompletionStream stream;
        stream.feed(sseEvent("Q: 1 2", "length"));
        stream.finish();
        QVERIFY(!stream.error.isEmpty());
        CompletionStream broken;
        broken.feed("data: {bad}\n\n");
        broken.finish();
        QVERIFY(!broken.error.isEmpty());
        CompletionStream shortStream;
        shortStream.feed(sseEvent("Q: 1"));
        shortStream.finish();
        QVERIFY(!shortStream.error.isEmpty());
    }
    void clientStatus_data()
    {
        QTest::addColumn<int>("status");
        for (int code : {401, 403, 429, 500})
            QTest::newRow(qPrintable(QString::number(code))) << code;
    }
    void clientStatus()
    {
        QFETCH(int, status);
        Server s;
        s.status = status;
        AiClient client;
        QSignalSpy fail(&client, &AiClient::failed);
        client.start(s.config(), {{"stream", true}});
        QTRY_COMPARE(fail.size(), 1);
        QVERIFY(!fail[0][0].toString().contains("unit-test-key"));
    }
    void boundedReviewAndRepair()
    {
        Server s;
        s.outputs = {"V: 1.1\nQ: 1x\n", "V: 1.1\nQ: 1x\n", "V: 1.1\nQ: 1 2 3 4 |\n"};
        QTemporaryDir dir;
        RecognitionTask task;
        QSignalSpy done(&task, &RecognitionTask::finished),
            stages(&task, &RecognitionTask::stageChanged);
        task.start(s.config(),
                   {image(dir.filePath("a.png")), image(dir.filePath("b.png"), Qt::red)});
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY2(done[0][1].toBool(), qPrintable(done[0][2].toString()));
        QCOMPARE(stages.size(), 3);
        QCOMPARE(s.requests.size(), 3);
        auto content = s.requests[0]["messages"].toArray().last().toObject()["content"].toArray();
        QCOMPARE(content.size(), 3);
        QVERIFY(content[1].toObject()["image_url"].toObject()["url"] !=
                content[2].toObject()["image_url"].toObject()["url"]);
        QVERIFY(!done[0][0].toString().contains("PRIVATE_REASONING"));
    }
    void validResultAlwaysReviewedAndDoesNotLoop()
    {
        Server s;
        QTemporaryDir dir;
        RecognitionTask task;
        QSignalSpy done(&task, &RecognitionTask::finished);
        task.start(s.config(), {image(dir.filePath("a.png"))});
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
        QVERIFY(done[0][1].toBool());
        QCOMPARE(s.requests.size(), 2);
    }
    void failedChecksStopAfterThreeCalls()
    {
        Server s;
        s.outputs = {"Q: invalid\n", "Q: invalid\n", "Q: invalid\n"};
        QTemporaryDir dir;
        RecognitionTask task;
        QSignalSpy done(&task, &RecognitionTask::finished);
        task.start(s.config(), {image(dir.filePath("a.png"))});
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
        QVERIFY(!done[0][1].toBool());
        QCOMPARE(s.requests.size(), 3);
        QVERIFY(!done[0][0].toString().isEmpty());
    }
    void cancelAndTimeout()
    {
        Server s;
        s.hold = true;
        AiClient client;
        QSignalSpy changed(&client, &AiClient::contentChanged), fail(&client, &AiClient::failed);
        client.start(s.config(), {{"stream", true}});
        QTRY_VERIFY(!changed.isEmpty());
        client.cancel();
        QTRY_COMPARE(fail.size(), 1);
        auto c = s.config();
        c.idleTimeoutMs = 80;
        client.start(c, {{"stream", true}});
        QTRY_COMPARE_WITH_TIMEOUT(fail.size(), 2, 3000);
    }
    void independentWindowAndAtomicUndo()
    {
        Server s;
        QTemporaryDir dir;
        MainWindow original;
        original.setConfirmClose(false);
        QVERIFY(original.document()->applyText("Q: 5 6 7 |\n"));
        auto before = original.document()->sourceText();
        QPointer<MainWindow> created =
            AiRecognitionUi::recognize(s.config(0), {image(dir.filePath("a.png"))});
        created->setConfirmClose(false);
        auto *editor = created->findChild<SourceEditor *>();
        QVERIFY(editor->isReadOnly());
        QTRY_VERIFY_WITH_TIMEOUT(!editor->isReadOnly(), 10000);
        QCOMPARE(original.document()->sourceText(), before);
        QCOMPARE(created->document()->snapshot().noteCount(), 4);
        QCOMPARE(editor->toPlainText(), created->document()->sourceText());
        created->document()->undo();
        QCOMPARE(created->document()->snapshot().noteCount(), 0);
        created->document()->redo();
        QCOMPARE(created->document()->snapshot().noteCount(), 4);
        created->close();
        QTRY_VERIFY(created.isNull());
    }
    void failedWindowPreservesDraftAndAllowsManualCorrection()
    {
        Server s;
        s.outputs = {"V: 1.1\nQ: 1x\n"};
        QTemporaryDir dir;
        QPointer<MainWindow> w =
            AiRecognitionUi::recognize(s.config(0), {image(dir.filePath("a.png"))});
        w->setConfirmClose(false);
        auto *editor = w->findChild<SourceEditor *>();
        QTRY_VERIFY_WITH_TIMEOUT(!editor->isReadOnly(), 10000);
        QVERIFY(w->document()->inputState().contains("draftSource"));
        QCOMPARE(w->document()->snapshot().noteCount(), 0);
        editor->setPlainText("Q: 1 2 3 4 |\n");
        QTRY_COMPARE(w->document()->snapshot().noteCount(), 4);
        w->close();
        QTRY_VERIFY(w.isNull());
    }
    void closeRunningRecognitionWindow()
    {
        Server server;
        server.hold = true;
        QTemporaryDir dir;
        QPointer<MainWindow> w =
            AiRecognitionUi::recognize(server.config(0), {image(dir.filePath("a.png"))});
        w->setConfirmClose(false);
        QTRY_VERIFY(!server.requests.isEmpty());
        w->close();
        QTRY_VERIFY(w.isNull());
    }
    void localValidation()
    {
        QVERIFY(RecognitionTask::validateSource("Q: 1 2 3 4 |\n").isEmpty());
        QVERIFY(RecognitionTask::validateSource("V: 1.1\nQ: 1 |\n# 疑点：看不清八度\n").size() > 0);
        QVERIFY(RecognitionTask::validateSource("V: 1.1\nQ: 1 2 3 4 5 |\n").size() > 0);
        QVERIFY(RecognitionTask::validateSource("V: 1.1\nQ: 1 2 3 4 |\nM: "
                                                "{\"kind\":\"time\",\"voice\":0,\"start\":\"0/"
                                                "1\",\"numerator\":0,\"denominator\":0}\n")
                    .size() > 0);
        QVERIFY(!RecognitionTask::systemPrompt().isEmpty());
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("JianpuLab");
    QCoreApplication::setApplicationName("JianpuLabAiTests");
    QStandardPaths::setTestModeEnabled(true);
    EngineRuntime runtime;
    AiTest t;
    return QTest::qExec(&t, argc, argv);
}
#include "tst_ai.moc"
