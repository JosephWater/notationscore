// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "musicaltime.h"
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QVector>

namespace jianpu
{
struct NoteData
{
    QString id;
    int midi = 60;
    int step = 0; // C=0 ... B=6, preserving spelling
    int alter = 0;
    int octave = 4;
    bool tieStart = false;
    bool tieEnd = false;
};
struct LyricData
{
    int verse = 0;
    QString text;
    bool extend = false;
};
struct EventData
{
    QString id;
    int track = 0;
    int keyFifths = 0;
    qint64 tick = 0;
    qint64 duration = 0;
    int denominator = 4;
    int dots = 0;
    int tuplet = 0;
    int tupletNormal = 0;
    QString tupletGroup;
    bool rest = false;
    bool grace = false;
    QVector<NoteData> notes;
    QVector<LyricData> lyrics;
    QStringList articulations;
    MusicalTime position, length;
    QString beamGroup;
    bool visible = true;
    bool rhythm = false;
    bool graceAfter = false;
    bool sourceBreak = false;
    QString logicalId;
    int sourceStart = -1;
    int sourceLength = 0;
};
struct VoiceData
{
    int track = 0;
    QString name;
    bool percussion = false;
};
struct MeasureData
{
    QString id;
    int number = 1;
    qint64 tick = 0;
    qint64 duration = 1920;
    int numerator = 4;
    int denominator = 4;
    bool repeatStart = false;
    bool repeatEnd = false;
    int repeatCount = 2;
    QVector<EventData> events;
    int barline = 1;
};
struct MarkData
{
    QString id;
    QString kind;
    QString text;
    int track = 0;
    qint64 tick = 0;
    qint64 endTick = 0;
    QString startId;
    QString endId;
    QJsonObject attributes;
    MusicalTime position, endPosition;
};
struct ScoreSnapshot
{
    quint64 revision = 0;
    bool textBacked = false; // Source bindings describe the written extent of each voice.
    QString title;
    int keyFifths = 0;
    bool minor = false;
    QVector<VoiceData> voices;
    QVector<MeasureData> measures;
    QVector<MarkData> marks;
    QStringList warnings;
    QStringList subtitles;
    QStringList authors;
    QMap<int, int> lyricRows;
    QMap<int, MusicalTime> sourceEnds;
    int noteCount() const
    {
        int n = 0;
        for (const auto &m : measures)
            for (const auto &e : m.events)
                n += e.notes.size();
        return n;
    }
};
struct ImportOptions
{
    QVector<int> tracks; // Empty means all tracks; indices after MIDI channel separation.
    int quantization = 16;
    bool tuplets = true;
};
struct MidiTrackInfo
{
    int index = 0;
    QString name;
    bool percussion = false;
};
enum class SelectionKind
{
    None,
    Notes,
    Lyrics,
    Measures,
    Graphic
};
struct ScoreSelection
{
    SelectionKind kind = SelectionKind::None;
    QStringList ids;
    QVector<int> measures;
    int track = 0, verse = 0;
};
struct ScoreDropTarget
{
    MusicalTime position;
    int track = 0, verse = 0, measure = -1;
    QVector<int> measures;
};
struct EditRequest
{
    enum class Kind
    {
        DiatonicUp,
        DiatonicDown,
        OctaveUp,
        OctaveDown,
        SetDegree,
        Sharp,
        Flat,
        Natural,
        Duration,
        Delete,
        AddChordNote,
        Lyrics,
        LyricsBatch,
        AppendMeasure,
        DeleteMeasure,
        AddMark,
        RemoveMark,
        SetKey,
        SetTime,
        AddVoice,
        AddPart,
        InsertRest,
        Paste,
        Grace,
        UpdateMark
    };
    Kind kind = Kind::DiatonicUp;
    QStringList ids;
    int value = 0;
    int secondary = 0;
    QString text;
    int dots = 0;
};
} // namespace jianpu
