// SPDX-License-Identifier: GPL-3.0-only
#include "core/runtime.h"
#include "core/scoredocument.h"
#include "ui/mainwindow.h"
#include "ui/scorecanvas.h"
#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>
using namespace jianpu;
class SelectionTest : public QObject
{
    Q_OBJECT
    QString sample()
    {
        return QStringLiteral(
            "Q1: 1 2 3 4 | 5 6 7 1' | 2' 3' 4' 5' |\nC1: 春夏秋冬山水天地清风明月\nC1: "
            "一二三四五六七八一起歌唱\nQ2: 5- 6- | 7- 1'- | 2'- 3'- |\n");
    }
    QVector<EventData> events(ScoreDocument &d, int track = 0)
    {
        QVector<EventData> result;
        for (const auto &m : d.snapshot().measures)
            for (const auto &e : m.events)
                if (e.track == track && !e.grace && !e.rest)
                    result << e;
        return result;
    }
    QPoint point(ScoreCanvas *c, const QString &id)
    {
        for (const auto &h : c->layoutResult().hits)
            if (h.id == id)
                return (h.rect.center() * c->zoom()).toPoint();
        return {};
    }
    QPoint background(ScoreCanvas *c, int measure)
    {
        for (const auto &m : c->layoutResult().measures)
            if (m.index == measure && m.track == 0)
                return ((m.rect.topLeft() + QPointF(8, 5)) * c->zoom()).toPoint();
        return {};
    }
  private slots:
    void measuresInsertOverwriteAndUndo()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(sample()));
        auto before = d.sourceText();
        ScoreSelection s;
        s.kind = SelectionKind::Measures;
        s.measures = {0, 1};
        auto clip = d.copyFragment(s);
        QVERIFY(!clip.isEmpty());
        ScoreDropTarget target;
        target.measure = 2;
        QVERIFY2(d.pasteFragment(clip, target, true), qPrintable(d.error()));
        QCOMPARE(d.snapshot().measures.size(), 5);
        QCOMPARE(d.snapshot().noteCount(), 30);
        d.undo();
        QCOMPARE(d.sourceText(), before);
        d.redo();
        QCOMPARE(d.snapshot().measures.size(), 5);
        d.undo();
        target.measure = 1;
        QVERIFY2(d.pasteFragment(clip, target, false), qPrintable(d.error()));
        QCOMPARE(d.snapshot().measures.size(), 3);
        QCOMPARE(d.snapshot().measures[1].events[0].notes[0].midi, 60);
        QVERIFY(!d.inputState()["protected"].toBool());
        auto source = d.sourceText();
        source.replace("Q1: 1 ", "Q1: 2 ");
        QVERIFY2(d.applyText(source), qPrintable(d.error()));
    }
    void nonAdjacentMeasuresCopyAndMove()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(sample()));
        ScoreSelection s;
        s.kind = SelectionKind::Measures;
        s.measures = {0, 2};
        auto clip = d.copyFragment(s);
        ScoreDropTarget target;
        target.measure = 3;
        QVERIFY2(d.pasteFragment(clip, target, true), qPrintable(d.error()));
        QCOMPARE(d.snapshot().measures.size(), 5);
        QCOMPARE(d.snapshot().measures[3].events[0].notes[0].midi, 60);
        QCOMPARE(d.snapshot().measures[4].events[0].notes[0].midi, 74);
        d.undo();
        s.measures = {0};
        clip = d.copyFragment(s);
        QVERIFY2(d.pasteFragment(clip, target, true, true), qPrintable(d.error()));
        QCOMPARE(d.snapshot().measures.size(), 3);
        QCOMPARE(d.snapshot().measures[0].events[0].notes[0].midi, 67);
        QCOMPARE(d.snapshot().measures[2].events[0].notes[0].midi, 60);
        d.undo();
        s.measures = {0, 2};
        QVERIFY2(d.deleteFragment(s), qPrintable(d.error()));
        QCOMPARE(d.snapshot().measures.size(), 1);
    }
    void nonAdjacentDestinationsOverwriteOnlySelectedMeasures()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(sample()));
        auto before = d.sourceText();
        ScoreSelection s;
        s.kind = SelectionKind::Measures;
        s.measures = {0, 1};
        auto clip = d.copyFragment(s);
        ScoreDropTarget target;
        target.measure = 0;
        target.measures = {0, 2};
        QVERIFY2(d.pasteFragment(clip, target, false), qPrintable(d.error()));
        auto score = d.snapshot();
        QCOMPARE(score.measures.size(), 3);
        QCOMPARE(score.measures[0].events[0].notes[0].midi, 60);
        QCOMPARE(score.measures[1].events[0].notes[0].midi, 67);
        QCOMPARE(score.measures[2].events[0].notes[0].midi, 67);
        d.undo();
        QCOMPARE(d.sourceText(), before);
        QVERIFY(!d.pasteFragment(
            QByteArrayLiteral("{\"version\":1,\"kind\":\"notes\",\"blocks\":[]}"), {}, true));
        QCOMPARE(d.sourceText(), before);
        QCOMPARE(d.snapshot().measures.size(), 3);
        auto damaged = QJsonDocument::fromJson(clip).object();
        auto blocks = damaged["blocks"].toArray();
        auto block = blocks[0].toObject();
        block["d"] = 0;
        blocks[0] = block;
        damaged["blocks"] = blocks;
        QVERIFY(!d.pasteFragment(QJsonDocument(damaged).toJson(), target, true));
        QCOMPARE(d.sourceText(), before);
    }
    void notesInsertAndMoveKeepLyrics()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(QStringLiteral(
            "Q: 1 2 3 4 | 5 6 7 1' |\nC1: 春夏秋冬山水天地\nC1: 一二三四五六七八\n")));
        auto notes = events(d);
        ScoreSelection s;
        s.kind = SelectionKind::Notes;
        s.ids = {notes[0].notes[0].id, notes[1].notes[0].id};
        auto clip = d.copyFragment(s);
        ScoreDropTarget target;
        target.position = MusicalTime(1);
        target.track = 0;
        QVERIFY2(d.pasteFragment(clip, target, true), qPrintable(d.error()));
        QCOMPARE(d.snapshot().noteCount(), 10);
        auto after = events(d);
        QCOMPARE(after[4].notes[0].midi, 60);
        QCOMPARE(after[4].lyrics[0].text, QStringLiteral("春"));
        d.undo();
        QVERIFY2(d.pasteFragment(clip, target, true, true), qPrintable(d.error()));
        QCOMPARE(d.snapshot().noteCount(), 8);
        after = events(d);
        QCOMPARE(after[0].notes[0].midi, 64);
        QCOMPARE(after[2].notes[0].midi, 60);
        QCOMPARE(after[2].lyrics[1].text, QStringLiteral("一"));
        QCOMPARE(d.snapshot().measures.size(), 2);
        QVERIFY(!d.inputState()["protected"].toBool());
    }
    void bulkEditsRetainCommentsPendingWordsAndDrafts()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(QStringLiteral("# 创作备注\nB: 标题\nU: 原样保留\n\nQ: 1 2 3 4 | 5 6 7 "
                                           "1' |\n# 段落注释\nC1: 春夏秋冬山水天地\n")));
        auto before = d.sourceText();
        auto notes = events(d);
        ScoreSelection s;
        s.kind = SelectionKind::Notes;
        s.ids = {notes[0].notes[0].id, notes[1].notes[0].id};
        auto clip = d.copyFragment(s);
        ScoreDropTarget target;
        target.position = MusicalTime(1);
        QVERIFY2(d.pasteFragment(clip, target, true, true), qPrintable(d.error()));
        QVERIFY(d.sourceText().contains(QStringLiteral("# 创作备注")));
        QVERIFY(d.sourceText().contains(QStringLiteral("# 段落注释")));
        QVERIFY(d.sourceText().contains(QStringLiteral("U: 原样保留")));
        d.undo();
        QCOMPARE(d.sourceText(), before);
        QVERIFY(d.deleteFragment(s));
        auto pending = d.inputState()["pendingLyrics"].toArray();
        QVERIFY(pending.contains(QStringLiteral("春")));
        QVERIFY(pending.contains(QStringLiteral("夏")));
        d.undo();
        QCOMPARE(d.sourceText(), before);
        auto draft = before + "Q: [";
        d.storeInputDraft(draft, "未闭合倚音");
        QVERIFY(!d.pasteFragment(clip, target, true));
        QCOMPARE(d.sourceText(), draft);
        QCOMPARE(d.snapshot().noteCount(), 8);
    }
    void lyricRangeMoveDoesNotTouchMusic()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(QStringLiteral("Q: 1 2 3 4 | 5 6 7 1' |\nC1: 春夏秋冬山水天地\n")));
        auto notes = events(d);
        ScoreSelection s;
        s.kind = SelectionKind::Lyrics;
        s.ids = {notes[0].id, notes[1].id};
        auto clip = d.copyFragment(s);
        ScoreDropTarget target;
        target.position = MusicalTime(1);
        target.track = 0;
        QVERIFY2(d.pasteFragment(clip, target, true, true), qPrintable(d.error()));
        QCOMPARE(d.snapshot().noteCount(), 8);
        QString words;
        for (const auto &e : events(d))
            for (const auto &l : e.lyrics)
                if (l.verse == 0)
                    words += l.text;
        QCOMPARE(words, QStringLiteral("秋冬春夏山水天地"));
        d.undo();
    }
    void chordToneCopiesAndMovesWithoutOtherTones()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText("Q: 1 2 3 4 |\n"));
        auto first = events(d)[0].notes[0].id;
        QVERIFY(d.edit({EditRequest::Kind::AddChordNote, {first}, 64}));
        auto before = events(d);
        QString tone;
        for (const auto &n : before[0].notes)
            if (n.midi == 64)
                tone = n.id;
        ScoreSelection s;
        s.kind = SelectionKind::Notes;
        s.ids = {tone};
        auto clip = d.copyFragment(s);
        ScoreDropTarget target;
        target.position = MusicalTime(1, 4);
        QVERIFY2(d.pasteFragment(clip, target, false), qPrintable(d.error()));
        auto after = events(d);
        QCOMPARE(after[0].notes.size(), 2);
        QCOMPARE(after[1].notes.size(), 1);
        QCOMPARE(after[1].notes[0].midi, 64);
        d.undo();
        target.position = MusicalTime(3, 4);
        QVERIFY2(d.pasteFragment(clip, target, true, true), qPrintable(d.error()));
        after = events(d);
        QCOMPARE(after[0].notes.size(), 1);
        QCOMPARE(after[0].notes[0].midi, 60);
        QCOMPARE(d.snapshot().noteCount(), 5);
    }
    void copiesBetweenVoicesInOneStaff()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText("Q: 1 2 3 4 |\n"));
        auto first = events(d)[0].notes[0].id;
        QVERIFY(d.edit({EditRequest::Kind::AddVoice, {first}}));
        QString rest;
        for (const auto &m : d.snapshot().measures)
            for (const auto &e : m.events)
                if (e.track == 1 && rest.isEmpty())
                    rest = e.id;
        QVERIFY(!rest.isEmpty());
        QVERIFY(d.edit({EditRequest::Kind::SetDegree, {rest}, 5, 4}));
        auto voice = events(d, 1);
        QVERIFY(!voice.isEmpty());
        ScoreSelection s;
        s.kind = SelectionKind::Notes;
        s.track = 1;
        s.ids = {voice[0].notes[0].id};
        auto clip = d.copyFragment(s);
        ScoreDropTarget target;
        target.position = MusicalTime(1, 4);
        target.track = 0;
        QVERIFY2(d.pasteFragment(clip, target, false), qPrintable(d.error()));
        QCOMPARE(events(d, 0)[1].notes[0].midi, 67);
        QCOMPARE(events(d, 1)[0].notes[0].midi, 67);
        d.undo();
        s.track = 0;
        s.ids = {events(d, 0)[0].notes[0].id};
        clip = d.copyFragment(s);
        target.track = 1;
        QVERIFY2(d.pasteFragment(clip, target, false), qPrintable(d.error()));
        QCOMPARE(events(d, 1)[1].notes[0].midi, 60);
    }
    void repeatAndTupletMeasureCopiesPreserveSemantics()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(
            QStringLiteral("Q: |: (y1/2/3/) 4 5 6 :| 1 2 3 4 ||\nC1: 春夏秋冬山水天地清风\n")));
        ScoreSelection s;
        s.kind = SelectionKind::Measures;
        s.measures = {0};
        auto clip = d.copyFragment(s);
        ScoreDropTarget target;
        target.measure = 2;
        QVERIFY2(d.pasteFragment(clip, target, true), qPrintable(d.error()));
        auto score = d.snapshot();
        QCOMPARE(score.measures.size(), 3);
        QVERIFY(score.measures[2].repeatStart);
        QVERIFY(score.measures[2].repeatEnd);
        int tuplets = 0;
        for (const auto &e : score.measures[2].events)
            if (e.tuplet == 3)
                ++tuplets;
        QCOMPARE(tuplets, 3);
        QVERIFY2(!d.textSnapshot().hasErrors(), qPrintable(d.inputMessages().join('\n')));
    }
    void fragmentProjectionPreservesAccidentalCancellation()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText("Q: 1# 1= 2 3 |\n"));
        ScoreSelection s;
        s.kind = SelectionKind::Measures;
        s.measures = {0};
        auto clip = d.copyFragment(s);
        ScoreDropTarget target;
        target.measure = 1;
        QVERIFY2(d.pasteFragment(clip, target, true), qPrintable(d.error()));
        auto native = events(d);
        auto parsed = d.textSnapshot();
        QCOMPARE(parsed.notes.size(), native.size());
        for (int i = 0; i < native.size(); ++i)
            QCOMPARE(parsed.notes[i].midi, native[i].notes[0].midi);
        auto source = d.sourceText();
        source.prepend("B: 升降号往返\n");
        QVERIFY2(d.applyText(source), qPrintable(d.error()));
        auto after = events(d);
        for (int i = 0; i < native.size(); ++i)
            QCOMPARE(after[i].notes[0].midi, native[i].notes[0].midi);
    }
    void completeTupletNoteGroupCopiesWithSlurAndLyrics()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(
            QStringLiteral("Q: ((y1/2/3/)) 4 5 6 | 1 2 3 4 |\nC1: 春夏秋冬山水天地清风\n")));
        auto notes = events(d);
        ScoreSelection s;
        s.kind = SelectionKind::Notes;
        for (int i = 0; i < 3; ++i)
            s.ids << notes[i].notes[0].id;
        auto clip = d.copyFragment(s);
        ScoreDropTarget target;
        target.position = MusicalTime(1);
        QVERIFY2(d.pasteFragment(clip, target, true), qPrintable(d.error()));
        int tuplets = 0, slurs = 0;
        for (const auto &e : events(d))
            if (e.tuplet == 3)
                ++tuplets;
        for (const auto &m : d.snapshot().marks)
            if (m.kind == "slur")
                ++slurs;
        QCOMPARE(tuplets, 6);
        QCOMPARE(slurs, 2);
        QCOMPARE(events(d)[6].lyrics[0].text, QStringLiteral("春"));
        QVERIFY2(!d.textSnapshot().hasErrors(), qPrintable(d.inputMessages().join('\n')));
    }
    void shiftControlAndDragSelect()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        QVERIFY(w.document()->applyText(sample()));
        auto *c = w.canvas();
        c->setZoom(0.65);
        auto notes = events(*w.document());
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, point(c, notes[0].notes[0].id));
        QTest::mouseClick(c, Qt::LeftButton, Qt::ShiftModifier, point(c, notes[3].notes[0].id));
        QCOMPARE(c->selection().size(), 4);
        QTest::mouseClick(c, Qt::LeftButton, Qt::ControlModifier, point(c, notes[5].notes[0].id));
        QCOMPARE(c->selection().size(), 5);
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, background(c, 0));
        QCOMPARE(c->contentSelection().measures, QVector<int>{0});
        QTest::mouseClick(c, Qt::LeftButton, Qt::ShiftModifier, background(c, 2));
        QCOMPARE(c->contentSelection().measures, (QVector<int>{0, 1, 2}));
        QTest::mouseClick(c, Qt::LeftButton, Qt::ControlModifier, background(c, 1));
        QCOMPARE(c->contentSelection().measures, (QVector<int>{0, 2}));
        QTest::keyClick(c, Qt::Key_Delete);
        QCOMPARE(w.document()->snapshot().measures.size(), 1);
        w.document()->undo();
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, point(c, notes[0].notes[0].id));
        QTest::mousePress(c, Qt::LeftButton, Qt::NoModifier, point(c, notes[1].notes[0].id));
        QTest::mouseMove(c, point(c, notes[3].notes[0].id));
        QTest::mouseRelease(c, Qt::LeftButton);
        QCOMPARE(c->selection().size(), 3);
    }
    void draggingSelectedNoteMovesContent()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        QVERIFY(w.document()->applyText("Q: 1 2 3 4 | 5 6 7 1' |\n"));
        auto *c = w.canvas();
        c->setZoom(0.7);
        auto notes = events(*w.document());
        auto a = point(c, notes[0].notes[0].id), b = point(c, notes[3].notes[0].id);
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, a);
        QTest::mousePress(c, Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(c, b);
        QTest::mouseRelease(c, Qt::LeftButton, Qt::NoModifier, b);
        auto after = events(*w.document());
        QCOMPARE(after.size(), 8);
        QCOMPARE(after[0].notes[0].midi, 62);
        QCOMPARE(after[2].notes[0].midi, 60);
        w.document()->undo();
        QCOMPARE(events(*w.document())[0].notes[0].midi, 60);
    }
    void dragMeasuresAndControlDragCopy()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        QVERIFY(w.document()->applyText(sample()));
        auto *c = w.canvas();
        c->setZoom(0.65);
        auto a = background(c, 0), b = background(c, 2);
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, a);
        QTest::mousePress(c, Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(c, b);
        QTest::mouseRelease(c, Qt::LeftButton, Qt::NoModifier, b);
        QCOMPARE(w.document()->snapshot().measures.size(), 3);
        QCOMPARE(w.document()->snapshot().measures[0].events[0].notes[0].midi, 67);
        QCOMPARE(w.document()->snapshot().measures[1].events[0].notes[0].midi, 60);
        w.document()->undo();
        a = background(c, 0);
        b = background(c, 2);
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, a);
        QTest::mousePress(c, Qt::LeftButton, Qt::ControlModifier, a);
        QTest::mouseMove(c, b);
        QTest::mouseRelease(c, Qt::LeftButton, Qt::ControlModifier, b);
        QCOMPARE(w.document()->snapshot().measures.size(), 4);
        QCOMPARE(w.document()->snapshot().measures[0].events[0].notes[0].midi, 60);
        QCOMPARE(w.document()->snapshot().measures[2].events[0].notes[0].midi, 60);
    }
    void wholeMeasurePasteUsesClickedNoteMeasure()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        QVERIFY(w.document()->applyText(sample()));
        auto *c = w.canvas();
        c->setZoom(0.65);
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, background(c, 0));
        c->copy();
        auto id = events(*w.document())[4].notes[0].id;
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, point(c, id));
        c->paste();
        QCOMPARE(w.document()->snapshot().measures.size(), 3);
        QCOMPARE(w.document()->snapshot().measures[1].events[0].notes[0].midi, 60);
        w.document()->undo();
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, background(c, 1));
        QTest::keyClick(c, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(w.document()->snapshot().measures.size(), 4);
        QCOMPARE(w.document()->snapshot().measures[1].events[0].notes[0].midi, 60);
        QCOMPARE(w.document()->snapshot().measures[2].events[0].notes[0].midi, 67);
    }
    void lyricRangeSelectionPasteAndExactTupletDrop()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        QVERIFY(w.document()->applyText(
            QStringLiteral("Q: (y7:4 1/2/3/4/5/6/7/) 1 2 |\nC1: 春夏秋冬山水天地清\n")));
        auto *c = w.canvas();
        c->setZoom(0.7);
        auto lyricPoint = [&](int index)
        { return (c->layoutResult().lyrics[index].rect.center() * c->zoom()).toPoint(); };
        auto a = lyricPoint(0), b = lyricPoint(1), destination = lyricPoint(4);
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseClick(c, Qt::LeftButton, Qt::ShiftModifier, b);
        QCOMPARE(c->contentSelection().ids.size(), 2);
        QTest::mousePress(c, Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(c, destination);
        QTest::mouseRelease(c, Qt::LeftButton, Qt::NoModifier, destination);
        auto after = events(*w.document());
        QCOMPARE(after[2].lyrics[0].text, QStringLiteral("春"));
        QCOMPARE(after[3].lyrics[0].text, QStringLiteral("夏"));
        QCOMPARE(after[4].lyrics[0].text, QStringLiteral("山"));
        QCOMPARE(after[3].position, MusicalTime(3, 14));
        w.document()->undo();
        a = lyricPoint(0);
        b = lyricPoint(1);
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseClick(c, Qt::LeftButton, Qt::ShiftModifier, b);
        QApplication::clipboard()->setText(QStringLiteral("你"));
        c->paste();
        after = events(*w.document());
        QCOMPARE(after[0].lyrics[0].text, QStringLiteral("你"));
        QCOMPARE(after[1].lyrics[0].text, QStringLiteral("秋"));
        QCOMPARE(after.size(), 9);
        w.document()->undo();
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, lyricPoint(0));
        c->copy();
        QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier, lyricPoint(4));
        QTest::mouseClick(c, Qt::LeftButton, Qt::ShiftModifier, lyricPoint(5));
        c->paste();
        after = events(*w.document());
        QCOMPARE(after[4].lyrics[0].text, QStringLiteral("春"));
        QCOMPARE(after[5].lyrics[0].text, QStringLiteral("天"));
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    EngineRuntime runtime;
    SelectionTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_selection.moc"
