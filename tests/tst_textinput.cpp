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
#include <QDockWidget>
#include <QFile>
#include <QFontMetricsF>
#include <QImage>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
using namespace jianpu;
class TextInputTest : public QObject
{
    Q_OBJECT
  private slots:
    void repeatPitchInsertionKeepsBothVerses()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *editor = w.findChild<SourceEditor *>();
        editor->setPlainText(QStringLiteral("Q: 1 1 1 1 |\nC1: 春夏秋冬\nC1: 一二三四\n"));
        QTRY_COMPARE(w.document()->snapshot().noteCount(), 4);
        auto parsed = FanqieFormat().parse(w.document()->sourceText());
        auto cursor = editor->textCursor();
        cursor.setPosition(parsed.notes[1].offset);
        editor->setTextCursor(cursor);
        editor->insertPlainText("1 ");
        QTRY_COMPARE(w.document()->snapshot().noteCount(), 5);
        auto check = [&](ScoreDocument *d)
        {
            QCOMPARE(d->sourceText().count("C1:"), 2);
            auto p = FanqieFormat().parse(d->sourceText());
            QCOMPARE(p.notes.size(), 5);
            QCOMPARE(p.notes[0].lyrics.value(0), QStringLiteral("春"));
            QVERIFY(p.notes[1].lyrics.isEmpty());
            QCOMPARE(p.notes[2].lyrics.value(0), QStringLiteral("夏"));
            QCOMPARE(p.notes[4].lyrics.value(0), QStringLiteral("冬"));
            QCOMPARE(p.notes[2].lyrics.value(1), QStringLiteral("二"));
            QCOMPARE(p.notes[4].lyrics.value(1), QStringLiteral("四"));
            QVERIFY(!d->inputState().value("hasErrors").toBool());
        };
        check(w.document());
        w.document()->undo();
        QCOMPARE(w.document()->snapshot().noteCount(), 4);
        w.document()->redo();
        check(w.document());
        QTemporaryDir dir;
        QVERIFY(w.document()->save(dir.filePath("repeated.jianpu")));
        ScoreDocument reopened;
        QVERIFY(reopened.open(dir.filePath("repeated.jianpu")));
        check(&reopened);
    }
    void insertionAcrossAutoBarsKeepsLyricOwners()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *editor = w.findChild<SourceEditor *>();
        editor->setPlainText(QStringLiteral("Q: 1 1 1 1 1 1 1 1\nC: 春夏秋冬山水天地\n"));
        QTRY_COMPARE(w.document()->snapshot().noteCount(), 8);
        auto parsed = FanqieFormat().parse(w.document()->sourceText());
        auto cursor = editor->textCursor();
        cursor.setPosition(parsed.notes[3].offset);
        editor->setTextCursor(cursor);
        editor->insertPlainText("1/ ");
        QTRY_VERIFY(w.document()->snapshot().noteCount() > 8);
        parsed = FanqieFormat().parse(w.document()->sourceText());
        QVector<TextNote> primary;
        for (const auto &n : parsed.notes)
            if (!n.tieBack && n.degree != 0 && n.degree != 8)
                primary << n;
        QCOMPARE(primary.size(), 9);
        QVERIFY(primary[3].lyrics.isEmpty());
        QCOMPARE(primary[4].lyrics.value(0), QStringLiteral("冬"));
        QString words;
        for (const auto &n : primary)
            words += n.lyrics.value(0);
        QCOMPARE(words, QStringLiteral("春夏秋冬山水天地"));
        QVERIFY2(!w.document()->inputState().value("hasErrors").toBool(),
                 qPrintable(w.document()->inputMessages().join('\n')));
    }
    void clickedMiddleOverwritesAndEndAppends()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *editor = w.findChild<SourceEditor *>();
        editor->setPlainText(QStringLiteral("Q: 1 2 3 4 | 5 6 7 1 |\nC: 春夏秋冬山水天地\n"));
        QTRY_COMPARE(w.document()->snapshot().noteCount(), 8);
        w.canvas()->setZoom(0.65);
        // Start in append mode, then click the middle: the click must override that mode.
        editor->setFocus();
        w.canvas()->setFocus();
        QTest::keyClick(w.canvas(), Qt::Key_End);
        const auto s = w.document()->snapshot();
        auto id = s.measures[0].events[1].notes[0].id;
        QPointF point;
        for (const auto &hit : w.canvas()->layoutResult().hits)
            if (hit.id == id)
                point = hit.rect.center() * w.canvas()->zoom();
        QTest::mouseClick(w.canvas(), Qt::LeftButton, Qt::NoModifier, point.toPoint());
        QTest::keyClicks(w.canvas(), "67");
        auto after = w.document()->snapshot();
        QCOMPARE(after.noteCount(), 8);
        QCOMPARE(after.measures[0].events[1].notes[0].midi, 69);
        QCOMPARE(after.measures[0].events[2].notes[0].midi, 71);
        QCOMPARE(after.measures[0].events[1].lyrics[0].text, QStringLiteral("夏"));
        QCOMPARE(after.measures[0].events[2].lyrics[0].text, QStringLiteral("秋"));
        QTest::keyClick(w.canvas(), Qt::Key_Slash);
        after = w.document()->snapshot();
        QCOMPARE(after.measures[0].events[2].denominator, 8);
        bool winter = false;
        for (const auto &e : after.measures[0].events)
            winter |=
                e.tick == 1440 && !e.lyrics.isEmpty() && e.lyrics[0].text == QStringLiteral("冬");
        QVERIFY(winter);
        QTest::keyClick(w.canvas(), Qt::Key_End);
        QTest::keyClick(w.canvas(), Qt::Key_5);
        QTRY_COMPARE(w.document()->snapshot().noteCount(), 9);
        QVERIFY(!w.document()->inputState().value("hasErrors").toBool());
    }
    void overwriteRestHiddenRestAndRhythm()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *editor = w.findChild<SourceEditor *>();
        editor->setPlainText("Q: 1 2 3 4 |\n");
        QTRY_COMPARE(w.document()->snapshot().noteCount(), 4);
        auto hit = w.canvas()->layoutResult().hits.first();
        QTest::mouseClick(w.canvas(), Qt::LeftButton, Qt::NoModifier,
                          (hit.rect.center() * w.canvas()->zoom()).toPoint());
        QTest::keyClick(w.canvas(), Qt::Key_N);
        QCOMPARE(w.canvas()->cursor().shape(), Qt::ArrowCursor);
        QTest::keyClicks(w.canvas(), "089");
        auto s = w.document()->snapshot();
        QCOMPARE(s.measures[0].events.size(), 4);
        QVERIFY(s.measures[0].events[0].rest && s.measures[0].events[0].visible);
        QVERIFY(s.measures[0].events[1].rest && !s.measures[0].events[1].visible);
        QVERIFY(s.measures[0].events[2].rhythm);
        for (int i = 0; i < 3; ++i)
            w.document()->undo();
        QCOMPARE(w.document()->snapshot().noteCount(), 4);
    }
    void overwriteSuffixUsesOriginalNoteAndNextPosition()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *editor = w.findChild<SourceEditor *>();
        editor->setPlainText("Q: 1 2 3 4 |\n");
        QTRY_COMPARE(w.document()->snapshot().noteCount(), 4);
        auto s = w.document()->snapshot();
        auto id = s.measures[0].events[1].notes[0].id;
        QPointF point;
        for (const auto &hit : w.canvas()->layoutResult().hits)
            if (hit.id == id)
                point = hit.rect.center() * w.canvas()->zoom();
        QTest::mouseClick(w.canvas(), Qt::LeftButton, Qt::NoModifier, point.toPoint());
        QTest::keyClicks(w.canvas(), "6-7");
        s = w.document()->snapshot();
        QCOMPARE(s.noteCount(), 3);
        QCOMPARE(s.measures[0].events[0].notes[0].midi, 60);
        QCOMPARE(s.measures[0].events[1].denominator, 2);
        QCOMPARE(s.measures[0].events.last().tick, qint64(1440));
        QCOMPARE(s.measures[0].events.last().notes[0].midi, 71);

        editor->setPlainText("Q: 1 2 3 4 |\n");
        QTRY_COMPARE(w.document()->snapshot().noteCount(), 4);
        s = w.document()->snapshot();
        id = s.measures[0].events[0].notes[0].id;
        QVERIFY(w.document()->edit({EditRequest::Kind::AddChordNote, {id}, 67}));
        s = w.document()->snapshot();
        for (const auto &note : s.measures[0].events[0].notes)
            if (note.midi == 67)
                id = note.id;
        for (const auto &hit : w.canvas()->layoutResult().hits)
            if (hit.id == id)
                point = hit.rect.center() * w.canvas()->zoom();
        QTest::mouseClick(w.canvas(), Qt::LeftButton, Qt::NoModifier, point.toPoint());
        QTest::keyClicks(w.canvas(), "6'");
        s = w.document()->snapshot();
        QSet<int> pitches;
        for (const auto &note : s.measures[0].events[0].notes)
            pitches.insert(note.midi);
        QCOMPARE(pitches, (QSet<int>{60, 81}));
    }
    void keyboardLyricsSkipTiedContinuations()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *editor = w.findChild<SourceEditor *>();
        editor->setPlainText("Q: 1 2--- |\nQ: 3 4 |\n");
        QTRY_COMPARE(w.document()->snapshot().noteCount(), 5);
        w.canvas()->setFocus();
        QTest::keyClick(w.canvas(), Qt::Key_C);
        auto *input = w.canvas();
        QVERIFY(w.canvas()->lyricSelected());
        QInputMethodEvent ime;
        ime.setCommitString(QStringLiteral("春夏秋冬"));
        QApplication::sendEvent(input, &ime);
        QString words;
        for (const auto &measure : w.document()->snapshot().measures)
            for (const auto &event : measure.events)
                if (!event.lyrics.isEmpty())
                    words += event.lyrics[0].text;
        QCOMPARE(words, QStringLiteral("春夏秋冬"));
        QVERIFY2(!w.document()->inputState().value("hasErrors").toBool(),
                 qPrintable(w.document()->inputMessages().join('\n')));
    }
    void overwriteTiedPitchKeepsLyricsAndSkipsContinuation()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *editor = w.findChild<SourceEditor *>();
        editor->setPlainText(QStringLiteral("Q: 1 2--- 3\nC1: 春夏秋\nC1: 一二三\n"));
        QTRY_COMPARE(w.document()->snapshot().noteCount(), 4);
        auto s = w.document()->snapshot();
        auto id = s.measures[0].events[1].notes[0].id;
        QPointF point;
        for (const auto &hit : w.canvas()->layoutResult().hits)
            if (hit.id == id)
                point = hit.rect.center() * w.canvas()->zoom();
        QTest::mouseClick(w.canvas(), Qt::LeftButton, Qt::NoModifier, point.toPoint());
        QTest::keyClicks(w.canvas(), "67");
        s = w.document()->snapshot();
        QString words;
        int tied = 0;
        for (const auto &measure : s.measures)
            for (const auto &event : measure.events)
            {
                if (!event.lyrics.isEmpty())
                    words += event.lyrics[0].text;
                for (const auto &note : event.notes)
                    if (note.tieStart || note.tieEnd)
                    {
                        QCOMPARE(note.midi, 69);
                        ++tied;
                    }
            }
        QCOMPARE(tied, 2);
        QCOMPARE(words, QStringLiteral("春夏秋"));
        auto p = FanqieFormat().parse(w.document()->sourceText());
        QVERIFY2(!p.hasErrors(), qPrintable(p.messages().join('\n')));
        QVERIFY(!w.document()->sourceText().contains("^^"));
        QCOMPARE(p.notes.last().degree, 7);
        QVERIFY(!w.document()->inputState().value("hasErrors").toBool());
    }
    void repeatedC1DisplaysVerseContext()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *editor = w.findChild<SourceEditor *>();
        editor->setPlainText(QStringLiteral("Q: 1 2 3 4 |\nC1: 春夏秋冬\nC1: 一二三四\n"));
        QTRY_COMPARE(w.document()->snapshot().noteCount(), 4);
        auto source = editor->toPlainText();
        auto cursor = editor->textCursor();
        cursor.setPosition(source.lastIndexOf("C1:") + 4);
        editor->setTextCursor(cursor);
        auto *context = w.findChild<QLabel *>("lyricContext");
        QVERIFY(context);
        QVERIFY(context->text().contains(QStringLiteral("第 1 声部")));
        QVERIFY(context->text().contains(QStringLiteral("第 2 段")));
        QCOMPARE(w.document()->sourceText().count("C1:"), 2);
    }
    void durationOverwriteKeepsOtherVoiceLyrics()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *editor = w.findChild<SourceEditor *>();
        editor->setPlainText(QStringLiteral("Q1: 1 2 3 4 |\nC1: 春夏秋冬\n"
                                            "Q2: 5 6 7 1 |\nC2: 山水天地\n"));
        QTRY_COMPARE(w.document()->snapshot().noteCount(), 8);
        auto hit = w.canvas()->layoutResult().hits.first();
        QTest::mouseClick(w.canvas(), Qt::LeftButton, Qt::NoModifier,
                          (hit.rect.center() * w.canvas()->zoom()).toPoint());
        QTest::keyClicks(w.canvas(), "5/");
        auto p = FanqieFormat().parse(w.document()->sourceText());
        QString words;
        for (const auto &n : p.notes)
            if (n.voice == 2)
                words += n.lyrics.value(0);
        QCOMPARE(words, QStringLiteral("山水天地"));
        QVERIFY(w.document()->applyText(w.document()->sourceText() + "# round trip\n"));
        words.clear();
        for (const auto &m : w.document()->snapshot().measures)
            for (const auto &e : m.events)
                if (e.track == 4 && !e.lyrics.isEmpty())
                    words += e.lyrics[0].text;
        QCOMPARE(words, QStringLiteral("山水天地"));
    }
    void barlinesAlignWithInkAndContentIsCentered()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(
            d.applyText(QStringLiteral("Q: 1' 2 3, 4 | 5 6' 7 1, |\nC: 春夏秋冬，山水天地。\n")));
        for (const auto &preset : {"classic", "modern", "children"})
        {
            auto layout =
                JianpuLayout().layout(d.snapshot(), {{"layout", LayoutSettings::defaults(preset)}});
            qreal left = layout.systems.first().bounds.left();
            for (const auto &measure : d.snapshot().measures)
            {
                QRectF bar, ink;
                QSet<QString> ids;
                for (const auto &e : measure.events)
                    for (const auto &n : e.notes)
                        ids.insert(n.id);
                for (const auto &item : layout.items)
                {
                    if (item.id == measure.id && item.kind == LayoutItem::Kind::Line &&
                        item.bounds.height() > 0 && qAbs(item.bounds.width()) < 0.01)
                        bar = item.bounds;
                    if (!ids.contains(item.id) || item.kind != LayoutItem::Kind::Text)
                        continue;
                    QFont font(item.fontFamily);
                    font.setPixelSize(qRound(item.fontSize));
                    font.setBold(item.bold);
                    QFontMetricsF fm(font);
                    auto glyph = fm.tightBoundingRect(item.text);
                    qreal baseline =
                        item.bounds.top() + (item.bounds.height() - fm.height()) / 2 + fm.ascent();
                    glyph.translate(item.bounds.center().x() - fm.horizontalAdvance(item.text) / 2,
                                    baseline);
                    ink = ink.isEmpty() ? glyph : ink.united(glyph);
                }
                QVERIFY(!bar.isEmpty() || bar.height() > 0);
                QVERIFY(!ink.isEmpty());
                QVERIFY2(qAbs(bar.center().y() - ink.center().y()) < 1.1, preset);
                QVERIFY2(qAbs((ink.left() - left) - (bar.left() - ink.right())) < 1.1, preset);
                left = bar.left();
            }
        }
    }
    void panelsCanBeReopenedFromView()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *menu = w.findChild<QMenu *>("viewMenu");
        QVERIFY(menu);
        for (const auto &name : {"voicesDock", "paletteDock", "sourceDock", "propertiesDock",
                                 "layoutDock", "reportDock"})
        {
            auto *dock = w.findChild<QDockWidget *>(name);
            QVERIFY(dock);
            auto *toggle = dock->toggleViewAction();
            QVERIFY(menu->actions().contains(toggle));
            dock->close();
            QVERIFY(dock->isHidden());
            QVERIFY(!toggle->isChecked());
            toggle->trigger();
            QVERIFY(!dock->isHidden());
            QVERIFY(toggle->isChecked());
            toggle->trigger();
            QVERIFY(dock->isHidden());
        }
    }
    void lyricsFitAtDifferentDpiAndAlignWithNotes()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(QStringLiteral("Q: 1/ 2/ 3 4 5 |\nC: 春，夏秋冬雪\nC: 一二三四五\n")));
        auto layout = JianpuLayout().layout(d.snapshot());
        int lyricCount = 0;
        for (const auto &item : layout.items)
        {
            if (item.kind != LayoutItem::Kind::Text || item.track < 0 ||
                qAbs(item.fontSize - LayoutSettings::fromJson({}).lyricPt * 4 / 3) > 0.01)
                continue;
            QFont font(item.fontFamily);
            font.setPixelSize(qRound(item.fontSize));
            for (int dpi : {96, 144, 192})
            {
                QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
                image.setDotsPerMeterX(qRound(dpi / 0.0254));
                image.setDotsPerMeterY(qRound(dpi / 0.0254));
                QFontMetricsF metrics(font, &image);
                QVERIFY2(metrics.height() <= item.bounds.height() + 0.01, qPrintable(item.text));
                image.fill(Qt::white);
                QPainter painter(&image);
                painter.setFont(font);
                painter.drawText(image.rect(), Qt::AlignLeft, item.text);
                painter.end();
                bool ink = false;
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x)
                        ink |= image.pixelColor(x, y) != QColor(Qt::white);
                QVERIFY(ink);
            }
            if (item.text == QStringLiteral("春，"))
            {
                qreal noteX = -1;
                for (const auto &hit : layout.hits)
                    if (hit.eventId == item.id)
                        noteX = hit.rect.center().x();
                QVERIFY(noteX > 0);
                QVERIFY(qAbs(item.bounds.left() +
                             QFontMetricsF(font).horizontalAdvance(QStringLiteral("春")) / 2 -
                             noteX) < 0.01);
            }
            ++lyricCount;
        }
        QCOMPARE(lyricCount, 10);
    }
    void referenceHeaderAndBarlineStyle()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(QStringLiteral("B: 参考版式\nB: 副标题\nZ: 张三 词 / 李四 曲\n"
                                           "D: G\nP: 4/4\nQ: 1 2 3 4 | 5 6 7 1 ||\n")));
        auto s = d.snapshot();
        auto first = s.measures[0].events[0].notes[0].id;
        QVERIFY(d.edit({EditRequest::Kind::AddMark, {first}, 152, 0, "tempo"}));
        s = d.snapshot();
        auto layout = JianpuLayout().layout(s);
        auto style = LayoutSettings::fromJson({});
        QVERIFY(!style.noteBold);
        QVERIFY(!style.showMeasures);
        QRectF key, author;
        int tempoLabels = 0;
        QString tempoId;
        for (const auto &m : s.marks)
            if (m.kind == "tempo")
                tempoId = m.id;
        QVERIFY(!tempoId.isEmpty());
        for (const auto &item : layout.items)
        {
            if (item.text == "1 = G")
                key = item.bounds;
            if (item.text == QStringLiteral("张三 词 / 李四 曲"))
                author = item.bounds;
            if (item.id == tempoId && item.kind == LayoutItem::Kind::Text)
            {
                ++tempoLabels;
                QVERIFY(item.bounds.top() < layout.systems[0].bounds.top());
            }
            // Normal barlines end measures; there is no extra line before the first note.
            if (item.id == s.measures[0].id && item.kind == LayoutItem::Kind::Line)
                QVERIFY(item.bounds.left() > layout.systems[0].bounds.left() + 1);
        }
        QVERIFY(!key.isEmpty());
        QVERIFY(!author.isEmpty());
        QVERIFY(author.left() > key.right());
        QVERIFY(qAbs(author.center().y() - key.center().y()) < 0.01);
        QCOMPARE(tempoLabels, 1);
        QVERIFY(!layout.hits.first().eventId.isEmpty());
        QVERIFY(layout.hitTest(layout.items.first().bounds.center()) != nullptr);
        QCOMPARE(layout.hitTest(layout.items.first().bounds.center())->id, QString("@title:0"));
    }
    void automaticBarsAndLayoutRowsRoundTrip()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(QStringLiteral(
            "# 保留\nB: 同步测试\nQ: 1 2 3 4 5 6 7 1 2 3 4 5 6 7 1 2\n"
            "C: 春夏秋冬山水天地花草树木风雨云雪\nC: 一二三四五六七八九十一二三四五六\n")));
        auto source = d.sourceText();
        QCOMPARE(FanqieFormat().parse(source).boundaries.size(), 4);
        auto view = d.viewSettings();
        auto style = LayoutSettings::defaults();
        style["measuresPerLine"] = 2;
        view["layout"] = style;
        d.setViewSettings(view);
        auto twoRows = d.sourceText();
        QCOMPARE(twoRows.count("Q:"), 2);
        QCOMPARE(twoRows.count("C:"), 4);
        auto parsed = FanqieFormat().parse(twoRows);
        QVERIFY2(!parsed.hasErrors(), qPrintable(parsed.messages().join('\n')));
        QCOMPARE(parsed.notes.size(), 16);
        QCOMPARE(parsed.notes[8].lyrics[0], QStringLiteral("花"));
        QCOMPARE(parsed.notes[8].lyrics[1], QStringLiteral("九"));
        for (const auto &m : d.snapshot().measures)
            for (const auto &e : m.events)
                if (!e.rest)
                {
                    auto match =
                        std::find_if(parsed.notes.begin(), parsed.notes.end(),
                                     [&](const auto &n) { return n.position == e.position; });
                    QVERIFY(match != parsed.notes.end());
                    QCOMPARE(match->midi, e.notes[0].midi);
                    QCOMPARE(match->lyrics[0], e.lyrics[0].text);
                }
        d.undo();
        QCOMPARE(d.sourceText(), source);
        d.redo();
        QCOMPARE(d.sourceText(), twoRows);
        style["measuresPerLine"] = 4;
        view["layout"] = style;
        d.setViewSettings(view);
        QCOMPARE(d.sourceText().count("Q:"), 1);
        QTemporaryDir dir;
        QVERIFY(d.save(dir.filePath("aligned.jianpu")));
        ScoreDocument reopened;
        QVERIFY(reopened.open(dir.filePath("aligned.jianpu")));
        QCOMPARE(reopened.sourceText(), d.sourceText());
        QCOMPARE(reopened.snapshot().measures[0].events[0].lyrics[0].text, QStringLiteral("春"));
    }
    void automaticBarsFollowDurationEdits()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText("Q: 1 2 3 4 5 6 7 1\n"));
        auto source = d.sourceText();
        QVERIFY(source.contains('|'));
        source.replace("1 2 3", "1 2- 3");
        QVERIFY2(d.applyText(source), qPrintable(d.error()));
        auto parsed = FanqieFormat().parse(d.sourceText());
        QVERIFY2(!parsed.hasErrors(), qPrintable(parsed.messages().join('\n')));
        QCOMPARE(parsed.notes[1].duration, MusicalTime(1, 2));
        QCOMPARE(d.snapshot().measures.size(), 3);
        for (const auto &m : d.snapshot().measures)
            QCOMPARE(m.duration, qint64(1920));
    }
    void multiVoiceRowsAndVisualProjection()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        auto view = d.viewSettings();
        auto style = LayoutSettings::defaults();
        style["measuresPerLine"] = 1;
        view["layout"] = style;
        d.setViewSettings(view);
        QVERIFY(
            d.applyText(QStringLiteral("Q1: 1 2 3 4 5 6 7 1\nC1: 春夏秋冬山水天地\n"
                                       "C1: 一二三四五六七八\nQ2: 5- 6- 7- 1-\nC2: 花草树木\n")));
        QCOMPARE(d.sourceText().count("Q1:"), 2);
        QCOMPARE(d.sourceText().count("Q2:"), 2);
        QCOMPARE(d.sourceText().count("C1:"), 4);
        QCOMPARE(d.sourceText().count("C2:"), 2);
        auto p = FanqieFormat().parse(d.sourceText());
        QVERIFY2(!p.hasErrors(), qPrintable(p.messages().join('\n')));
        auto s = d.snapshot();
        auto layout = JianpuLayout().layout(s, d.viewSettings());
        QMap<qint64, qreal> anchors;
        for (const auto &hit : layout.hits)
            if (hit.note)
            {
                if (anchors.contains(hit.tick))
                    QVERIFY(qAbs(anchors[hit.tick] - hit.rect.center().x()) < 0.01);
                anchors[hit.tick] = hit.rect.center().x();
            }
        auto id = s.measures[0].events[0].notes[0].id;
        QVERIFY(d.edit({EditRequest::Kind::AddChordNote, {id}, 67}));
        QVERIFY(d.inputState()["protected"].toBool());
        QCOMPARE(d.sourceText().count("Q1:"), 2);
        QCOMPARE(d.sourceText().count("Q2:"), 2);
        QVERIFY(d.sourceText().contains(QStringLiteral("春")));
        d.undo();
        QVERIFY(!d.inputState()["protected"].toBool());
        QCOMPARE(d.snapshot().noteCount(), s.noteCount());
    }
    void regularSystemsShareMeasureGrid()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText("Q: " + QString("1 2 3 4 | ").repeated(8) + "\n"));
        auto s = d.snapshot();
        auto style = LayoutSettings::defaults("children");
        auto layout = JianpuLayout().layout(s, {{"layout", style}});
        QVERIFY(layout.systems.size() >= 2);
        QMap<int, QVector<qreal>> starts;
        QSet<QString> seen;
        for (const auto &hit : layout.hits)
            if (hit.note && !seen.contains(s.measures[hit.measure].id))
            {
                seen.insert(s.measures[hit.measure].id);
                for (int row = 0; row < layout.systems.size(); ++row)
                    if (layout.systems[row].start <= MusicalTime(hit.tick, 1920) &&
                        MusicalTime(hit.tick, 1920) < layout.systems[row].end)
                        starts[row] << hit.rect.center().x();
            }
        for (int row = 1; row < layout.systems.size(); ++row)
            for (int column = 0; column < std::min(starts[0].size(), starts[row].size()); ++column)
                QVERIFY(qAbs(starts[0][column] - starts[row][column]) < 0.01);
    }
    void continuationTextPreservesTiesAndLyrics()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(QStringLiteral("Q: 1 2---\nC: 春夏\n")));
        auto aligned = d.sourceText();
        QVERIFY(aligned.contains('^'));
        auto p = FanqieFormat().parse(aligned);
        QVERIFY2(!p.hasErrors(), qPrintable(p.messages().join('\n')));
        QVERIFY(p.notes.last().tieBack);
        QVERIFY(p.notes.last().lyrics.isEmpty());
        int noteCount = d.snapshot().noteCount();
        QVERIFY2(d.applyText(aligned + "# round trip\n"), qPrintable(d.error()));
        QCOMPARE(d.snapshot().noteCount(), noteCount);
        int lyrics = 0, ties = 0;
        for (const auto &m : d.snapshot().measures)
            for (const auto &e : m.events)
            {
                lyrics += e.lyrics.size();
                for (const auto &n : e.notes)
                    ties += n.tieStart;
            }
        QCOMPARE(lyrics, 2);
        QVERIFY(ties > 0);
        QVERIFY(FanqieFormat().parse("Q: 1^ 2").hasErrors());
        QVERIFY(FanqieFormat().parse("Q: 1^").hasErrors());
        auto view = d.viewSettings();
        auto style = LayoutSettings::defaults();
        style["measuresPerLine"] = 1;
        view["layout"] = style;
        d.setViewSettings(view);
        QCOMPARE(d.sourceText().count("Q:"), 2);
        style["measuresPerLine"] = 4;
        view["layout"] = style;
        d.setViewSettings(view);
        QCOMPARE(d.sourceText().count("Q:"), 1);
    }
    void importedTupletProjectionKeepsTiming()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText("Q: (y1/2/3/) 4 5 6 |\n"));
        QTemporaryDir dir;
        QVERIFY(d.exportFile(dir.filePath("tuplet.musicxml")));
        ScoreDocument imported;
        QVERIFY(imported.open(dir.filePath("tuplet.musicxml")));
        const auto source = imported.sourceText();
        const auto p = FanqieFormat().parse(source);
        QVERIFY2(!p.hasErrors(), qPrintable(p.messages().join('\n')));
        QCOMPARE(p.notes[1].position, MusicalTime(1, 12));
        auto s = imported.snapshot();
        for (const auto &m : s.measures)
            for (const auto &e : m.events)
                if (!e.grace)
                {
                    QVERIFY(e.sourceStart >= 0);
                    auto match = std::find_if(p.notes.begin(), p.notes.end(), [&](const auto &n)
                                              { return n.offset == e.sourceStart; });
                    QVERIFY(match != p.notes.end());
                    QCOMPARE(match->position, e.position);
                    QCOMPARE(match->duration, e.length);
                }
    }
    void keyboardSuffixAfterAutomaticBar()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *editor = w.findChild<SourceEditor *>();
        w.canvas()->setFocus();
        QTest::keyClick(w.canvas(), Qt::Key_Q);
        QTest::keyClicks(w.canvas(), "1234");
        QTRY_VERIFY(w.document()->sourceText().contains('|'));
        QTest::keyClick(w.canvas(), Qt::Key_Slash);
        QTRY_COMPARE(FanqieFormat().parse(w.document()->sourceText()).notes.last().duration,
                     MusicalTime(1, 8));
        QVERIFY(!w.document()->inputState()["hasErrors"].toBool());
        editor->setFocus();
        QTRY_VERIFY(editor->hasFocus());
    }
    void continuousChineseLyricsKeepCaretAtEnd()
    {
        MainWindow w;
        w.setConfirmClose(false);
        w.show();
        auto *editor = w.findChild<SourceEditor *>();
        editor->setPlainText("Q: 1 2 3 4\nC: ");
        QTRY_COMPARE(w.document()->snapshot().noteCount(), 4);
        editor->moveCursor(QTextCursor::End);
        editor->setFocus();
        for (const auto &word : {QStringLiteral("春"), QStringLiteral("夏"), QStringLiteral("秋"),
                                 QStringLiteral("冬")})
        {
            QInputMethodEvent ime;
            ime.setCommitString(word);
            QApplication::sendEvent(editor, &ime);
            QTRY_VERIFY(w.document()->sourceText().contains(word));
            QVERIFY(!w.document()->inputState()["hasErrors"].toBool());
            QCOMPARE(editor->textCursor().position(), int(editor->toPlainText().size()));
        }
        auto s = w.document()->snapshot();
        QCOMPARE(s.measures[0].events[0].lyrics[0].text, QStringLiteral("春"));
        QCOMPARE(s.measures[0].events[3].lyrics[0].text, QStringLiteral("冬"));
        auto source = w.document()->sourceText();
        source.replace(QStringLiteral("春 夏 秋 冬"), QStringLiteral("春 @"));
        QVERIFY(w.document()->applyText(source));
        QVERIFY(w.document()->sourceText().trimmed().endsWith("@"));
        QVERIFY(w.document()->applyText(w.document()->sourceText() + QStringLiteral("夏")));
        s = w.document()->snapshot();
        QVERIFY(s.measures[0].events[1].lyrics.isEmpty());
        QCOMPARE(s.measures[0].events[2].lyrics[0].text, QStringLiteral("夏"));
    }
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
        QCOMPARE(p.pendingLyrics.join(""), QStringLiteral("快活。"));
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
        source = d.sourceText();
        source.replace(QStringLiteral("原题"), QStringLiteral("改题"));
        QVERIFY(d.applyText(source));
        QVERIFY(d.inputState()["pendingLyrics"].toArray().contains(QStringLiteral("夏")));
        source = d.sourceText();
        source.replace(QStringLiteral("春 秋"), QStringLiteral("春~秋"));
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
        auto *input = w.canvas();
        QVERIFY(w.canvas()->lyricSelected());
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("中"));
        QApplication::sendEvent(input, &commit);
        QTRY_VERIFY(w.document()->sourceText().contains(QStringLiteral("C2: 中")));
        QTest::keyClick(input, Qt::Key_Escape);
        editor->setFocus();
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
        auto *input = w.canvas();
        QVERIFY(w.canvas()->lyricSelected());
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("春，夏秋冬"));
        QApplication::sendEvent(input, &commit);
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
