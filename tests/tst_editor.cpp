// SPDX-License-Identifier: GPL-3.0-only
#include "core/runtime.h"
#include "core/scoredocument.h"
#include "layout/jianpulayout.h"
#include "ui/mainwindow.h"
#include "ui/scorecanvas.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QInputMethodEvent>
#include <QLineEdit>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using namespace jianpu;
class EditorTest : public QObject
{
    Q_OBJECT
  private slots:
    void relativeAccidentalSpelling()
    {
        NoteData f;
        f.step = 3;
        f.midi = 65;
        QCOMPARE(JianpuLayout::noteText(f, 1), QStringLiteral("♭7"));
        NoteData c;
        QCOMPARE(JianpuLayout::noteText(c, 0, true), QStringLiteral("♮1"));
    }
    void selectionZoomAndEditing()
    {
        ScoreDocument d;
        QVERIFY(d.open(EXAMPLE_SCORE));
        ScoreCanvas canvas(&d);
        canvas.show();
        auto first = canvas.layoutResult().hits.first();
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, first.rect.center().toPoint());
        QCOMPARE(canvas.selection(), QStringList{first.id});
        QTest::keyClick(&canvas, Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(d.snapshot().measures[0].events[0].notes[0].midi, 62);
        QTest::keySequence(&canvas, QKeySequence::Undo);
        QCOMPARE(d.snapshot().measures[0].events[0].notes[0].midi, 60);
        canvas.setZoom(0.7);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier,
                          (first.rect.center() * canvas.zoom()).toPoint());
        QCOMPARE(canvas.selection(), QStringList{first.id});
        const auto &layout = canvas.layoutResult();
        QCOMPARE(layout.hitTest(first.rect.center())->id, first.id);
        QVector<const HitRegion *> voice;
        for (const auto &h : layout.hits)
            if (h.track == 0 && h.note)
                voice << &h;
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::ShiftModifier,
                          (voice[3]->rect.center() * canvas.zoom()).toPoint());
        QCOMPARE(canvas.selection().size(), 4);
        QVERIFY(canvas.selection().contains(voice[3]->id));
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::ControlModifier,
                          (voice[0]->rect.center() * canvas.zoom()).toPoint());
        QCOMPARE(canvas.selection().size(), 3);
    }
    void restReplacementKeepsSelection()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        ScoreCanvas canvas(&d);
        canvas.show();
        auto rest = canvas.layoutResult().hits.first();
        canvas.selectId(rest.id);
        QTest::keyClick(&canvas, Qt::Key_1);
        QCOMPARE(d.snapshot().noteCount(), 1);
        QVERIFY(!canvas.selection().isEmpty());
        QTest::keyClick(&canvas, Qt::Key_Left);
        QTest::keyClick(&canvas, Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(d.snapshot().measures[0].events[0].notes[0].midi, 62);
    }
    void chineseLyricsInput()
    {
        MainWindow window;
        QVERIFY(window.openPath(EXAMPLE_SCORE, false));
        window.show();
        auto *canvas = window.canvas();
        canvas->selectId(canvas->layoutResult().hits.first().id);
        auto *editor = window.findChild<QLineEdit *>("lyricsEditor");
        QVERIFY(editor);
        editor->clear();
        editor->setFocus();
        QInputMethodEvent ime;
        ime.setCommitString(QStringLiteral("山水之间"));
        QApplication::sendEvent(editor, &ime);
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(window.document()->snapshot().measures[0].events[0].lyrics[0].text,
                 QStringLiteral("山水之间"));
        window.document()->undo();
        QCOMPARE(window.document()->snapshot().measures[0].events[0].lyrics[0].text,
                 QStringLiteral("春"));
    }
    void layoutAlignmentAndCollisions()
    {
        ScoreDocument d;
        QVERIFY(d.open(EXAMPLE_SCORE));
        auto layout = JianpuLayout().layout(d.snapshot());
        QVERIFY(layout.pages.size() > 1);
        QMap<qint64, qreal> x;
        for (const auto &hit : layout.hits)
        {
            if (!hit.note)
                continue;
            if (x.contains(hit.tick))
                QVERIFY(qAbs(x[hit.tick] - hit.rect.center().x()) < 0.01);
            x[hit.tick] = hit.rect.center().x();
        }
        for (const auto &dot : layout.items)
            if (dot.kind == LayoutItem::Kind::Dot)
            {
                for (const auto &digit : layout.items)
                    if (digit.kind == LayoutItem::Kind::Text && digit.fontFamily == "DejaVu Sans" &&
                        digit.track >= 0)
                        QVERIFY2(!dot.bounds.intersects(digit.bounds),
                                 "Octave dot overlaps a chord digit");
            }
        auto image = QImage(layout.size.toSize(), QImage::Format_ARGB32_Premultiplied);
        QVERIFY(!image.isNull());
    }
    void longAndDenseScores()
    {
        ScoreSnapshot s;
        s.title = QStringLiteral("密集节奏");
        s.voices = {{0, "旋律", false}};
        for (int i = 0; i < 32; ++i)
        {
            MeasureData m;
            m.number = i + 1;
            m.tick = i * 1920;
            for (int j = 0; j < 32; ++j)
            {
                EventData e;
                e.id = QString("e%1-%2").arg(i).arg(j);
                e.tick = m.tick + j * 60;
                e.duration = 60;
                e.denominator = 32;
                NoteData n;
                n.id = e.id + "n";
                e.notes = {n};
                m.events << e;
            }
            s.measures << m;
        }
        auto result = JianpuLayout().layout(s);
        QVERIFY(result.pages.size() > 1);
        QCOMPARE(qRound(result.pages.first().width()), 794);
        QMap<qint64, QRectF> noteRects;
        for (const auto &hit : result.hits)
            if (hit.note)
            {
                bool inPage = false;
                for (const auto &page : result.pages)
                    if (page.contains(hit.rect))
                        inPage = true;
                QVERIFY(inPage);
                if (noteRects.contains(hit.tick - 60))
                    QVERIFY(!noteRects[hit.tick - 60].intersects(hit.rect));
                noteRects[hit.tick] = hit.rect;
            }
    }
    void expressiveAnchorsAndGraceSpacing()
    {
        ScoreDocument d;
        QVERIFY(d.open(QFileInfo(EXAMPLE_SCORE).dir().filePath("rhythm.musicxml")));
        auto score = d.snapshot();
        bool group = false;
        for (const auto &mark : score.marks)
            if (mark.kind == "tuplet")
            {
                QCOMPARE(mark.tick, 1680);
                QCOMPARE(mark.endTick, 2160);
                group = true;
            }
        QVERIFY(group);
        auto layout = JianpuLayout().layout(score);
        for (const auto &mark : score.marks)
            if (mark.kind == "tuplet")
            {
                QRectF bracket;
                for (const auto &item : layout.items)
                    if (item.id == mark.id && item.kind == LayoutItem::Kind::Curve)
                        bracket = item.bounds;
                for (const auto &measure : score.measures)
                    for (const auto &event : measure.events)
                        if (event.tuplet == 3 && event.tick >= mark.tick &&
                            event.tick < mark.endTick)
                            for (const auto &note : event.notes)
                                for (const auto &hit : layout.hits)
                                    if (hit.id == note.id)
                                        QVERIFY(hit.rect.center().x() >= bracket.left() &&
                                                hit.rect.center().x() <= bracket.right());
            }
        QMap<QString, QRectF> boxes;
        for (const auto &hit : layout.hits)
            boxes[hit.id] = hit.rect;
        for (const auto &measure : score.measures)
            for (const auto &grace : measure.events)
                if (grace.grace)
                    for (const auto &main : measure.events)
                        if (!main.grace && main.track == grace.track && main.tick == grace.tick)
                            for (const auto &a : grace.notes)
                                for (const auto &b : main.notes)
                                    QVERIFY(!boxes[a.id].intersects(boxes[b.id]));
        bool continuedKey = false;
        for (const auto &item : layout.items)
            if (item.text.contains("6 = G"))
                continuedKey = true;
        QVERIFY(continuedKey);
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("JianpuEditorTests");
    QCoreApplication::setOrganizationName("JianpuLab");
    QStandardPaths::setTestModeEnabled(true);
    EngineRuntime runtime;
    EditorTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_editor.moc"
