#include "scorewidget.h"
#include <QImage>
#include <QSignalSpy>
#include <QTest>

class ScoreWidgetTest : public QObject
{
    Q_OBJECT
private slots:
    void selectEditUndoRedo()
    {
        ScoreWidget widget;
        widget.show();
        QSignalSpy spy(&widget, &ScoreWidget::selectionChanged);
        QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, widget.noteRect(0).center().toPoint());
        QCOMPARE(widget.selectedIndex(), 0);
        QCOMPARE(spy.count(), 1);
        QTest::keyClick(&widget, Qt::Key_Up);
        QCOMPARE(widget.pitchAt(0), 1);
        QCOMPARE(widget.pitchAt(1), 1);
        QTest::keySequence(&widget, QKeySequence::Undo);
        QCOMPARE(widget.pitchAt(0), 0);
        QTest::keySequence(&widget, QKeySequence::Redo);
        QCOMPARE(widget.pitchAt(0), 1);
        QTest::keyClick(&widget, Qt::Key_Down);
        QCOMPARE(widget.pitchAt(0), 0);
    }

    void historyTracksNotesAcrossSelection()
    {
        ScoreWidget widget;
        widget.show();
        QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, widget.noteRect(0).center().toPoint());
        QTest::keyClick(&widget, Qt::Key_Up);
        QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, widget.noteRect(4).center().toPoint());
        QTest::keyClick(&widget, Qt::Key_Down);
        QCOMPARE(widget.pitchAt(4), 6);
        widget.undoStack()->undo();
        QCOMPARE(widget.pitchAt(4), 7);
        widget.undoStack()->undo();
        QCOMPARE(widget.pitchAt(0), 0);
        widget.undoStack()->redo();
        QCOMPARE(widget.pitchAt(0), 1);
        QTest::keyClick(&widget, Qt::Key_Down);
        QVERIFY(!widget.undoStack()->canRedo());
    }

    void blankClickAndPitchLimits()
    {
        ScoreWidget widget;
        widget.show();
        QTest::keyClick(&widget, Qt::Key_Up);
        QCOMPARE(widget.undoStack()->count(), 0);
        QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, widget.noteRect(0).center().toPoint());
        for (int i = 0; i < 50; ++i)
            QTest::keyClick(&widget, Qt::Key_Up);
        QCOMPARE(widget.pitchAt(0), ScoreWidget::MaximumPitch);
        QCOMPARE(widget.undoStack()->count(), ScoreWidget::MaximumPitch);
        for (int i = 0; i < 60; ++i)
            QTest::keyClick(&widget, Qt::Key_Down);
        QCOMPARE(widget.pitchAt(0), ScoreWidget::MinimumPitch);
        QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
        QCOMPARE(widget.selectedIndex(), -1);
        const int count = widget.undoStack()->count();
        QTest::keyClick(&widget, Qt::Key_Up);
        QCOMPARE(widget.undoStack()->count(), count);
    }

    void octaveBoundariesAndRendering()
    {
        QCOMPARE(ScoreWidget::degreeText(-1), QStringLiteral("7"));
        QCOMPARE(ScoreWidget::octaveDots(-1), -1);
        QCOMPARE(ScoreWidget::degreeText(7), QStringLiteral("1"));
        QCOMPARE(ScoreWidget::octaveDots(7), 1);
        ScoreWidget widget;
        widget.resize(1080, 380);
        const auto image = widget.grab().toImage();
        QVERIFY(!image.isNull());
        QCOMPARE(image.size(), widget.size() * widget.devicePixelRatioF());
        QVERIFY(image.pixelColor(40, 130) != image.pixelColor(0, 0));
    }
};

QTEST_MAIN(ScoreWidgetTest)
#include "tst_scorewidget.moc"
