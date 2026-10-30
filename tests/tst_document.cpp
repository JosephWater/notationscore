// SPDX-License-Identifier: GPL-3.0-only
#include "core/runtime.h"
#include "core/scoredocument.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace jianpu;
namespace
{
QStringList notes(const ScoreSnapshot &s, int track = 0)
{
    QStringList ids;
    for (const auto &m : s.measures)
        for (const auto &e : m.events)
            if (e.track == track && !e.grace)
                for (const auto &n : e.notes)
                    ids << n.id;
    return ids;
}
QByteArray semantic(const ScoreSnapshot &s)
{
    QByteArray bytes;
    for (const auto &m : s.measures)
    {
        bytes += QByteArray::number(m.tick) + ":" + QByteArray::number(m.duration) + ";";
        for (const auto &e : m.events)
        {
            bytes += QByteArray::number(e.track) + ":" + QByteArray::number(e.tick) + ":" +
                     QByteArray::number(e.duration) + ":";
            for (const auto &n : e.notes)
                bytes += QByteArray::number(n.midi) + ",";
            for (const auto &l : e.lyrics)
                bytes += QByteArray::number(l.verse) + l.text.toUtf8();
            bytes += ";";
        }
    }
    return bytes;
}
int pitch(const ScoreSnapshot &s, const QString &id)
{
    for (const auto &m : s.measures)
        for (const auto &e : m.events)
            for (const auto &n : e.notes)
                if (n.id == id)
                    return n.midi;
    return -1;
}
} // namespace
class DocumentTest : public QObject
{
    Q_OBJECT
  private slots:
    void nativeAndFormatRoundTrips()
    {
        ScoreDocument d;
        QVERIFY2(d.open(EXAMPLE_SCORE), qPrintable(d.error()));
        auto initial = d.snapshot();
        QCOMPARE(initial.measures.size(), 8);
        QCOMPARE(initial.voices.size(), 3);
        QCOMPARE(initial.noteCount(), 96);
        QTemporaryDir dir;
        auto file = dir.filePath(QStringLiteral("中文工程.jianpu"));
        d.setViewSettings(QJsonObject{{"zoom", 1.25}});
        QVERIFY2(d.save(file), qPrintable(d.error()));
        QVERIFY(!d.modified());
        ScoreDocument other;
        QVERIFY2(other.open(file), qPrintable(other.error()));
        QCOMPARE(semantic(initial), semantic(other.snapshot()));
        QCOMPARE(other.viewSettings(), d.viewSettings());
        QCOMPARE(notes(other.snapshot()), notes(initial));
        QVERIFY(!other.modified());
        for (const auto &suffix : {"musicxml", "mxl", "mid"})
        {
            auto path = dir.filePath(QStringLiteral("roundtrip.") + suffix);
            QVERIFY2(d.exportFile(path), qPrintable(d.error()));
            ScoreDocument imported;
            QVERIFY2(imported.open(path), qPrintable(imported.error()));
            QVERIFY(imported.snapshot().noteCount() >= initial.noteCount());
            if (QString(suffix) != "mid")
                QCOMPARE(semantic(initial), semantic(imported.snapshot()));
        }
    }
    void pitchLyricsAndUndo()
    {
        ScoreDocument d;
        QVERIFY(d.open(EXAMPLE_SCORE));
        auto before = d.snapshot();
        auto id = notes(before).first();
        QVERIFY2(d.edit({EditRequest::Kind::DiatonicUp, {id}}), qPrintable(d.error()));
        QCOMPARE(pitch(d.snapshot(), id), 62);
        QVERIFY(d.canUndo());
        d.undo();
        QCOMPARE(semantic(d.snapshot()), semantic(before));
        d.redo();
        QCOMPARE(pitch(d.snapshot(), id), 62);
        d.undo();
        QVERIFY2(d.edit({EditRequest::Kind::Lyrics, {id}, 1, 0, QStringLiteral("中文<&歌词")}),
                 qPrintable(d.error()));
        auto now = d.snapshot();
        QCOMPARE(now.measures[0].events[0].lyrics[1].text, QStringLiteral("中文<&歌词"));
        d.undo();
        QCOMPARE(semantic(d.snapshot()), semantic(before));
        QVERIFY(d.canRedo());
        QVERIFY(d.edit({EditRequest::Kind::OctaveUp, {id}}));
        QVERIFY(!d.canRedo());
    }
    void rhythmChordAndNewScore()
    {
        ScoreDocument d;
        QVERIFY2(d.newScore(), qPrintable(d.error()));
        auto rest = d.snapshot().measures[0].events[0].id;
        QVERIFY2(d.edit({EditRequest::Kind::SetDegree, {rest}, 1, 4}), qPrintable(d.error()));
        auto id = notes(d.snapshot()).first();
        QCOMPARE(pitch(d.snapshot(), id), 60);
        QVERIFY2(d.edit({EditRequest::Kind::AddChordNote, {id}, 64}), qPrintable(d.error()));
        QCOMPARE(d.snapshot().noteCount(), 2);
        auto chordNotes = notes(d.snapshot());
        QVERIFY2(d.edit({EditRequest::Kind::Delete, {chordNotes.last()}}), qPrintable(d.error()));
        QCOMPARE(d.snapshot().noteCount(), 1);
        d.undo();
        QCOMPARE(d.snapshot().noteCount(), 2);
        auto before = semantic(d.snapshot());
        QVERIFY2(d.edit({EditRequest::Kind::Duration, {id}, 8, 1}), qPrintable(d.error()));
        d.undo();
        QCOMPARE(semantic(d.snapshot()), before);
        QVERIFY2(d.edit({EditRequest::Kind::AppendMeasure}), qPrintable(d.error()));
        QCOMPARE(d.snapshot().measures.size(), 2);
        d.undo();
        QCOMPARE(d.snapshot().measures.size(), 1);
    }
    void errorsPreserveDocumentAndFile()
    {
        ScoreDocument d;
        QVERIFY(d.open(EXAMPLE_SCORE));
        auto before = semantic(d.snapshot());
        QTemporaryDir dir;
        QFile invalid(dir.filePath("broken.musicxml"));
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.write("<score-partwise>");
        invalid.close();
        QVERIFY(!d.open(invalid.fileName()));
        QCOMPARE(semantic(d.snapshot()), before);
        auto file = dir.filePath("score.jianpu");
        QVERIFY(d.save(file));
        QVERIFY(!d.save(dir.filePath("missing/score.jianpu")));
        ScoreDocument restored;
        QVERIFY(restored.open(file));
        QCOMPARE(semantic(restored.snapshot()), before);
        QVERIFY(!d.edit({EditRequest::Kind::Duration, {notes(d.snapshot()).first()}, 3}));
        QCOMPARE(semantic(d.snapshot()), before);
    }
    void marksVoicesAndSavedState()
    {
        ScoreDocument d;
        QVERIFY(d.open(EXAMPLE_SCORE));
        auto ids = notes(d.snapshot());
        auto before = semantic(d.snapshot());
        for (const auto &kind : {"slur", "crescendo", "volta"})
        {
            QVERIFY2(d.edit({EditRequest::Kind::AddMark, {ids[0], ids[3]}, 1, 0, kind}),
                     qPrintable(d.error()));
            d.undo();
            QCOMPARE(semantic(d.snapshot()), before);
        }
        QVERIFY2(d.edit({EditRequest::Kind::AddVoice, {ids[0]}}), qPrintable(d.error()));
        QCOMPARE(d.snapshot().voices.size(), 4);
        d.undo();
        QCOMPARE(d.snapshot().voices.size(), 3);
        QVERIFY(d.edit({EditRequest::Kind::DiatonicUp, {ids.first()}}));
        QTemporaryDir dir;
        QVERIFY(d.save(dir.filePath("state.jianpu")));
        QVERIFY(!d.modified());
        d.undo();
        QVERIFY(d.modified());
        d.redo();
        QVERIFY(!d.modified());
        d.undo();
        QVERIFY(d.edit({EditRequest::Kind::OctaveUp, {ids.first()}}));
        QVERIFY(d.modified());
    }
    void richMarks_data()
    {
        QTest::addColumn<QString>("kind");
        for (const auto &kind : {"dynamic:f", "tempo", "repeat-start", "repeat-end", "jump:DC_FINE",
                                 "marker:fine", "articulation:articStaccatoAbove",
                                 "articulation:ornamentTrill", "fermata", "tuplet", "grace", "tie"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    void richMarks()
    {
        QFETCH(QString, kind);
        ScoreDocument d;
        QVERIFY(d.open(EXAMPLE_SCORE));
        auto ids = notes(d.snapshot());
        if (kind == "tie")
            QVERIFY(d.edit({EditRequest::Kind::SetDegree, {ids[1]}, 1}));
        auto original = semantic(d.snapshot());
        auto originalCount = d.snapshot().noteCount();
        auto request =
            EditRequest{kind == "grace" ? EditRequest::Kind::Grace : EditRequest::Kind::AddMark,
                        kind == "tie" ? QStringList{ids[0], ids[1]} : QStringList{ids[0]},
                        kind == "tempo" ? 96 : 0, 0, kind};
        QVERIFY2(d.edit(request), qPrintable(d.error()));
        auto edited = d.snapshot();
        if (kind == "grace")
            QCOMPARE(edited.noteCount(), originalCount + 1);
        if (kind == "tuplet")
            QVERIFY(edited.measures[0].events[0].tuplet == 3);
        QTemporaryDir dir;
        auto path = dir.filePath("mark.jianpu");
        QVERIFY2(d.save(path), qPrintable(d.error()));
        ScoreDocument reopened;
        QVERIFY2(reopened.open(path), qPrintable(reopened.error()));
        QCOMPARE(semantic(edited), semantic(reopened.snapshot()));
        QStringList originalMarks, reopenedMarks;
        for (const auto &mark : edited.marks)
            originalMarks << mark.kind + ":" + mark.text + ":" + QString::number(mark.tick) + ":" +
                                 QString::number(mark.endTick);
        for (const auto &mark : reopened.snapshot().marks)
            reopenedMarks << mark.kind + ":" + mark.text + ":" + QString::number(mark.tick) + ":" +
                                 QString::number(mark.endTick);
        originalMarks.sort();
        reopenedMarks.sort();
        QCOMPARE(originalMarks, reopenedMarks);
        d.undo();
        QCOMPARE(semantic(d.snapshot()), original);
    }
    void midiOptionsAndClipboard()
    {
        ScoreDocument d;
        QVERIFY(d.open(EXAMPLE_SCORE));
        QTemporaryDir dir;
        auto path = dir.filePath("poly.mid");
        QVERIFY(d.exportFile(path));
        QString error;
        auto tracks = ScoreDocument::midiTracks(path, &error);
        QVERIFY2(tracks.size() >= 2, qPrintable(error));
        ScoreDocument selected;
        QVERIFY2(selected.open(path, {{tracks.last().index}, 32, false}),
                 qPrintable(selected.error()));
        QVERIFY(selected.snapshot().noteCount() > 0);
        QVERIFY(selected.snapshot().noteCount() < d.snapshot().noteCount() * 2);
        auto ids = notes(d.snapshot());
        auto clipboard = d.copy({ids[0], ids[1]});
        QVERIFY(!clipboard.isEmpty());
        auto before = semantic(d.snapshot());
        QVERIFY2(d.edit({EditRequest::Kind::Paste, {ids[6]}, 0, 0, QString::fromUtf8(clipboard)}),
                 qPrintable(d.error()));
        d.undo();
        QCOMPARE(semantic(d.snapshot()), before);
    }
    void instrumentsKeyTimeAndMarkProperties()
    {
        ScoreDocument d;
        QVERIFY(d.newScore());
        auto rest = d.snapshot().measures[0].events[0].id;
        QVERIFY(d.edit({EditRequest::Kind::SetDegree, {rest}, 1, 4}));
        auto id = notes(d.snapshot()).first();
        QVERIFY2(d.edit({EditRequest::Kind::SetKey, {id}, 1}), qPrintable(d.error()));
        QVERIFY2(d.edit({EditRequest::Kind::SetDegree, {id}, 4}), qPrintable(d.error()));
        QCOMPARE(pitch(d.snapshot(), id), 60);
        QVERIFY(d.edit({EditRequest::Kind::SetDegree, {id}, 7}));
        QCOMPARE(pitch(d.snapshot(), id), 66);
        QVERIFY(d.edit({EditRequest::Kind::Sharp, {id}}));
        QCOMPARE(pitch(d.snapshot(), id), 67);
        QVERIFY(d.edit({EditRequest::Kind::Natural, {id}}));
        QCOMPARE(pitch(d.snapshot(), id), 66);
        QVERIFY(d.edit({EditRequest::Kind::SetKey, {id}, 1, 1}));
        QVERIFY(d.snapshot().minor);
        auto voiceCount = d.snapshot().voices.size();
        QVERIFY2(d.edit({EditRequest::Kind::AddPart, {}, 0, 0, "flute"}), qPrintable(d.error()));
        QCOMPARE(d.snapshot().voices.size(), voiceCount + 1);
        d.undo();
        QCOMPARE(d.snapshot().voices.size(), voiceCount);
        QVERIFY2(d.edit({EditRequest::Kind::SetTime, {id}, 3, 4}), qPrintable(d.error()));
        QCOMPARE(d.snapshot().measures[0].numerator, 3);
        d.undo();
        QCOMPARE(d.snapshot().measures[0].numerator, 4);
        QVERIFY(d.edit({EditRequest::Kind::AddMark, {id}, 0, 0, "dynamic:p"}));
        QString markId;
        for (const auto &mark : d.snapshot().marks)
            if (mark.kind == "dynamic")
                markId = mark.id;
        QVERIFY(!markId.isEmpty());
        QVERIFY2(d.edit({EditRequest::Kind::UpdateMark, {markId}, 0, 0, "ff"}),
                 qPrintable(d.error()));
        auto now = d.snapshot();
        bool found = false;
        for (const auto &mark : now.marks)
            if (mark.id == markId && mark.text == "ff")
                found = true;
        QVERIFY(found);
        QVERIFY2(d.edit({EditRequest::Kind::RemoveMark, {markId}}), qPrintable(d.error()));
        d.undo();
        bool restored = false;
        for (const auto &mark : d.snapshot().marks)
            if (mark.id == markId)
                restored = true;
        QVERIFY(restored);
    }
    void expressiveFixtureAndFormatZero()
    {
        auto directory = QFileInfo(EXAMPLE_SCORE).dir();
        ScoreDocument d;
        QVERIFY2(d.open(directory.filePath("rhythm.musicxml")), qPrintable(d.error()));
        const auto s = d.snapshot();
        QCOMPARE(s.measures.size(), 4);
        QCOMPARE(s.measures[0].duration, 240);
        QCOMPARE(s.measures[2].numerator, 3);
        QCOMPARE(s.measures[3].denominator, 8);
        QVERIFY(!s.warnings.isEmpty());
        bool grace = false, tuplet = false, tie = false, percussion = false, dottedEighth = false;
        QString drumNote;
        for (const auto &voice : s.voices)
            if (voice.percussion)
                percussion = true;
        for (const auto &m : s.measures)
            for (const auto &e : m.events)
            {
                if (e.grace)
                    grace = true;
                if (e.tuplet)
                    tuplet = true;
                if (e.dots == 1 && e.denominator == 8 && e.duration == 360)
                    dottedEighth = true;
                if (e.track >= 4 && !e.notes.isEmpty())
                    drumNote = e.notes.first().id;
            }
        for (const auto &mark : s.marks)
            if (mark.kind == "tie")
                tie = true;
        QVERIFY(grace && tuplet && tie && percussion && dottedEighth);
        QVERIFY(!d.edit({EditRequest::Kind::DiatonicUp, {drumNote}}));
        QTemporaryDir dir;
        QVERIFY2(d.save(dir.filePath("rhythm.jianpu")), qPrintable(d.error()));
        ScoreDocument reopened;
        QVERIFY2(reopened.open(d.filePath()), qPrintable(reopened.error()));
        QCOMPARE(semantic(s), semantic(reopened.snapshot()));
        ScoreDocument midi;
        QVERIFY2(midi.open(directory.filePath("format0.mid")), qPrintable(midi.error()));
        QCOMPARE(midi.snapshot().noteCount(), 2);
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("JianpuLabTests");
    QCoreApplication::setOrganizationName("JianpuLab");
    QStandardPaths::setTestModeEnabled(true);
    EngineRuntime runtime;
    DocumentTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_document.moc"
