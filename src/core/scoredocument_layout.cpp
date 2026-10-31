// SPDX-License-Identifier: GPL-3.0-only
#include "layout/jianpulayout.h"
#include "scoredocument.h"
#include <QJsonArray>
#include <algorithm>

namespace jianpu
{
void ScoreDocument::syncSourceLayout()
{
    if (!m_project || m_input.contains("draftSource"))
        return;
    auto score = snapshot();
    auto format = FanqieFormat();
    const bool projected = !m_input.contains("source") || m_input.value("protected").toBool();
    auto source = m_input["source"].toString();
    if (projected)
    {
        source = format.fromSnapshot(score);
        m_input["protected"] = true;
        m_input["format"] = format.formatId();
    }
    auto parsed = projected ? format.parse(source, m_view.value("inputHabits").toObject())
                            : parseText(source);
    if (parsed.hasErrors() || m_input["hasErrors"].toBool())
        return;
    auto oldBindings = m_input["bindings"].toArray();
    if (projected)
        oldBindings = {};
    QMap<int, int> tracks;
    int track = 0, visibleVoice = 0;
    QVector<int> projectedTracks;
    for (const auto &voice : score.voices)
        if (!voice.percussion)
            projectedTracks << voice.track;
    for (auto voice = parsed.voiceNames.begin(); voice != parsed.voiceNames.end(); ++voice)
    {
        tracks[voice.key()] = projected ? projectedTracks.value(visibleVoice++) : track;
        track += 4;
    }
    for (auto &note : parsed.notes)
        for (auto value : oldBindings)
        {
            auto b = value.toObject();
            if (b["offset"].toInt() != note.offset)
                continue;
            note.id = b["id"].toString();
            note.sourceBreak = b["break"].toBool();
            // The kernel is authoritative, including visual lyric edits.
            for (const auto &measure : score.measures)
                for (const auto &event : measure.events)
                    if (!event.grace && event.track == b["track"].toInt() &&
                        event.position == note.position)
                    {
                        note.lyrics.clear();
                        for (const auto &lyric : event.lyrics)
                            note.lyrics[lyric.verse] = lyric.text;
                    }
            break;
        }
    QVector<MusicalTime> starts;
    auto layout = JianpuLayout().layout(score, m_view);
    for (const auto &system : layout.systems)
        starts << system.start;
    auto aligned = format.alignedSource(parsed, starts);
    if (aligned == m_input["source"].toString() && m_input.contains("bindings"))
        return;
    auto updated = format.parse(aligned, m_view.value("inputHabits").toObject());
    if (updated.hasErrors())
        return;
    QJsonArray bindings, automaticBreaks;
    QJsonArray automaticBars;
    for (const auto &b : updated.boundaries)
    {
        bool wasExplicit = false;
        for (const auto &old : parsed.boundaries)
            wasExplicit |= !old.automatic && b.voice == old.voice && b.position == old.position;
        if (b.type == 1 && !wasExplicit)
            automaticBars << b.offset;
    }
    for (auto &note : updated.notes)
    {
        auto old = std::find_if(parsed.notes.begin(), parsed.notes.end(), [&](const auto &n)
                                { return n.voice == note.voice && n.position == note.position; });
        if (old != parsed.notes.end())
            note.id = old->id;
        if (note.sourceBreak && (old == parsed.notes.end() || !old->sourceBreak))
        {
            automaticBreaks << note.id;
            note.sourceBreak = false;
        }
        QJsonObject lyrics;
        for (auto it = note.lyrics.begin(); it != note.lyrics.end(); ++it)
            lyrics[QString::number(it.key())] = it.value();
        bindings << QJsonObject{{"id", note.id},
                                {"offset", note.offset},
                                {"length", note.length},
                                {"track", tracks.value(note.voice)},
                                {"n", note.position.n},
                                {"d", note.position.d},
                                {"break", note.sourceBreak},
                                {"lyrics", lyrics}};
    }
    m_input["autoBreakIds"] = automaticBreaks;
    m_input["automaticBarOffsets"] = automaticBars;
    m_input["source"] = aligned;
    m_input["bindings"] = bindings;
}
} // namespace jianpu
