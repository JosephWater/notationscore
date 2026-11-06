// SPDX-License-Identifier: GPL-3.0-only
#include "core/runtime.h"
#include "core/scoredocument.h"
#include "input/textmarks.h"
#include "layout/jianpulayout.h"
#include "layout/layoutsettings.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
using namespace jianpu;
class NotationTest : public QObject
{
    Q_OBJECT
    QString sample()
    {
        QFile f(NOTATION_EXAMPLE);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(f.readAll());
    }
    QStringList signature(const ScoreSnapshot &s)
    {
        QStringList out;
        for (const auto &m : s.marks)
            out << m.kind + ":" + QString::number(m.tick) + ":" + QString::number(m.endTick) + ":" +
                       QString::fromUtf8(
                           QJsonDocument(m.attributes).toJson(QJsonDocument::Compact));
        for (const auto &m : s.measures)
            if (m.repeatEnd)
                out << "repeat:" + QString::number(m.tick) + ":" + QString::number(m.repeatCount);
        out.sort();
        return out;
    }
  private slots:
    void grammarAndChords()
    {
        auto p = FanqieFormat().parse("V: 1.1\nQ: {1 3' 5#}/ 2/ 3 4 5 |\nC1: 春夏秋冬天\n");
        QVERIFY2(!p.hasErrors(), qPrintable(p.messages().join('\n')));
        QCOMPARE(p.notes.size(), 5);
        QCOMPARE(p.notes[0].chord.size(), 2);
        QCOMPARE(p.notes[0].duration, MusicalTime(1, 8));
        QCOMPARE(p.notes[0].chord[0].midi, 76);
        QCOMPARE(p.notes[0].chord[1].midi, 68);
        QCOMPARE(FanqieFormat::token(p.notes[0]), "{1 3' 5#}/");
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY2(d.applyText(p.source), qPrintable(d.error()));
        QCOMPARE(d.snapshot().noteCount(), 7);
        QVERIFY(d.sourceText().contains("{1 3' 5#}/"));
        QCOMPARE(d.textSnapshot().notes[0].lyrics.value(0), QStringLiteral("春"));
    }
    void badMarks_data()
    {
        QTest::addColumn<QString>("mark");
        QTest::newRow("unknown") << "{\"kind\":\"invented\",\"voice\":1,\"start\":\"0/1\"}";
        QTest::newRow("zero denominator")
            << "{\"kind\":\"tempo\",\"voice\":0,\"start\":\"0/0\",\"bpm\":96}";
        QTest::newRow("missing ending")
            << "{\"kind\":\"volta\",\"voice\":1,\"start\":\"0/1\",\"end\":\"1/1\",\"closed\":true}";
        QTest::newRow("invalid time") << "{\"kind\":\"time\",\"voice\":0,\"start\":\"0/"
                                         "1\",\"numerator\":4,\"denominator\":0}";
    }
    void badMarks()
    {
        QFETCH(QString, mark);
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText("Q: 1 2 3 4 |\n"));
        auto before = d.sourceText();
        QVERIFY(!d.applyText("V: 1.1\nQ: 1 2 3 4 |\nM: " + mark + "\n"));
        QCOMPARE(d.sourceText(), before);
    }
    void importExportAndProject()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY2(d.applyText(sample()), qPrintable(d.error()));
        auto s = d.snapshot();
        QCOMPARE(s.measures[1].repeatCount, 3);
        QCOMPARE(s.noteCount(), 19);
        int voltas = 0;
        bool breath = false, pedal = false, jump = false;
        for (const auto &m : s.marks)
        {
            if (m.kind == "volta")
            {
                ++voltas;
                QVERIFY(!m.attributes["endings"].toArray().isEmpty());
            }
            breath |= m.kind == "breath";
            pedal |= m.kind == "pedal";
            jump |= m.kind == "jump";
        }
        QCOMPARE(voltas, 2);
        QVERIFY(breath);
        QVERIFY(pedal);
        QVERIFY(jump);
        auto original = signature(s);
        QTemporaryDir dir;
        for (const auto &suffix : {"jianpu", "musicxml", "mxl"})
        {
            auto path = dir.filePath(QString("score.") + suffix);
            QVERIFY2(QString(suffix) == "jianpu" ? d.save(path) : d.exportFile(path),
                     qPrintable(d.error()));
            ScoreDocument other;
            QVERIFY2(other.open(path), qPrintable(other.error()));
            QCOMPARE(other.snapshot().noteCount(), s.noteCount());
            QCOMPARE(signature(other.snapshot()), original);
            QVERIFY(other.sourceText().contains("V: 1.1"));
        }
    }
    void chordAndMarksFollowEditsAndUndo()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText("V: 1.1\nQ: 1 2 3 4 |\nC1: 春夏秋冬\nM: "
                            "{\"kind\":\"fermata\",\"voice\":1,\"start\":\"1/2\"}\n"));
        auto before = d.sourceText();
        auto p = d.textSnapshot();
        auto changed = before;
        changed.insert(p.notes[1].offset, "5 ");
        SourceChanges edits{before, {{p.notes[1].offset, 0, 2}}};
        QVERIFY2(d.applyText(changed, {}, edits), qPrintable(d.error()));
        bool found = false;
        for (const auto &m : d.snapshot().marks)
            if (m.kind == "fermata")
            {
                found = true;
                QCOMPARE(m.tick, 1440);
            }
        QVERIFY(found);
        QCOMPARE(d.textSnapshot().notes[2].lyrics.value(0), QStringLiteral("夏"));
        d.undo();
        QCOMPARE(d.sourceText(), before);
        d.redo();
        QCOMPARE(d.snapshot().noteCount(), 5);
        auto s = d.snapshot();
        QString id;
        for (const auto &bar : s.measures)
            for (const auto &e : bar.events)
                if (!e.notes.isEmpty())
                {
                    id = e.notes[0].id;
                    break;
                }
        QVERIFY(d.edit({EditRequest::Kind::OctaveUp, {id}}));
        QVERIFY(d.sourceText().contains("M:"));
        QVERIFY(!d.textSnapshot().hasErrors());
    }
    void crossLineVoltaGeometry()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(sample()));
        auto view = d.viewSettings();
        auto style = LayoutSettings::defaults("classic");
        style["measuresPerLine"] = 1;
        view["layout"] = style;
        d.setViewSettings(view);
        QString open;
        for (const auto &m : d.snapshot().marks)
            if (m.kind == "volta" && !m.attributes["closed"].toBool())
                open = m.id;
        QVERIFY(!open.isEmpty());
        int paths = 0;
        for (const auto &item : d.presentation().items)
            if (item.id == open && item.kind == LayoutItem::Kind::Curve)
                ++paths;
        QCOMPARE(paths, 2);
        QVERIFY(d.sourceText().contains("\"endings\":[1,3]"));
        bool countLabel = false;
        for (const auto &item : d.presentation().items)
            countLabel |= item.text == QStringLiteral("×3");
        QVERIFY(countLabel);
    }
    void midMeasureOctaveLineKeepsSoundingPitch()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY2(d.applyText("V: 1.1\nQ: 1 2 3 4 |\nM: "
                             "{\"kind\":\"ottava\",\"voice\":1,\"start\":\"1/4\",\"end\":\"3/"
                             "4\",\"octaves\":1}\n"),
                 qPrintable(d.error()));
        QVector<int> pitches;
        for (const auto &m : d.snapshot().measures)
            for (const auto &e : m.events)
                for (const auto &n : e.notes)
                    pitches << n.midi;
        QCOMPARE(pitches, QVector<int>({60, 62, 64, 65}));
        bool found = false;
        for (const auto &m : d.snapshot().marks)
            if (m.kind == "ottava")
            {
                found = true;
                QCOMPARE(m.tick, 480);
                QCOMPARE(m.endTick, 1440);
            }
        QVERIFY(found);
        QTemporaryDir dir;
        QVERIFY(d.exportFile(dir.filePath("octave.xml")));
        ScoreDocument back;
        QVERIFY(back.open(dir.filePath("octave.xml")));
        QCOMPARE(signature(back.snapshot()), signature(d.snapshot()));
    }
    void keyAndMeterChange()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        auto source = QString(
            "V: 1.1\nQ: 1 2 3 4 | 1 2 3 | 4 5 6 |\nM: "
            "{\"kind\":\"time\",\"voice\":0,\"start\":\"1/"
            "1\",\"numerator\":3,\"denominator\":4}\nM: "
            "{\"kind\":\"key\",\"voice\":0,\"start\":\"1/1\",\"fifths\":2,\"minor\":false}\n");
        auto parsed = FanqieFormat().parse(source);
        QVERIFY2(!parsed.hasErrors(), qPrintable(parsed.messages().join('\n')));
        QCOMPARE(parsed.notes[4].midi, 62);
        QVERIFY2(d.applyText(source), qPrintable(d.error()));
        QCOMPARE(d.snapshot().measures[1].numerator, 3);
        QCOMPARE(d.snapshot().measures[1].duration, 1440);
        QCOMPARE(d.snapshot().measures[1].events[0].notes[0].midi, 62);
    }
    void deletedPointMarkFollowsItsNote()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(
            "V: 1.1\nQ: 1 2 3 4 |\nM: {\"kind\":\"fermata\",\"voice\":1,\"start\":\"1/2\"}\n"));
        auto before = d.sourceText(), after = before;
        auto n = d.textSnapshot().notes[2];
        after.remove(n.offset, n.length + 1);
        QVERIFY2(d.applyText(after, {}, SourceChanges{before, {{n.offset, n.length + 1, 0}}}),
                 qPrintable(d.error()));
        QCOMPARE(d.snapshot().noteCount(), 3);
        for (const auto &m : d.snapshot().marks)
            QVERIFY(m.kind != "fermata");
        d.undo();
        QVERIFY(d.sourceText().contains("fermata"));
    }
    void fragmentRetainsChordAndVolta()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(sample()));
        auto before = signature(d.snapshot());
        ScoreSelection selection;
        selection.kind = SelectionKind::Measures;
        selection.measures = {0, 1};
        auto fragment = d.copyFragment(selection);
        QVERIFY(!fragment.isEmpty());
        ScoreDropTarget target;
        target.measure = 4;
        QVERIFY2(d.pasteFragment(fragment, target, true), qPrintable(d.error()));
        QCOMPARE(d.snapshot().measures.size(), 7);
        QVERIFY(d.sourceText().contains("V: 1.1"));
        QVERIFY(!d.textSnapshot().hasErrors());
        d.undo();
        QCOMPARE(signature(d.snapshot()), before);
    }
    void exactTupletMarkPosition()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY2(d.applyText("V: 1.1\nQ: (y7:4 1/ 2/ 3/ 4/ 5/ 6/ 7/) |\nM: "
                             "{\"kind\":\"fermata\",\"voice\":1,\"start\":\"1/14\"}\n"),
                 qPrintable(d.error()));
        bool found = false;
        for (const auto &mark : d.snapshot().marks)
            if (mark.kind == "fermata")
            {
                found = true;
                QCOMPARE(mark.position, MusicalTime(1, 14));
            }
        QVERIFY(found);
        auto projected = FanqieFormat().fromSnapshot(d.snapshot());
        QVERIFY(projected.contains("\"start\":\"1/14\""));
    }
    void visualChordEditUpgradesTextWithoutProtection()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText("Q: 1 2 3 4 |\n"));
        auto id = d.snapshot().measures[0].events[0].notes[0].id;
        QVERIFY2(d.edit({EditRequest::Kind::AddChordNote, {id}, 67}), qPrintable(d.error()));
        QCOMPARE(d.snapshot().noteCount(), 5);
        QVERIFY(d.sourceText().contains("V: 1.1"));
        QVERIFY(!d.inputState()["protected"].toBool());
        QVERIFY(d.sourceText().contains("{1 5}"));
        d.undo();
        QCOMPARE(d.snapshot().noteCount(), 4);
        d.redo();
        QCOMPARE(d.snapshot().noteCount(), 5);
    }
    void unrepresentableNativeArticulationStillExports()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText("Q: 1 2 3 4 |\n"));
        auto id = d.snapshot().measures[0].events[0].notes[0].id;
        QVERIFY(d.edit(
            {EditRequest::Kind::AddMark, {id}, 0, 0, "articulation:articTenutoStaccatoAbove"}));
        QTemporaryDir dir;
        QVERIFY2(d.exportFile(dir.filePath("native.xml")), qPrintable(d.error()));
        ScoreDocument native;
        QVERIFY(native.open(dir.filePath("native.xml")));
        QVERIFY2(native.exportFile(dir.filePath("again.xml")), qPrintable(native.error()));
        QCOMPARE(native.snapshot().noteCount(), 4);
    }
    void extendedRestEditRetainsBothVersesAsPending()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        QVERIFY(d.applyText(QStringLiteral("V: 1.1\nQ: 1 2 3 4 |\nC1: 春夏秋冬\nC1: 一二三四\n")));
        auto original = d.sourceText();
        auto id = d.snapshot().measures[0].events[0].notes[0].id;
        QVERIFY2(d.edit({EditRequest::Kind::SetDegree, {id}, 0}), qPrintable(d.error()));
        auto pending = d.inputState()["pendingLyrics"].toArray();
        QVERIFY(pending.contains(QStringLiteral("春")));
        QVERIFY(pending.contains(QStringLiteral("一")));
        d.undo();
        QCOMPARE(d.sourceText(), original);
        QVERIFY(d.inputState()["pendingLyrics"].toArray().isEmpty());
        d.redo();
        QVERIFY(d.inputState()["pendingLyrics"].toArray().contains(QStringLiteral("春")));
    }
    void oldFormatCompatibility()
    {
        auto p = FanqieFormat().parse("V: 1.0\nQ: 1 2 3 4 |\n");
        QVERIFY(!p.hasErrors());
        QVERIFY(TextFormatAdapter::create("fanqie-1.1"));
        QVERIFY(FanqieFormat().parse("V: 1.0\nQ: {1 3} |\n").hasErrors());
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    EngineRuntime runtime;
    NotationTest t;
    return QTest::qExec(&t, argc, argv);
}
#include "tst_notation.moc"
