// SPDX-License-Identifier: GPL-3.0-only
#include "core/runtime.h"
#include "core/scoredocument.h"
#include "layout/visualelement.h"
#include "ui/mainwindow.h"
#include "ui/scorecanvas.h"
#include "ui/sourceeditor.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include <QLabel>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
using namespace jianpu;
class ProjectionTest : public QObject
{
    Q_OBJECT
  private slots:
    void longTextWithOverflowStillProjectsEveryRow()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QString source = "Q: " + QString("1 2 3 4 ").repeated(24) +
                         "\nC1: " + QStringLiteral("春夏秋冬").repeated(24) +
                         QStringLiteral("多出歌词") +
                         "\nC1: " + QStringLiteral("一二三四").repeated(24) + "\n";
        QVERIFY2(d.applyText(source), qPrintable(d.error()));
        auto p = FanqieFormat().parse(d.sourceText());
        QCOMPARE(p.notes.size(), 96);
        QVERIFY(d.presentation().systems.size() > 1);
        QCOMPARE(d.sourceText().count("Q:"), d.presentation().systems.size());
        QCOMPARE(d.sourceText().count("C1:"), d.presentation().systems.size() * 2);
        QCOMPARE(p.pendingLyrics.join(""), QStringLiteral("多出歌词"));
        QVERIFY(p.boundaries.size() >= 24);
        int syllables = 0;
        for (const auto &m : d.snapshot().measures)
            for (const auto &e : m.events)
                syllables += e.lyrics.size();
        QCOMPARE(syllables, 192);
    }
    void openSlurDoesNotStopWrapping()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText("Q: (" + QString("1 2 3 4 ").repeated(20) + "\n"));
        QVERIFY(d.inputState().value("hasErrors").toBool());
        QCOMPARE(d.sourceText().count("Q:"), d.presentation().systems.size());
        QVERIFY(d.sourceText().count('|') >= 20);
        QCOMPARE(d.snapshot().noteCount(), 80);
    }
    void focusEntryModesAndLyrics()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *editor = w.findChild<SourceEditor *>();
        editor->setFocus();
        editor->setPlainText(QStringLiteral("Q: 1 2 3 4 |\nC1: 春夏秋冬\nC1: 一二三四\n"));
        QTRY_COMPARE(w.document()->snapshot().noteCount(), 4);
        auto *canvas = w.canvas();
        auto clickNote = [&](int index)
        {
            auto e = w.document()->snapshot().measures.first().events[index];
            for (const auto &h : canvas->layoutResult().hits)
                if (h.id == e.notes.first().id)
                {
                    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                                      (h.rect.center() * canvas->zoom()).toPoint());
                    break;
                }
        };
        clickNote(1);
        QVERIFY(canvas->hasFocus());
        QCOMPARE(canvas->entryController()->mode(), EntryMode::Overwrite);
        QTest::keyClicks(canvas, "6");
        QCOMPARE(w.document()->snapshot().noteCount(), 4);
        QCOMPARE(w.document()->textSnapshot().notes[1].degree, 6);
        clickNote(1);
        QTest::keyClick(canvas, Qt::Key_Insert);
        QCOMPARE(canvas->entryController()->mode(), EntryMode::Insert);
        QCOMPARE(w.findChild<QComboBox *>("entryMode")->currentData().toInt(),
                 int(EntryMode::Insert));
        QTest::keyClicks(canvas, "5/");
        auto p = w.document()->textSnapshot();
        QVector<TextNote> primary;
        for (const auto &n : p.notes)
            if (!n.tieBack)
                primary << n;
        QCOMPARE(primary.size(), 5);
        QCOMPARE(p.notes[2].degree, 5);
        QCOMPARE(p.notes[2].duration, MusicalTime(1, 8));
        QVERIFY(p.notes[2].lyrics.isEmpty());
        QCOMPARE(p.notes[1].lyrics.value(0), QStringLiteral("夏"));
        QCOMPARE(p.notes[3].lyrics.value(0), QStringLiteral("秋"));
        QCOMPARE(p.notes[3].lyrics.value(1), QStringLiteral("三"));
        QTest::keyClick(canvas, Qt::Key_End);
        QCOMPARE(canvas->entryController()->mode(), EntryMode::Append);
        int count = w.document()->snapshot().noteCount();
        QTest::keyClicks(canvas, "7");
        QCOMPARE(w.document()->snapshot().noteCount(), count + 1);
        QTest::keyClick(canvas, Qt::Key_Left);
        QCOMPARE(canvas->entryController()->mode(), EntryMode::Overwrite);
        QTest::keyClick(canvas, Qt::Key_Right);
        QVERIFY(canvas->atAppendPosition());
        QCOMPARE(canvas->entryController()->mode(), EntryMode::Append);
        QTest::mouseClick(editor->viewport(), Qt::LeftButton);
        QVERIFY(editor->hasFocus());
        QCOMPARE(canvas->entryController()->surface(), InputSurface::Text);
        QVERIFY(!w.findChild<QAction *>("toggleInputMode"));
    }
    void backspaceCrossesBarAndWrappedRowsOnce()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        QVERIFY(w.document()->applyText("Q: " + QString("1 2 3 4 ").repeated(12) + "\n"));
        auto *c = w.canvas();
        c->setFocus();
        QTest::keyClick(c, Qt::Key_End);
        for (int count = 48; count > 0; --count)
        {
            QTest::keyClick(c, Qt::Key_Backspace);
            QCOMPARE(w.document()->snapshot().noteCount(), count - 1);
            QCOMPARE(w.document()->textSnapshot().notes.size(), count - 1);
            if (count > 1)
                QCOMPARE(w.document()->sourceText().count("Q:"),
                         w.document()->presentation().systems.size());
        }
        QTest::keyClick(c, Qt::Key_1);
        QCOMPARE(w.document()->snapshot().noteCount(), 1);
        w.document()->undo();
        QCOMPARE(w.document()->snapshot().noteCount(), 0);
        w.document()->undo();
        QCOMPARE(w.document()->snapshot().noteCount(), 1);
    }
    void unequalVoicesHaveOnlyCorrespondingRows()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText("Q1: " + QString("1 2 3 4 ").repeated(12) +
                            "\nC1: " + QStringLiteral("春夏秋冬").repeated(12) +
                            "\nQ2: 5 6 7 1 |\nC2: 高山流水\n"));
        auto p = d.textSnapshot();
        int rows = 0;
        for (const auto &sys : d.presentation().systems)
            rows += sys.tracks.size();
        QCOMPARE(d.sourceText().count(QRegularExpression("(?m)^Q[0-9]*:")), rows);
        for (const auto &n : p.notes)
            if (n.voice == 2)
                QVERIFY(!n.lyrics.value(0).isEmpty());
    }
    void draggingAndNudgingMetadata()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        QVERIFY(w.document()->applyText(QStringLiteral("B: 拖动标题\nQ: 1 2 3 4 |\n")));
        auto *c = w.canvas();
        c->setZoom(0.7);
        auto before = c->layoutResult().elements.value("@title:0")->bounds(c->layoutResult());
        QPoint origin = (before.center() * c->zoom()).toPoint();
        QTest::mousePress(c, Qt::LeftButton, Qt::NoModifier, origin);
        QCOMPARE(c->selection(), QStringList{"@title:0"});
        QTest::mouseMove(c, origin + QPoint(14, 7));
        QTest::mouseRelease(c, Qt::LeftButton, Qt::NoModifier, origin + QPoint(14, 7));
        auto moved = c->layoutResult().elements.value("@title:0")->bounds(c->layoutResult());
        QVERIFY(QLineF(moved.center(), before.center() + QPointF(20, 10)).length() < 0.1);
        QTest::keyClick(c, Qt::Key_Right);
        auto nudged = c->layoutResult().elements.value("@title:0")->bounds(c->layoutResult());
        QCOMPARE(nudged, moved.translated(1, 0));
        w.document()->undo();
        QCOMPARE(c->layoutResult().elements.value("@title:0")->bounds(c->layoutResult()), moved);
        w.document()->undo();
        QCOMPARE(c->layoutResult().elements.value("@title:0")->bounds(c->layoutResult()), before);
    }
    void compoundKeyboardSyntaxStillWorks()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *c = w.canvas();
        c->setFocus();
        QTest::keyClick(c, Qt::Key_Q);
        QTest::keyClicks(c, "(y1/2/3/)<4&mf5!");
        auto parsed = w.document()->textSnapshot();
        QVERIFY2(!parsed.hasErrors(),
                 qPrintable(w.document()->sourceText() + "\n" + parsed.messages().join('\n')));
        QCOMPARE(parsed.notes.size(), 5);
        QCOMPARE(parsed.notes[0].duration, MusicalTime(1, 12));
        QCOMPARE(parsed.notes[0].tuplet, 3);
        QCOMPARE(parsed.notes[2].duration, MusicalTime(1, 12));
        QCOMPARE(parsed.notes[3].dynamics, QStringList{"mf"});
        QCOMPARE(parsed.spans.size(), 2);
    }
    void graceSuffixAfterAutomaticBar()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *c = w.canvas();
        c->setFocus();
        QTest::keyClick(c, Qt::Key_Q);
        QTest::keyClicks(c, "1234[h23]");
        auto p = w.document()->textSnapshot();
        QVERIFY2(!p.hasErrors(),
                 qPrintable(w.document()->sourceText() + "\n" + p.messages().join('\n')));
        QCOMPARE(p.notes.size(), 4);
        QCOMPARE(p.notes.last().graces.size(), 2);
        QVERIFY(p.notes.last().graces.first().graceAfter);
    }
    void middleGraceDraftPreservesFollowingLyrics()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        QVERIFY(
            w.document()->applyText(QStringLiteral("Q: 1 2 3 4\nC1: 春夏秋冬\nC1: 一二三四\n")));
        auto *c = w.canvas();
        auto note = w.document()->snapshot().measures[0].events[1].notes[0].id;
        for (const auto &hit : c->layoutResult().hits)
            if (hit.id == note)
            {
                QTest::mouseClick(c, Qt::LeftButton, Qt::NoModifier,
                                  (hit.rect.center() * c->zoom()).toPoint());
                break;
            }
        QTest::keyClicks(c, "[h2");
        QVERIFY(w.document()->inputState().contains("draftSource"));
        QCOMPARE(w.document()->snapshot().noteCount(), 4);
        QTest::keyClicks(c, "3]");
        auto p = w.document()->textSnapshot();
        QVERIFY2(!p.hasErrors(), qPrintable(p.messages().join('\n')));
        QCOMPARE(p.notes.size(), 4);
        QCOMPARE(p.notes[1].graces.size(), 2);
        QCOMPARE(p.notes[2].lyrics.value(0), QStringLiteral("秋"));
        QCOMPARE(p.notes[3].lyrics.value(1), QStringLiteral("四"));
    }
    void authoredRepeatDoesNotTrapBackspace()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        QVERIFY(w.document()->applyText("Q: |: 1 2 3 4 :|\n"));
        auto *c = w.canvas();
        c->setFocus();
        QTest::keyClick(c, Qt::Key_End);
        for (int n = 4; n > 0; --n)
        {
            QTest::keyClick(c, Qt::Key_Backspace);
            QCOMPARE(w.document()->snapshot().noteCount(), n - 1);
            QVERIFY(w.document()->sourceText().contains(":|"));
        }
    }
    void insertionBeyondFirstRowKeepsAllVerses()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        QVERIFY(w.document()->applyText("Q: " + QString("1 2 3 4 ").repeated(24) +
                                        "\nC1: " + QStringLiteral("春夏秋冬").repeated(24) +
                                        "\nC1: " + QStringLiteral("一二三四").repeated(24) + "\n"));
        auto *c = w.canvas();
        const auto p = w.document()->textSnapshot();
        for (const auto &m : c->score().measures)
            for (const auto &e : m.events)
                if (e.position == p.notes[70].position)
                {
                    c->selectId(e.notes.first().id);
                    break;
                }
        c->setFocus();
        c->entryController()->selectTarget(false);
        QTest::keyClick(c, Qt::Key_Insert);
        QTest::keyClicks(c, "5/");
        QString first, second;
        for (const auto &n : w.document()->textSnapshot().notes)
        {
            first += n.lyrics.value(0);
            second += n.lyrics.value(1);
        }
        QCOMPARE(first, QStringLiteral("春夏秋冬").repeated(24));
        QCOMPARE(second, QStringLiteral("一二三四").repeated(24));
        QCOMPARE(w.document()->sourceText().count("Q:"),
                 w.document()->presentation().systems.size());
        int words = 0;
        for (const auto &m : c->score().measures)
            for (const auto &e : m.events)
                words += e.lyrics.size();
        QCOMPARE(words, 192);
    }
    void compositeElementsMoveTogether()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(
            QStringLiteral("B: 标题\nZ: 作者\nD: D\nP: 4/4\nQ: (1 2 3 4) |\nC: 春夏秋冬\n")));
        const auto &before = d.presentation();
        QVERIFY(before.elements.contains("@time:0"));
        QVector<QRectF> old;
        for (const auto &item : before.items)
            if (item.elementId == "@time:0")
                old << item.bounds;
        QCOMPARE(old.size(), 3);
        QVERIFY(d.moveElement("@time:0", {8, 9}));
        int index = 0;
        for (const auto &item : d.presentation().items)
            if (item.elementId == "@time:0")
                QCOMPARE(item.bounds, old[index++].translated(8, 9));
        for (const auto &item : d.presentation().items)
        {
            QVERIFY(!item.elementId.isEmpty());
            QVERIFY(d.presentation().elements.contains(item.elementId));
        }
    }
    void metadataMovementIsAtomicAndPersistent()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(
            QStringLiteral("B: 标题\nB: 副标题\nZ: 作者\nQ: 1 2 3 4 |\nC: 春夏秋冬\n")));
        auto oldSource = d.sourceText();
        auto original = d.presentation().elements.value("@title:0")->bounds(d.presentation());
        auto note = d.presentation().hits.first().rect;
        QVERIFY(dynamic_cast<const TextVisualElement *>(
            d.presentation().elements.value("@title:0").get()));
        QVERIFY(d.moveElement("@title:0", {12, -7}));
        QCOMPARE(d.presentation().elements.value("@title:0")->bounds(d.presentation()),
                 original.translated(12, -7));
        QCOMPARE(d.presentation().hits.first().rect, note);
        QCOMPARE(d.sourceText(), oldSource);
        d.undo();
        QCOMPARE(d.presentation().elements.value("@title:0")->bounds(d.presentation()), original);
        d.redo();
        QTemporaryDir dir;
        QVERIFY(d.save(dir.filePath("offsets.jianpu")));
        ScoreDocument reopened;
        QVERIFY(reopened.open(dir.filePath("offsets.jianpu")));
        QCOMPARE(
            reopened.presentation().elements.value("@title:0")->bounds(reopened.presentation()),
            original.translated(12, -7));
    }
    void nativePickupProjectionAndOffsetsSurviveEdits()
    {
        ScoreDocument d;
        QVERIFY(d.open(RHYTHM_SCORE));
        auto source = d.sourceText();
        QVERIFY(!source.isEmpty());
        QVERIFY(source.contains("Q2: 8/ |"));
        auto p = d.textSnapshot();
        QVERIFY2(p.canProject(), qPrintable(p.messages().join('\n')));
        int rows = 0;
        for (const auto &sys : d.presentation().systems)
            rows += sys.tracks.size();
        QCOMPARE(source.count(QRegularExpression("(?m)^Q[0-9]*:")), rows);
        QString dynamic, note;
        for (const auto &mark : d.snapshot().marks)
            if (mark.kind == "dynamic" && mark.text == "p")
                dynamic = mark.id;
        for (const auto &m : d.snapshot().measures)
            for (const auto &e : m.events)
                if (!e.grace && !e.notes.isEmpty())
                    note = e.notes.first().id;
        QVERIFY(!dynamic.isEmpty());
        const auto key = d.presentation().elements.value(dynamic)->identity().key;
        QVERIFY(d.moveElement(dynamic, {6, 3}));
        QVERIFY(d.edit({EditRequest::Kind::DiatonicUp, {note}}));
        QCOMPARE(d.presentation().elements.value(dynamic)->identity().key, key);
        auto plainView = d.viewSettings();
        plainView.remove("elementOffsets");
        const auto plain = JianpuLayout().layout(d.snapshot(), plainView);
        QCOMPARE(d.presentation().elements.value(dynamic)->bounds(d.presentation()),
                 plain.elements.value(dynamic)->bounds(plain).translated(6, 3));
    }
    void addedMarksRemainAnchored()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText("Q: 1 2 3 4 | 5 6 7 1 |\n"));
        auto score = d.snapshot();
        auto first = score.measures[0].events[0].notes[0].id;
        auto third = score.measures[0].events[2].notes[0].id;
        QVERIFY(d.edit({EditRequest::Kind::AddMark, {first}, 0, 0, "dynamic:mf"}));
        QString mf;
        for (const auto &mark : d.snapshot().marks)
            if (mark.kind == "dynamic" && mark.text == "mf")
                mf = mark.id;
        QVERIFY(!mf.isEmpty());
        QPointF old;
        for (const auto &item : d.presentation().items)
            if (item.id == mf)
                old = item.bounds.center();
        QVERIFY(d.edit({EditRequest::Kind::AddMark, {third}, 0, 0, "dynamic:pp"}));
        QPointF now;
        for (const auto &item : d.presentation().items)
            if (item.id == mf)
                now = item.bounds.center();
        QCOMPARE(now, old);
        QPointF head;
        for (const auto &hit : d.presentation().hits)
            if (hit.id == first)
                head = hit.rect.center();
        QVERIFY(qAbs(head.x() - now.x()) < 0.01);
        QVERIFY(d.moveElement(mf, {5, 3}));
        QVERIFY(d.applyText(d.sourceText() + "# preserve offsets\n"));
        for (const auto &mark : d.snapshot().marks)
            if (mark.kind == "dynamic" && mark.text == "mf")
                for (const auto &item : d.presentation().items)
                    if (item.id == mark.id)
                        QCOMPARE(item.bounds.center(), old + QPointF(5, 3));
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    EngineRuntime runtime;
    ProjectionTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_projection.moc"
