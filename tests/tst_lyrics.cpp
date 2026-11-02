// SPDX-License-Identifier: GPL-3.0-only
#include "core/runtime.h"
#include "core/scoredocument.h"
#include "layout/alignmentsnapper.h"
#include "layout/layoutsettings.h"
#include "layout/visualelement.h"
#include "ui/mainwindow.h"
#include "ui/scorecanvas.h"
#include "ui/sourceeditor.h"
#include <QApplication>
#include <QClipboard>
#include <QDoubleSpinBox>
#include <QFile>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>
using namespace jianpu;
class LyricTest : public QObject
{
    Q_OBJECT
    void ime(ScoreCanvas *c, const QString &text)
    {
        QInputMethodEvent event;
        event.setCommitString(text);
        QApplication::sendEvent(c, &event);
    }
    void select(ScoreCanvas *c, int track, int verse, int index)
    {
        QVector<LyricSlot> candidates;
        for (const auto &s : c->layoutResult().lyrics)
            if (s.track == track && s.verse == verse && !s.eventId.isEmpty())
                candidates << s;
        std::sort(candidates.begin(), candidates.end(),
                  [](const auto &a, const auto &b) { return a.tick < b.tick; });
        QVERIFY(index < candidates.size());
        auto s = candidates[index];
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier,
                          (s.rect.center() * c->zoom()).toPoint());
        QVERIFY(c->lyricSelected());
    }
    QStringList words(ScoreDocument *d, int verse)
    {
        QStringList result;
        for (const auto &m : d->snapshot().measures)
            for (const auto &e : m.events)
                if (!e.rest && !e.grace)
                {
                    QString word;
                    for (const auto &l : e.lyrics)
                        if (l.verse == verse)
                            word = l.text;
                    result << word;
                }
        return result;
    }
  private slots:
    void twoVersesOnCanvasAndExport()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *c = w.canvas();
        QVERIFY(w.document()->applyText("Q: 1 2 3 4 |\n"));
        c->setFocus();
        QTest::keyClick(c, Qt::Key_C);
        QVERIFY(c->lyricSelected());
        QCOMPARE(c->selectedLyric()->verse, 0);
        QInputMethodEvent composition("尚未提交", {});
        QApplication::sendEvent(c, &composition);
        QVERIFY(words(w.document(), 0).join("").isEmpty());
        ime(c, QStringLiteral("春夏秋冬"));
        QTest::keyClick(c, Qt::Key_C);
        QCOMPARE(c->selectedLyric()->verse, 1);
        ime(c, QStringLiteral("一二三四"));
        QCOMPARE(words(w.document(), 0).join(""), QStringLiteral("春夏秋冬"));
        QCOMPARE(words(w.document(), 1).join(""), QStringLiteral("一二三四"));
        QCOMPARE(w.document()->snapshot().lyricRows.value(0), 2);
        QVERIFY(!w.document()->inputState()["protected"].toBool());
        QTemporaryDir dir;
        QVERIFY2(w.document()->exportFile(dir.filePath("two.musicxml")),
                 qPrintable(w.document()->error()));
        w.document()->undo();
        QVERIFY(words(w.document(), 1).join("").isEmpty());
        w.document()->redo();
        QCOMPARE(words(w.document(), 1).join(""), QStringLiteral("一二三四"));
    }
    void lyricOverwriteInsertAppendAndNavigation()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *c = w.canvas();
        QVERIFY(
            w.document()->applyText(QStringLiteral("Q: 1 2 3 4 | 5 6\nC1: 春夏秋\nC1: 一二三\n")));
        select(c, 0, 0, 1);
        QCOMPARE(c->entryController()->mode(), EntryMode::Overwrite);
        ime(c, QStringLiteral("好"));
        QCOMPARE(words(w.document(), 0).mid(0, 3),
                 QStringList({QStringLiteral("春"), QStringLiteral("好"), QStringLiteral("秋")}));
        select(c, 0, 0, 1);
        QTest::keyClick(c, Qt::Key_Insert);
        ime(c, QStringLiteral("新"));
        QCOMPARE(words(w.document(), 0).mid(0, 4),
                 QStringList({QStringLiteral("春"), QStringLiteral("好"), QStringLiteral("新"),
                              QStringLiteral("秋")}));
        QCOMPARE(words(w.document(), 1).mid(0, 3),
                 QStringList({QStringLiteral("一"), QStringLiteral("二"), QStringLiteral("三")}));
        select(c, 0, 0, 4);
        QCOMPARE(c->entryController()->mode(), EntryMode::Append);
        ime(c, QStringLiteral("天"));
        QCOMPARE(words(w.document(), 0)[4], QStringLiteral("天"));
        select(c, 0, 0, 1);
        QTest::keyClick(c, Qt::Key_Up);
        QVERIFY(!c->lyricSelected());
        QVERIFY(!c->modelSelection().isEmpty());
        QTest::keyClick(c, Qt::Key_Down);
        QVERIFY(c->lyricSelected());
        QCOMPARE(c->selectedLyric()->verse, 0);
        QTest::keyClick(c, Qt::Key_Down);
        QCOMPARE(c->selectedLyric()->verse, 1);
        auto before = w.document()->viewSettings();
        auto center = c->selectedLyric()->rect.center() * c->zoom();
        QTest::mousePress(c, Qt::LeftButton, Qt::NoModifier, center.toPoint());
        QTest::mouseMove(c, (center + QPointF(20, 5)).toPoint());
        QTest::mouseRelease(c, Qt::LeftButton);
        QCOMPARE(w.document()->viewSettings(), before);
    }
    void lyricsWrapIntoFollowingMusicRow()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        auto v = d.viewSettings();
        v["layout"] = QJsonObject{{"measuresPerLine", 1}};
        d.setViewSettings(v);
        QVERIFY(d.applyText(QStringLiteral("Q: 1 2 |\nC1: 春夏秋冬\nQ: 3 4 |\nC1:\n")));
        QCOMPARE(words(&d, 0).join(""), QStringLiteral("春夏秋冬"));
        QVERIFY2(!d.textSnapshot().hasErrors(), qPrintable(d.inputMessages().join('\n')));
        QCOMPARE(d.sourceText().count("Q:"), 2);
        QCOMPARE(d.sourceText().count("C1:"), 2);
        QVERIFY(d.inputState()["pendingLyrics"].toArray().isEmpty());
        QTemporaryDir dir;
        QVERIFY(d.exportFile(dir.filePath("wrapped.xml")));
    }
    void crossedBarDurationDoesNotLockLyrics()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *c = w.canvas();
        QVERIFY(w.document()->applyText("Q: 1 2 3 4 | 5 6 7 1 |\n"));
        auto id = w.document()->snapshot().measures[0].events[3].notes[0].id;
        QVERIFY(w.document()->edit({EditRequest::Kind::Duration, {id}, 2, 0}));
        QVERIFY(!w.document()->inputState()["protected"].toBool());
        c->setFocus();
        QTest::keyClick(c, Qt::Key_C);
        ime(c, QStringLiteral("春夏秋冬"));
        QTest::keyClick(c, Qt::Key_C);
        ime(c, QStringLiteral("一二三四"));
        QVERIFY2(!w.document()->inputState().contains("draftSource"),
                 qPrintable(w.document()->inputMessages().join('\n')));
        QCOMPARE(words(w.document(), 1).join(""), QStringLiteral("一二三四"));
    }
    void numberingDoesNotShiftWords()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(QStringLiteral("Q: 1 2 3 4 |\nC1: 春夏秋冬\nC1: 一二三四\n")));
        auto before = d.presentation();
        auto view = d.viewSettings();
        view["layout"] = QJsonObject{{"showLyricNumbers", true}};
        d.setViewSettings(view);
        QCOMPARE(before.lyrics.size(), d.presentation().lyrics.size());
        for (int i = 0; i < before.lyrics.size(); ++i)
            QCOMPARE(before.lyrics[i].rect, d.presentation().lyrics[i].rect);
        int count = 0;
        for (const auto &item : d.presentation().items)
            if (item.id.startsWith("@lyric-number:"))
            {
                ++count;
                QVERIFY(item.bounds.right() < d.presentation().systems.first().bounds.left());
            }
        QCOMPARE(count, 2);
    }
    void measureNumbersDoNotOverlapAboveNoteAnnotations()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(QStringLiteral("Q: 1\"轻快\" 2 3 4 |\n")));
        QString annotation;
        for (const auto &mark : d.snapshot().marks)
            if (mark.kind == "text")
                annotation = mark.id;
        QVERIFY(!annotation.isEmpty());
        for (const auto &preset : {"classic", "modern", "children"})
        {
            auto view = d.viewSettings();
            auto settings = LayoutSettings::defaults(preset);
            settings["showMeasures"] = true;
            view["layout"] = settings;
            d.setViewSettings(view);
            QRectF number, label;
            for (const auto &item : d.presentation().items)
            {
                if (item.id == d.snapshot().measures[0].id && item.kind == LayoutItem::Kind::Text)
                    number = item.bounds;
                if (item.id == annotation)
                    label = item.bounds;
            }
            QVERIFY(!number.isEmpty());
            QVERIFY(!label.isEmpty());
            QVERIFY(!number.intersects(label));
        }
    }
    void transformsPersistAndHitTestingFollows()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(QStringLiteral("B: 旋转标题\nQ: 1 2 3 4 |\nC: 春夏秋冬\n")));
        QVERIFY(d.transformElement("@title:0", 30, 1.5));
        QVERIFY(d.moveElement("@title:0", {12, 9}));
        auto element = d.presentation().elements.value("@title:0");
        auto bounds = element->bounds(d.presentation());
        QVERIFY(d.presentation().hitTest(bounds.center()));
        QCOMPARE(d.presentation().hitTest(bounds.center())->id, QString("@title:0"));
        QVERIFY(!d.transformElement(d.presentation().lyrics.first().id, 30, 2));
        QTemporaryDir dir;
        QVERIFY(d.save(dir.filePath("transform.jianpu")));
        ScoreDocument r;
        QVERIFY(r.open(dir.filePath("transform.jianpu")));
        QCOMPARE(r.presentation().elements.value("@title:0")->bounds(r.presentation()), bounds);
        auto snap = AlignmentSnapper::snap({101, 80, 40, 20}, {{100, 30, 40, 20}}, 3);
        QCOMPARE(snap.correction.x(), -1.0);
        QVERIFY(!snap.guides.isEmpty());
    }
    void nativeLyricsCanBeChangedWithoutRebuildingMusic()
    {
        QTemporaryDir dir;
        QString path = dir.filePath("native.musicxml");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(
            R"(<score-partwise version="4.0"><part-list><score-part id="P1"><part-name>Violin</part-name></score-part></part-list><part id="P1"><measure number="1"><attributes><divisions>1</divisions><key><fifths>0</fifths></key><time><beats>4</beats><beat-type>4</beat-type></time></attributes><note><pitch><step>C</step><octave>4</octave></pitch><duration>1</duration><type>quarter</type></note><note><chord/><pitch><step>E</step><octave>4</octave></pitch><duration>1</duration><type>quarter</type></note><note><pitch><step>D</step><octave>4</octave></pitch><duration>1</duration><type>quarter</type></note><note><rest/><duration>2</duration><type>half</type></note></measure></part></score-partwise>)");
        file.close();
        ScoreDocument d;
        QVERIFY(d.open(path));
        QVERIFY(d.inputState()["protected"].toBool());
        int count = d.snapshot().noteCount();
        auto instrument = d.snapshot().voices.first().name;
        auto source = d.sourceText() + QStringLiteral("C1: 春夏\nC1: 一二\n");
        QVERIFY2(d.applyText(source), qPrintable(d.error()));
        QCOMPARE(d.snapshot().noteCount(), count);
        QCOMPARE(d.snapshot().voices.first().name, instrument);
        QCOMPARE(words(&d, 1).join(""), QStringLiteral("一二"));
        QVERIFY2(d.exportFile(dir.filePath("native-out.musicxml")), qPrintable(d.error()));
        d.undo();
        QVERIFY(words(&d, 1).join("").isEmpty());
        d.redo();
        QCOMPARE(words(&d, 1).join(""), QStringLiteral("一二"));
    }
    void assigningPendingWordsClearsOnlyThoseWords()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        QVERIFY(w.document()->applyText(QStringLiteral("Q: 1 2 3\nC: 春夏秋\n")));
        auto source = w.document()->sourceText();
        source.replace("1 2 3", "1 3");
        QVERIFY(w.document()->applyText(source));
        QVERIFY(!w.document()->inputState()["pendingLyrics"].toArray().isEmpty());
        select(w.canvas(), 0, 0, 1);
        ime(w.canvas(), QStringLiteral("夏"));
        QVERIFY(w.document()->inputState()["pendingLyrics"].toArray().isEmpty());
        QTemporaryDir dir;
        QVERIFY2(w.document()->exportFile(dir.filePath("assigned.xml")),
                 qPrintable(w.document()->error()));
    }
    void sourceCaretAndVisualLyricSelectEachOther()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        QVERIFY(
            w.document()->applyText(QStringLiteral("Q: 1 2 3 4 |\nC1: 春夏秋冬\nC1: 一二三四\n")));
        select(w.canvas(), 0, 1, 1);
        auto *editor = w.findChild<SourceEditor *>();
        QCOMPARE(editor->textCursor().selectedText(), QStringLiteral("二"));
        QTest::mouseClick(editor->viewport(), Qt::LeftButton);
        QTRY_VERIFY(editor->hasFocus());
        auto cursor = editor->textCursor();
        cursor.setPosition(editor->toPlainText().indexOf(QStringLiteral("秋")));
        editor->setTextCursor(cursor);
        QVERIFY(w.canvas()->lyricSelected());
        QCOMPARE(w.canvas()->selectedLyric()->text, QStringLiteral("秋"));
    }
    void graphicHandlesRotateAndScale()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *c = w.canvas();
        QVERIFY(w.document()->applyText(QStringLiteral("B: 图形变换\nQ: 1 2 3 4 |\n")));
        auto element = c->layoutResult().elements.value("@title:0");
        auto box = element->bounds(c->layoutResult());
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, (box.center() * c->zoom()).toPoint());
        auto origin = element->transformOrigin(c->layoutResult());
        QPointF handle(box.center().x(), box.top() - 18);
        auto end = origin + QPointF(QLineF(origin, handle).length(), 0);
        QTest::mousePress(c, Qt::LeftButton, Qt::NoModifier, (handle * c->zoom()).toPoint());
        QTest::mouseMove(c, (end * c->zoom()).toPoint());
        QTest::mouseRelease(c, Qt::LeftButton, Qt::NoModifier, (end * c->zoom()).toPoint());
        QVERIFY(std::abs(w.findChild<QDoubleSpinBox *>("elementRotation")->value()) > 10);
        element = c->layoutResult().elements.value("@title:0");
        box = element->bounds(c->layoutResult());
        origin = element->transformOrigin(c->layoutResult());
        handle = box.bottomRight();
        end = origin + (handle - origin) * 1.2;
        QTest::mousePress(c, Qt::LeftButton, Qt::NoModifier, (handle * c->zoom()).toPoint());
        QTest::mouseMove(c, (end * c->zoom()).toPoint());
        QTest::mouseRelease(c, Qt::LeftButton, Qt::NoModifier, (end * c->zoom()).toPoint());
        QVERIFY(w.findChild<QDoubleSpinBox *>("elementScale")->value() > 110);
        int notes = w.document()->snapshot().noteCount();
        QTest::keyClick(c, Qt::Key_Down); // ordinary arrows navigate away from graphic text
        QCOMPARE(w.document()->snapshot().noteCount(), notes);
    }
    void lyricClipboardAndDeleteDoNotChangeMusic()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        QVERIFY(w.document()->applyText(QStringLiteral("Q: 1 2 3 4 |\nC1: 春夏秋\n")));
        auto *c = w.canvas();
        select(c, 0, 0, 1);
        QTest::keySequence(c, QKeySequence::Copy);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("夏"));
        QTest::keyClick(c, Qt::Key_Delete);
        QCOMPARE(w.document()->snapshot().noteCount(), 4);
        QCOMPARE(words(w.document(), 0).mid(0, 2),
                 QStringList({QStringLiteral("春"), QStringLiteral("秋")}));
        QTest::keySequence(c, QKeySequence::Paste);
        QCOMPARE(w.document()->snapshot().noteCount(), 4);
        QCOMPARE(words(w.document(), 0)[1], QStringLiteral("夏"));
    }
    void completedLongVerseStaysAtEnd()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto view = w.document()->viewSettings();
        view["layout"] = QJsonObject{{"measuresPerLine", 1}};
        w.document()->setViewSettings(view);
        QVERIFY(w.document()->applyText("Q: 1 2 3 4 | 5 6 7 1 | 2 3 4 5 |\n"));
        auto *c = w.canvas();
        c->setFocus();
        QTest::keyClick(c, Qt::Key_C);
        ime(c, QStringLiteral("春夏秋冬山水天地清风明月"));
        QVERIFY(c->selectedLyric());
        QCOMPARE(c->selectedLyric()->system, 2);
        ime(c, QStringLiteral("多"));
        QCOMPARE(words(w.document(), 0).join(""), QStringLiteral("春夏秋冬山水天地清风明月"));
        QVERIFY(!w.document()->inputState()["pendingLyrics"].toArray().isEmpty());
    }
    void validLegacyDraftRecoversOnOpen()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(QStringLiteral("Q: 1 2 3 4 |\nC1: 春夏秋冬\n")));
        auto draft = d.sourceText() + QStringLiteral("C1: 一二三四\n");
        d.storeInputDraft(draft, QStringLiteral("此曲谱含受保护对象，请使用谱面编辑"));
        QTemporaryDir dir;
        QVERIFY(d.save(dir.filePath("legacy.jianpu")));
        ScoreDocument r;
        QVERIFY(r.open(dir.filePath("legacy.jianpu")));
        QVERIFY(!r.inputState().contains("draftSource"));
        QCOMPARE(words(&r, 1).join(""), QStringLiteral("一二三四"));
        QVERIFY2(r.exportFile(dir.filePath("recovered.xml")), qPrintable(r.error()));
    }
    void trueOverflowCanBeResolvedAndExported()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(QStringLiteral("Q: 1 2\nC: 春夏秋冬\n")));
        QTemporaryDir dir;
        QVERIFY(!d.exportFile(dir.filePath("error.xml")));
        d.discardPendingLyrics();
        QVERIFY2(d.exportFile(dir.filePath("resolved.xml")), qPrintable(d.error()));
        d.undo();
        QVERIFY(!d.exportFile(dir.filePath("again.xml")));
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    EngineRuntime runtime;
    LyricTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_lyrics.moc"
