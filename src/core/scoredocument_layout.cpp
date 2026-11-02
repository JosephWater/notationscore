// SPDX-License-Identifier: GPL-3.0-only
#include "layout/jianpulayout.h"
#include "layout/visualelement.h"
#include "presentation/scoreprojection.h"
#include "scoredocument.h"
#include <QJsonArray>
#include <algorithm>

namespace jianpu
{
const LayoutResult &ScoreDocument::presentation() const
{
    static const LayoutResult empty;
    return m_presentation ? *m_presentation : empty;
}
void ScoreDocument::refreshPresentation()
{
    m_presentation = std::make_unique<LayoutResult>(m_projection->layout(snapshot(), m_view));
}
bool ScoreDocument::transformElement(const QString &id, double rotation, double scale,
                                     const QString &mergeKey)
{
    auto element = presentation().elements.value(id);
    if (!element || !element->identity().movable)
        return fail(tr("请选择音符和歌词以外的图形元素"));
    auto view = m_view;
    auto offsets = view["elementOffsets"].toObject();
    auto value = offsets[element->identity().key].toObject();
    value["rotation"] = std::clamp(rotation, -360.0, 360.0);
    value["scale"] = std::clamp(scale, 0.25, 4.0);
    offsets[element->identity().key] = value;
    view["elementOffsets"] = offsets;
    setViewSettings(view, mergeKey);
    return true;
}
bool ScoreDocument::moveElement(const QString &id, QPointF delta, const QString &mergeKey)
{
    auto element = presentation().elements.value(id);
    if (!element || !element->identity().movable)
        return fail(tr("此元素的位置由音乐时间决定，请选择标题、记号或其他非音符元素。"));
    auto view = m_view;
    auto offsets = view["elementOffsets"].toObject();
    auto key = element->identity().key;
    auto old = offsets.value(key).toObject();
    old["x"] = old["x"].toDouble() + delta.x();
    old["y"] = old["y"].toDouble() + delta.y();
    offsets[key] = old;
    view["elementOffsets"] = offsets;
    setViewSettings(view, mergeKey);
    return true;
}
void ScoreDocument::syncSourceLayout()
{
    for (int pass = 0; pass < 8; ++pass)
        if (!syncSourceLayoutPass())
            return;
}
bool ScoreDocument::syncSourceLayoutPass()
{
    if (!m_project || m_input.contains("draftSource"))
        return false;
    auto score = snapshot();
    const auto &format = m_projection->format();
    const bool projected = !m_input.contains("source") || m_input.value("protected").toBool();
    auto source = m_input.value("source").toString();
    if (projected)
    {
        source = format.fromSnapshot(score);
        m_input["protected"] = true;
        m_input["format"] = format.formatId();
    }
    auto parsed = projected ? format.parse(source, m_view.value("inputHabits").toObject())
                            : parseText(source);
    if (!parsed.canProject())
        return false;
    if (!projected && m_input.contains("overflow"))
        parsed.restoreOverflow(m_input["overflow"].toArray());
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
    if (projected)
        for (auto &note : parsed.notes)
            for (const auto &measure : score.measures)
                for (const auto &event : measure.events)
                    if (!event.grace && event.track == tracks.value(note.voice) &&
                        event.position == note.position)
                        note.id = "native:" + event.id;
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
                        auto oldLyrics = note.lyrics;
                        note.lyrics.clear();
                        for (const auto &lyric : event.lyrics)
                            note.lyrics[lyric.verse] = lyric.text;
                        parsed.rewriteLyrics |= oldLyrics != note.lyrics;
                    }
            break;
        }
    for (auto it = tracks.begin(); it != tracks.end(); ++it)
    {
        int count = score.lyricRows.value(it.value());
        parsed.rewriteLyrics |= count > parsed.verseRows.value(it.key());
        parsed.verseRows[it.key()] = std::max(count, parsed.verseRows.value(it.key()));
    }
    auto layout = m_projection->layout(score, m_view);
    auto aligned = m_projection->align(parsed, layout);
    if (aligned == m_input["source"].toString() && m_input.contains("bindings"))
    {
        updateInputDiagnostics(parsed);
        return false;
    }
    auto updated = format.parse(aligned, m_view.value("inputHabits").toObject());
    if (!updated.canProject())
        return false;
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
    m_input["overflow"] = updated.overflowJson();
    updateInputDiagnostics(updated);
    return true;
}
void ScoreDocument::updateInputDiagnostics(const TextParseResult &updated)
{
    // Reflow changes source line numbers. Recompute locations while keeping
    // lyrics detached by deletions, which intentionally have no source token.
    QStringList unassigned;
    for (auto value : m_input["pendingLyrics"].toArray())
        unassigned << value.toString();
    for (const auto &word : updated.pendingLyrics)
        unassigned.removeOne(word);
    auto messages = updated.messages();
    if (!unassigned.isEmpty())
        messages << tr("已删除音符的歌词待分配：%1").arg(unassigned.join(" "));
    m_input["messages"] = QJsonArray::fromStringList(messages);
    m_input["hasErrors"] = updated.hasErrors() || !unassigned.isEmpty();
}
} // namespace jianpu
