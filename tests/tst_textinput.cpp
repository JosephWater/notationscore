// SPDX-License-Identifier: GPL-3.0-only
#include "core/runtime.h"
#include "core/scoredocument.h"
#include "input/inputcontroller.h"
#include "input/textformat.h"
#include "layout/jianpulayout.h"
#include "layout/layoutsettings.h"
#include "ui/mainwindow.h"
#include "ui/scorecanvas.h"
#include "ui/sourceeditor.h"
#include <QComboBox>
#include <QFile>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
using namespace jianpu;
class TextInputTest : public QObject
{
    Q_OBJECT
  private slots:
    void durationsAndSpelling()
    {
        auto p = FanqieFormat().parse("V: 1.0\nD: D\nP: 4/4\nQ: 1/ 2// 3- 4/- 5-. 6/-. 1''# |\n");
        QVERIFY2(!p.hasErrors(), qPrintable(p.messages().join('\n')));
        QCOMPARE(p.notes[0].duration, MusicalTime(1, 8));
        QCOMPARE(p.notes[1].duration, MusicalTime(1, 16));
        QCOMPARE(p.notes[2].duration, MusicalTime(1, 2));
        QCOMPARE(p.notes[3].duration, MusicalTime(3, 8));
        QCOMPARE(p.notes[4].duration, MusicalTime(3, 4));
        QCOMPARE(p.notes[5].duration, MusicalTime(9, 16));
        QCOMPARE(p.notes[0].midi, 62);
        QCOMPARE(p.notes[6].midi, 87);
        auto measure = FanqieFormat().parse("Q: 1# 1 1= 1 | 1");
        QCOMPARE(measure.notes[0].midi, 61);
        QCOMPARE(measure.notes[1].midi, 61);
        QCOMPARE(measure.notes[2].midi, 60);
        QCOMPARE(measure.notes[4].midi, 60);
        auto note = FanqieFormat().parse("Q: 1# 1", {{"accidentalScope", "note"}});
        QCOMPARE(note.notes[1].midi, 60);
    }
    void tupletsAndPending()
    {
        auto p = FanqieFormat().parse("Q: (y111) (y1/1/1/) (y5:4 1/2/3/4/5/)");
        QVERIFY2(!p.hasErrors(), qPrintable(p.messages().join('\n')));
        QCOMPARE(p.notes[3].position, MusicalTime(1, 2));
        QCOMPARE(p.notes[6].position, MusicalTime(3, 4));
        QCOMPARE(p.notes.last().position + p.notes.last().duration, MusicalTime(5, 4));
        auto seven = FanqieFormat().parse("Q: (y1/2/3/4/5/6/7/)");
        QCOMPARE(seven.notes.last().position + seven.notes.last().duration, MusicalTime(1, 2));
        auto pending = FanqieFormat().parse("Q: (1/2/ <3\n");
        QVERIFY(pending.hasErrors());
        QCOMPARE(pending.spans.size(), 2);
        QVERIFY(!pending.spans[0].closed);
        auto voices = FanqieFormat().parse("Q1: (1 2 |\nQ2: (5 6 |\nQ1: 3 4) |\nQ2: 7 1) |\n");
        QVERIFY2(!voices.hasErrors(), qPrintable(voices.messages().join('\n')));
        QVERIFY(voices.spans[0].closed);
        QVERIFY(voices.spans[1].closed);
    }
    void sampleAndLyrics()
    {
        auto p = FanqieFormat().parse(QStringLiteral(
            "# 注释\nV: 1.0\nB: 排排坐\nB: 幼儿歌曲\nZ: 佚名 词曲\nD: D\nP: 4/4\nQ: 1 1 2 - | 3 3 "
            "2 - | 5 3 3 1 | 2 - |\nC: 排排坐，吃果果，幼儿园里朋友多，\nQ: 3 3 2 - | 6, 1 2 - | 5 "
            "5 3 1 | 2 3 1 - ||\nC: 朋友多，来唱歌，唱起歌来多快活。"));
        QCOMPARE(p.title, QStringLiteral("排排坐"));
        QCOMPARE(p.subtitles, QStringList{QStringLiteral("幼儿歌曲")});
        QCOMPARE(p.voiceNames.size(), 1);
        QCOMPARE(p.notes.size(), 24);
        QCOMPARE(p.pendingLyrics.join(""), QStringLiteral("友多，"));
        QVERIFY(p.hasErrors());
        auto words =
            FanqieFormat().parse(QStringLiteral("Q: 1- 0 2 3 4\nC: 你~好，@世界\nC: 春夏秋冬"));
        QVERIFY2(!words.hasErrors(), qPrintable(words.messages().join('\n')));
        QCOMPARE(words.notes[0].lyrics[0], QStringLiteral("你好，"));
        QVERIFY(!words.notes[2].lyrics.contains(0));
        QCOMPARE(words.notes[3].lyrics[0], QStringLiteral("世"));
        QCOMPARE(words.notes[4].lyrics[1], QStringLiteral("冬"));
    }
    void musicXmlAndNativeHistory()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QString source =
            QStringLiteral("V: 1.0\nB: 中文测试\nB: 副标题\nZ: 佚名 词曲\nD: D\nP: 4/4\nQ1: 1/2/ "
                           "3- | (y1/2/3/) 8 9 ||\nC1: 你~好，世界@\nQ2: 5- 6- | 5- 6- ||\n");
        QVERIFY2(d.applyText(source), qPrintable(d.error()));
        auto s = d.snapshot();
        QCOMPARE(s.title, QStringLiteral("中文测试"));
        QCOMPARE(s.voices.size(), 2);
        QCOMPARE(s.subtitles.size(), 1);
        bool hidden = false, rhythm = false;
        for (auto m : s.measures)
            for (auto e : m.events)
            {
                hidden |= e.rest && !e.visible;
                rhythm |= e.rhythm;
            }
        QVERIFY(hidden);
        QVERIFY(rhythm);
        QVERIFY(d.canUndo());
        d.undo();
        QCOMPARE(d.snapshot().title, QStringLiteral("未命名曲谱"));
        d.redo();
        QCOMPARE(d.sourceText(), source);
        auto settings = d.viewSettings();
        settings["layout"] = LayoutSettings::defaults("children");
        d.setViewSettings(settings);
        QCOMPARE(LayoutSettings::fromJson(d.viewSettings()).notePt, 22.0);
        d.undo();
        QCOMPARE(LayoutSettings::fromJson(d.viewSettings()).notePt, 18.0);
        d.redo();
        QTemporaryDir dir;
        auto path = dir.filePath(QStringLiteral("中文工程.jianpu"));
        QVERIFY2(d.save(path), qPrintable(d.error()));
        ScoreDocument restored;
        QVERIFY2(restored.open(path), qPrintable(restored.error()));
        QCOMPARE(restored.sourceText(), source);
        QCOMPARE(restored.snapshot().noteCount(), d.snapshot().noteCount());
        QCOMPARE(LayoutSettings::fromJson(restored.viewSettings()).notePt, 22.0);
    }
    void beamsAndPresetGeometry()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY2(d.applyText("Q: 1/2/ 3//4//5/ |\n"), qPrintable(d.error()));
        auto s = d.snapshot();
        for (const auto &preset : {"classic", "modern", "children"})
        {
            auto layout = JianpuLayout().layout(s, {{"layout", LayoutSettings::defaults(preset)}});
            QCOMPARE(qRound(layout.pages[0].width()), 794);
            const auto &events = s.measures[0].events;
            QPointF a, b;
            for (auto hit : layout.hits)
            {
                if (hit.eventId == events[0].id)
                    a = hit.rect.center();
                if (hit.eventId == events[1].id)
                    b = hit.rect.center();
            }
            QVERIFY(b.x() > a.x());
            bool connected = false;
            for (auto item : layout.items)
                if (item.kind == LayoutItem::Kind::Line && item.id == events[0].id &&
                    item.bounds.left() <= a.x() + 1 && item.bounds.right() >= b.x() - 1)
                    connected = true;
            QVERIFY2(connected, preset);
        }
    }
    void hairpinEndingAtLineBoundary()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText("Q: 1 2 3 4 | <5 6 7 1! |\nQ: 1 2 3 4 |\n"));
        auto score = d.snapshot();
        auto layout = JianpuLayout().layout(score);
        const auto start = score.measures[1].events[0].id;
        double x = 0;
        for (auto hit : layout.hits)
            if (hit.eventId == start)
                x = hit.rect.center().x();
        int curves = 0;
        for (auto mark : score.marks)
            if (mark.kind == "hairpin")
                for (auto item : layout.items)
                    if (item.id == mark.id && item.kind == LayoutItem::Kind::Curve)
                    {
                        ++curves;
                        QVERIFY(item.bounds.left() >= x - 1);
                    }
        QCOMPARE(curves, 1);
    }
    void preserveLyricsOnInsertionAndUndo()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QString source = QStringLiteral("Q: 1 2 3\nC: 春夏秋\n");
        QVERIFY(d.applyText(source));
        source.replace("1 2", "1 4 2");
        QVERIFY(d.applyText(source));
        auto s = d.snapshot();
        QVector<EventData> events;
        for (auto m : s.measures)
            for (auto e : m.events)
                if (!e.rest)
                    events << e;
        QCOMPARE(events.size(), 4);
        QCOMPARE(events[0].lyrics[0].text, QStringLiteral("春"));
        QVERIFY(events[1].lyrics.isEmpty());
        QCOMPARE(events[2].lyrics[0].text, QStringLiteral("夏"));
        d.undo();
        QCOMPARE(d.snapshot().noteCount(), 3);
        d.redo();
        QCOMPARE(d.snapshot().noteCount(), 4);
    }
    void exactEngineTupletsAndLongNotes()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY2(d.applyText("Q: (y1/2/3/4/5/6/7/) (y1/2/3/4/5/6/7/) |\n"), qPrintable(d.error()));
        MusicalTime sum;
        int count = 0;
        for (auto m : d.snapshot().measures)
            for (auto e : m.events)
                if (!e.rest && !e.grace)
                {
                    sum += e.length;
                    ++count;
                }
        QCOMPARE(count, 14);
        QCOMPARE(sum, MusicalTime(1));
        QTemporaryDir dir;
        auto path = dir.filePath("seven.musicxml");
        QVERIFY2(d.exportFile(path), qPrintable(d.error()));
        ScoreDocument imported;
        QVERIFY2(imported.open(path), qPrintable(imported.error()));
        sum = {};
        for (auto m : imported.snapshot().measures)
            for (auto e : m.events)
                if (!e.rest && !e.grace)
                    sum += e.length;
        QCOMPARE(sum, MusicalTime(1));
        QVERIFY2(d.applyText("Q: 1 2--- |\n"), qPrintable(d.error()));
        int ties = 0;
        sum = {};
        for (auto m : d.snapshot().measures)
            for (auto e : m.events)
                if (!e.rest)
                {
                    sum += e.length;
                    for (auto n : e.notes)
                        ties += n.tieStart ? 1 : 0;
                }
        QCOMPARE(sum, MusicalTime(5, 4));
        QVERIFY(ties > 0);
    }
    void deletedLyricsRemainPendingUntilEdited()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QString source = QStringLiteral("B: 原题\nQ: 1 2 3\nC: 春夏秋\n");
        QVERIFY(d.applyText(source));
        source.replace("1 2 3", "1 3");
        QVERIFY(d.applyText(source));
        QVERIFY(d.inputState()["pendingLyrics"].toArray().contains(QStringLiteral("夏")));
        source.replace(QStringLiteral("原题"), QStringLiteral("改题"));
        QVERIFY(d.applyText(source));
        QVERIFY(d.inputState()["pendingLyrics"].toArray().contains(QStringLiteral("夏")));
        source.replace(QStringLiteral("春夏秋"), QStringLiteral("春秋"));
        QVERIFY(d.applyText(source));
        QVERIFY(d.inputState()["pendingLyrics"].toArray().isEmpty());
    }
    void visualDurationLyricsAndMarkStayEditable()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText("# Keep\nQ: 1 2 3 4 |\n"));
        auto first = d.snapshot().measures[0].events[0].notes[0].id;
        QVERIFY(d.edit({EditRequest::Kind::Duration, {first}, 2}));
        QVERIFY2(!d.inputState()["protected"].toBool(), qPrintable(d.inputMessages().join('\n')));
        auto parsed = FanqieFormat().parse(d.sourceText());
        QCOMPARE(parsed.notes.size(), 3);
        QCOMPARE(parsed.notes[0].duration, MusicalTime(1, 2));
        int count = d.snapshot().noteCount();
        QVERIFY(d.applyText(d.sourceText() + "# comment\n"));
        QCOMPARE(d.snapshot().noteCount(), count);
        QVERIFY(d.applyText("# Keep\nQ: 1 2 3 4 |\n"));
        first = d.snapshot().measures[0].events[0].notes[0].id;
        QVERIFY(d.edit({EditRequest::Kind::Duration, {first}, 8}));
        parsed = FanqieFormat().parse(d.sourceText());
        QCOMPARE(parsed.notes[1].degree, 0);
        QCOMPARE(parsed.notes[1].duration, MusicalTime(1, 8));
        QVERIFY(d.edit({EditRequest::Kind::Lyrics, {first}, 0, 0, QStringLiteral("中")}));
        QVERIFY(d.sourceText().contains(QStringLiteral("中")));
        QVERIFY(d.edit({EditRequest::Kind::AddMark, {first}, 0, 0, "dynamic:mp"}));
        QVERIFY(d.sourceText().contains("&mp"));
        QVERIFY(!d.inputState()["protected"].toBool());
        QVERIFY2(d.applyText(d.sourceText() + "# next\n"), qPrintable(d.error()));
        QCOMPARE(d.snapshot().measures[0].events[0].lyrics[0].text, QStringLiteral("中"));
    }
    void expressionsDraftAndVisualRoundTrip()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QString source = QStringLiteral("# 保留注释\nV: 1.0\nB: 表情测试\nQ: "
                                        "(1[2/3#]&mp\"中文注释\" (2[h4/] 3) | <4 5! 6) ||\n");
        QVERIFY2(d.applyText(source), qPrintable(d.error()));
        auto s = d.snapshot();
        int grace = 0, afterGrace = 0, slurs = 0, hairpins = 0;
        bool dynamic = false, annotation = false;
        for (auto m : s.measures)
            for (auto e : m.events)
                if (e.grace)
                {
                    ++grace;
                    afterGrace += e.graceAfter;
                }
        for (auto mark : s.marks)
        {
            slurs += mark.kind == "slur";
            hairpins += mark.kind == "hairpin";
            dynamic |= mark.kind == "dynamic";
            annotation |= mark.text.contains(QStringLiteral("中文注释"));
        }
        QCOMPARE(grace, 3);
        QCOMPARE(afterGrace, 1);
        QCOMPARE(slurs, 2);
        QCOMPARE(hairpins, 1);
        QVERIFY(dynamic);
        QVERIFY(annotation);
        QString id;
        for (auto m : s.measures)
            for (auto e : m.events)
                if (!e.grace && !e.notes.isEmpty() && id.isEmpty())
                    id = e.notes[0].id;
        QVERIFY2(d.edit({EditRequest::Kind::DiatonicUp, {id}}), qPrintable(d.error()));
        QVERIFY(d.sourceText().startsWith("# 保留注释"));
        QVERIFY(d.sourceText().contains("2[2/3#]"));
        d.undo();
        QCOMPARE(d.sourceText(), source);
        d.redo();
        source = "Q: (y1/2/3/\n";
        QVERIFY2(d.applyText(source), qPrintable(d.error()));
        QVERIFY(d.inputState()["hasErrors"].toBool());
        QVERIFY(!d.inputState()["pendingSpans"].toArray().isEmpty());
        QTemporaryDir dir;
        auto path = dir.filePath("draft.jianpu");
        QVERIFY(d.save(path));
        QVERIFY(!d.exportFile(dir.filePath("invalid.mid")));
        ScoreDocument restored;
        QVERIFY(restored.open(path));
        QCOMPARE(restored.sourceText(), source);
        QVERIFY(restored.inputState()["hasErrors"].toBool());
    }
    void textFileAndProtectedImport()
    {
        QTemporaryDir dir;
        auto file = dir.filePath(QStringLiteral("中文.jptxt"));
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write("\xef\xbb\xbfV: 1.0\r\nB: UTF-8\r\nQ: 1/2/ |\r\n");
        out.close();
        ScoreDocument d;
        QVERIFY2(d.open(file), qPrintable(d.error()));
        QCOMPARE(d.snapshot().title, QString("UTF-8"));
        QVERIFY(!d.sourceText().contains('\r'));
        auto xml = dir.filePath("import.musicxml");
        QVERIFY(d.exportFile(xml));
        ScoreDocument imported;
        QVERIFY(imported.open(xml));
        auto before = imported.snapshot().noteCount();
        // Add an unsupported chord, then ensure source editing cannot silently discard it.
        QString id;
        for (auto m : imported.snapshot().measures)
            for (auto e : m.events)
                if (!e.notes.isEmpty() && id.isEmpty())
                    id = e.notes[0].id;
        QVERIFY(imported.edit({EditRequest::Kind::AddChordNote, {id}, 67}));
        auto text = imported.sourceText();
        text.replace("1/", "3/");
        QVERIFY(!imported.applyText(text));
        QCOMPARE(imported.snapshot().noteCount(), before + 1);
    }
    void sourceEditorAndKeyboard()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *editor = w.findChild<SourceEditor *>();
        QVERIFY(editor);
        editor->setPlainText(QStringLiteral("V: 1.0\nB: 键盘输入\nQ: 1/2/3/4/ |\nC: 春夏秋冬\n"));
        QTRY_COMPARE(w.document()->snapshot().title, QStringLiteral("键盘输入"));
        w.canvas()->setFocus();
        QTest::keyClick(w.canvas(), Qt::Key_Q);
        QTRY_COMPARE(w.document()->snapshot().voices.size(), 2);
        QTest::keyClick(w.canvas(), Qt::Key_5);
        QTest::keyClick(w.canvas(), Qt::Key_Slash);
        QTRY_VERIFY(w.document()->sourceText().contains("Q2: 5/"));
        QTest::keyClick(w.canvas(), Qt::Key_C);
        auto *input = w.findChild<QLineEdit *>("keyboardTextInput");
        QVERIFY(input && input->isVisible());
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("中"));
        QApplication::sendEvent(input, &commit);
        QTRY_VERIFY(w.document()->sourceText().contains(QStringLiteral("C2: 中")));
        QTest::keyClick(input, Qt::Key_Escape);
        QTest::keyClick(w.canvas(), Qt::Key_F6);
        QTRY_VERIFY(editor->hasFocus());
    }
    void keyboardLyricsStartAtScoreBeginning()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *editor = w.findChild<SourceEditor *>();
        editor->setPlainText("Q: 1 2 |\nQ: 3 4 |\n");
        QTRY_COMPARE(w.document()->snapshot().noteCount(), 4);
        w.canvas()->setFocus();
        QTest::keyClick(w.canvas(), Qt::Key_C);
        auto *input = w.findChild<QLineEdit *>("keyboardTextInput");
        QVERIFY(input->isVisible());
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("春，夏秋冬"));
        QApplication::sendEvent(input, &commit);
        QTest::keyClick(input, Qt::Key_Return);
        auto s = w.document()->snapshot();
        QStringList words;
        for (auto m : s.measures)
            for (auto e : m.events)
                if (!e.lyrics.isEmpty())
                    words << e.lyrics[0].text;
        QCOMPARE(words, QStringList({QStringLiteral("春，"), QStringLiteral("夏"),
                                     QStringLiteral("秋"), QStringLiteral("冬")}));
        QTest::keyClick(w.canvas(), Qt::Key_C);
        QTest::keyClick(input, Qt::Key_Escape);
        QCOMPARE(w.document()->snapshot().lyricRows.value(0), 2);
    }
    void directDynamicsAndChineseAnnotation()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        w.canvas()->setFocus();
        QTest::keyClick(w.canvas(), Qt::Key_Q);
        QTest::keyClicks(w.canvas(), "1/2/&mp");
        QTRY_VERIFY(w.document()->sourceText().contains("&mp"));
        QTest::keyClick(w.canvas(), Qt::Key_QuoteDbl);
        auto *input = w.findChild<QLineEdit *>("keyboardTextInput");
        QVERIFY(input->isVisible());
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("轻轻地"));
        QApplication::sendEvent(input, &commit);
        QTest::keyClick(input, Qt::Key_Return);
        QTest::keyClicks(w.canvas(), "3/");
        QTRY_VERIFY(w.document()->sourceText().contains(QStringLiteral("\"轻轻地\"3/")));
        QVERIFY(!w.document()->inputState()["hasErrors"].toBool());
    }
    void oneUndoForEachNoteWithSuffixes()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        w.canvas()->setFocus();
        QTest::keyClick(w.canvas(), Qt::Key_Q);
        QTest::keyClicks(w.canvas(), "1/2//");
        QTRY_COMPARE(FanqieFormat().parse(w.document()->sourceText()).notes.size(), 2);
        w.document()->undo();
        auto parsed = FanqieFormat().parse(w.document()->sourceText());
        QCOMPARE(parsed.notes.size(), 1);
        QCOMPARE(parsed.notes[0].duration, MusicalTime(1, 8));
        w.document()->redo();
        parsed = FanqieFormat().parse(w.document()->sourceText());
        QCOMPARE(parsed.notes.size(), 2);
        QCOMPARE(parsed.notes[1].duration, MusicalTime(1, 16));
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("JianpuLabTests");
    EngineRuntime runtime;
    TextInputTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_textinput.moc"
