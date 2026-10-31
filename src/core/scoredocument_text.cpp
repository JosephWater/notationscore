// SPDX-License-Identifier: GPL-3.0-only
#include "engraving/dom/chord.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/segment.h"
#include "engraving/engravingproject.h"
#include "engraving/infrastructure/eidregister.h"
#include "engraving/infrastructure/mscreader.h"
#include "engraving/infrastructure/mscwriter.h"
#include "global/internal/baseapplication.h"
#include "global/io/buffer.h"
#include "layout/jianpulayout.h"
#include "presentation/scoreprojection.h"
#include "scoredocument.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUuid>

namespace jianpu
{
TextParseResult ScoreDocument::parseText(const QString &source, const SourceChanges &changes) const
{
    auto habits = m_view.value("inputHabits").toObject();
    auto old = m_input["source"].toString();
    int prefix = 0, suffix = 0;
    while (prefix < std::min(old.size(), source.size()) && old[prefix] == source[prefix])
        ++prefix;
    while (suffix < std::min(old.size(), source.size()) - prefix &&
           old[old.size() - 1 - suffix] == source[source.size() - 1 - suffix])
        ++suffix;
    QJsonArray offsets;
    for (auto value : m_input.value("automaticBarOffsets").toArray())
    {
        int p = value.toInt();
        if (changes.before == old && !changes.edits.isEmpty())
        {
            bool alive = true;
            for (const auto &edit : changes.edits)
            {
                if (p >= edit.offset && p < edit.offset + edit.removed)
                {
                    alive = false;
                    break;
                }
                if (p >= edit.offset + edit.removed)
                    p += edit.added - edit.removed;
            }
            if (alive)
                offsets << p;
        }
        else if (p < prefix)
            offsets << p;
        else if (p >= old.size() - suffix)
            offsets << p + source.size() - old.size();
    }
    habits["automaticBarOffsets"] = offsets;
    return FanqieFormat().parse(source, habits);
}
namespace
{
QStringList lyricLines(const QString &source)
{
    QStringList result;
    for (const auto &line : source.split('\n'))
        if (QRegularExpression("^\\s*C\\d*\\s*[:：]").match(line).hasMatch())
            result << line;
    return result;
}
QJsonObject binding(const TextNote &n, int track)
{
    QJsonObject lyrics;
    for (auto it = n.lyrics.begin(); it != n.lyrics.end(); ++it)
        lyrics[QString::number(it.key())] = it.value();
    return {
        {"id", n.id},        {"offset", n.offset}, {"length", n.length},     {"track", track},
        {"n", n.position.n}, {"d", n.position.d},  {"break", n.sourceBreak}, {"lyrics", lyrics}};
}
} // namespace
QString ScoreDocument::sourceText() const
{
    if (m_input.contains("draftSource"))
        return m_input["draftSource"].toString();
    if (m_input.value("source").isString())
        return m_input.value("source").toString();
    auto score = snapshot();
    const auto &format = m_projection->format();
    auto projection = format.fromSnapshot(score);
    projection = m_projection->align(format.parse(projection), m_projection->layout(score, m_view));
    projection +=
        QStringLiteral("# 原生乐谱的乐器定义和扩展对象受保护，请在谱面中继续编辑音乐正文。\n");
    return projection;
}
TextParseResult ScoreDocument::analyzeText(const QString &source,
                                           const SourceChanges &changes) const
{
    return parseText(source, changes);
}
TextParseResult ScoreDocument::textSnapshot() const { return parseText(sourceText()); }
QStringList ScoreDocument::inputMessages() const
{
    QStringList out;
    for (auto v : m_input.value("messages").toArray())
        out << v.toString();
    return out;
}
void ScoreDocument::setInputState(const QJsonObject &state)
{
    // Cursor/focus are session state, not musical edits. Preferences belong to viewSettings.
    m_input = state;
}
void ScoreDocument::storeInputDraft(const QString &source, const QString &error,
                                    const QString &mergeKey)
{
    if (m_input.value("draftSource").toString() == source)
        return;
    auto before = historyState();
    m_input["draftSource"] = source;
    m_input["hasErrors"] = true;
    m_input["messages"] = QJsonArray{error};
    recordChange(before, false, mergeKey);
    notify();
}
void ScoreDocument::applyBindings(TextParseResult &parsed, const SourceChanges &changes) const
{
    auto oldSource = m_input.value("source").toString();
    auto bindings = m_input.value("bindings").toArray();
    int prefix = 0;
    while (prefix < std::min(oldSource.size(), parsed.source.size()) &&
           oldSource[prefix] == parsed.source[prefix])
        ++prefix;
    int suffix = 0;
    while (suffix < std::min(oldSource.size(), parsed.source.size()) - prefix &&
           oldSource[oldSource.size() - 1 - suffix] ==
               parsed.source[parsed.source.size() - 1 - suffix])
        ++suffix;
    int delta = parsed.source.size() - oldSource.size();
    const bool exact = changes.before == oldSource && !changes.edits.isEmpty();
    bool preserveLyrics = lyricLines(oldSource) == lyricLines(parsed.source);
    struct OverflowOwner
    {
        TextParseResult::Overflow value;
        QString anchor;
    };
    QVector<OverflowOwner> overflowOwners;
    if (preserveLyrics && !bindings.isEmpty())
    {
        auto oldParsed = parseText(oldSource);
        for (const auto &overflow : oldParsed.overflow)
        {
            QString anchor;
            for (const auto &n : oldParsed.notes)
                if (n.voice == overflow.voice && n.segment == overflow.segment)
                    for (auto value : bindings)
                    {
                        auto b = value.toObject();
                        if (b["offset"].toInt() == n.offset)
                            anchor = b["id"].toString();
                    }
            overflowOwners << OverflowOwner{overflow, anchor};
        }
        parsed.overflow.clear();
        parsed.pendingLyrics.clear();
        for (auto v : m_input.value("pendingLyrics").toArray())
            parsed.pendingLyrics << v.toString();
        for (int i = parsed.diagnostics.size() - 1; i >= 0; --i)
            if (parsed.diagnostics[i].message.contains(QStringLiteral("歌词词位待分配")))
                parsed.diagnostics.removeAt(i);
    }

    QSet<QString> used;
    for (auto &note : parsed.notes)
    {
        QJsonObject found;
        for (auto v : bindings)
        {
            auto b = v.toObject();
            int offset = b["offset"].toInt();
            bool same = (offset + b["length"].toInt() <= prefix && offset == note.offset) ||
                        (offset >= oldSource.size() - suffix && offset + delta == note.offset);
            if (exact)
            {
                bool deleted = false;
                int end = offset + b["length"].toInt();
                for (const auto &edit : changes.edits)
                {
                    if (edit.removed && edit.offset <= offset &&
                        edit.offset + edit.removed >= end && edit.added == 0)
                        deleted = true;
                    auto map = [&](int value, bool right)
                    {
                        if (value < edit.offset || (value == edit.offset && !right))
                            return value;
                        if (value >= edit.offset + edit.removed)
                            return value + edit.added - edit.removed;
                        return edit.offset;
                    };
                    offset = map(offset, true);
                    end = map(end, false);
                }
                same = !deleted && offset == note.offset;
            }
            if (same && !used.contains(b["id"].toString()))
            {
                found = b;
                break;
            }
        }
        // A single-token replacement keeps its identity (pitch, octave and duration edits).
        if (found.isEmpty() && !exact)
        {
            QVector<QJsonObject> oldChanged;
            QVector<int> newChanged;
            for (auto v : bindings)
            {
                auto b = v.toObject();
                int a = b["offset"].toInt();
                if (a + b["length"].toInt() > prefix && a < oldSource.size() - suffix)
                    oldChanged << b;
            }
            for (int i = 0; i < parsed.notes.size(); ++i)
            {
                const auto &n = parsed.notes[i];
                if (n.offset + n.length > prefix && n.offset < parsed.source.size() - suffix)
                    newChanged << i;
            }
            if (oldChanged.size() == 1 && newChanged.size() == 1 &&
                parsed.notes[newChanged[0]].offset == note.offset)
                found = oldChanged[0];
        }
        if (!found.isEmpty())
        {
            note.id = found["id"].toString();
            if (m_input.value("autoBreakIds").toArray().contains(note.id))
                note.sourceBreak = false;
            used.insert(note.id);
        }
        if (preserveLyrics && !bindings.isEmpty())
        {
            note.lyrics.clear();
            auto lyrics = found["lyrics"].toObject();
            for (auto it = lyrics.begin(); it != lyrics.end(); ++it)
            {
                if (note.degree == 0 || note.degree == 8 || note.tieBack)
                {
                    if (!it.value().toString().isEmpty())
                        parsed.pendingLyrics << it.value().toString();
                }
                else
                    note.lyrics[it.key().toInt()] = it.value().toString();
            }
        }
    }
    if (preserveLyrics)
        for (auto v : bindings)
        {
            auto b = v.toObject();
            if (!used.contains(b["id"].toString()))
                for (auto word : b["lyrics"].toObject())
                    if (!word.toString().isEmpty())
                        parsed.pendingLyrics << word.toString();
        }
    for (auto owner : overflowOwners)
    {
        for (const auto &n : parsed.notes)
            if (n.id == owner.anchor)
                owner.value.segment = n.segment;
        parsed.overflow << owner.value;
    }
    if (preserveLyrics && !parsed.pendingLyrics.isEmpty())
        parsed.diagnostics.push_back({0, 1, 1, 1,
                                      tr("歌词待分配：%1").arg(parsed.pendingLyrics.join(" ")),
                                      true, InputDiagnostic::Category::UnassignedLyrics});
}
bool ScoreDocument::applyText(const QString &source, const QString &mergeKey,
                              const SourceChanges &changes)
{
    if (!m_input.contains("draftSource") && source == m_input.value("source").toString() &&
        m_input.contains("bindings") &&
        m_input.value("habits").toObject() == m_view.value("inputHabits").toObject())
        return true;
    const auto previous = historyState();
    auto parsed = parseText(source, changes);
    if (parsed.fatal)
        return fail(parsed.messages().join("\n"));
    bool protectedContent = m_input.value("protected").toBool();
    // A snapshot does not contain complete instrument definitions. Native imports remain
    // editable through engine transactions, and text headers can be changed on a native clone.
    if (!m_input.contains("source"))
        protectedContent = true;
    if (protectedContent)
    {
        auto body = [](const QString &s)
        {
            QStringList lines;
            for (auto line : s.split('\n'))
                if (QRegularExpression("^\\s*[QC]\\d*\\s*[:：]").match(line).hasMatch())
                    lines << line;
            return lines;
        };
        const auto baseline = m_input.contains("source") ? m_input["source"].toString()
                                                         : FanqieFormat().fromSnapshot(snapshot());
        if (body(source) != body(baseline))
            return fail(tr("此曲谱含受保护对象，请使用谱面编辑；文字修改不会覆盖原始乐谱。"));
        // Clone the complete native score for metadata edits, preserving every imported object.
        using namespace mu::engraving;
        auto app = muse::modularity::globalIoc()->resolve<muse::IApplication>("jianpu");
        auto clone = EngravingProject::create(app->contexts().front());
        auto buffer = muse::io::Buffer::opened(muse::io::IODevice::WriteOnly);
        MscWriter::Params wp;
        wp.device = &buffer;
        wp.filePath = "score.mscz";
        MscWriter writer(wp);
        if (!writer.open() || !m_project->writeMscz(writer, false))
            return fail(tr("无法复制受保护曲谱"));
        writer.close();
        auto data = buffer.data();
        muse::io::Buffer readerBuffer(&data);
        readerBuffer.open(muse::io::IODevice::ReadOnly);
        MscReader::Params rp;
        rp.device = &readerBuffer;
        rp.filePath = "score.mscz";
        MscReader reader(rp);
        if (!reader.open() || !clone->loadMscz(reader, nullptr, false) ||
            !clone->setupMasterScore(false))
            return fail(tr("无法恢复受保护曲谱"));
        ScoreDocument staging;
        staging.m_project = clone;
        auto old = staging.snapshot();
        if (old.keyFifths != parsed.fifths || old.minor != parsed.minor)
            if (!staging.edit({EditRequest::Kind::SetKey, {}, parsed.fifths, parsed.minor ? 1 : 0}))
                return fail(staging.error());
        if (!old.measures.isEmpty() && (old.measures[0].numerator != parsed.numerator ||
                                        old.measures[0].denominator != parsed.denominator))
            if (!staging.edit(
                    {EditRequest::Kind::SetTime, {}, parsed.numerator, parsed.denominator}))
                return fail(staging.error());
        m_project = clone;
    }
    else
    {
        applyBindings(parsed, changes);
        auto xml = FanqieFormat().musicXml(parsed);
        if (xml.isEmpty())
            return fail(tr("时值过于复杂，无法在内核中精确表示；输入已保留，请调整连音比例。"));
        QTemporaryDir dir;
        QFile file(dir.filePath("input.musicxml"));
        if (!file.open(QIODevice::WriteOnly) || file.write(xml) != xml.size())
            return fail(tr("无法创建文字输入临时曲谱"));
        file.close();
        ScoreDocument candidate;
        if (!candidate.open(file.fileName()))
            return fail(tr("输入未写入，乐谱保持完整：%1").arg(candidate.error()));
        m_project = candidate.m_project;
    }
    auto *score = m_project->masterScore();
    for (auto *measure = score->firstMeasure(); measure; measure = measure->nextMeasure())
        for (auto *segment = measure->first(); segment; segment = segment->next())
            for (auto *element : segment->elist())
                if (element && element->isChord())
                    for (auto *note : mu::engraving::toChord(element)->notes())
                        if (note->headGroup() == mu::engraving::NoteHeadGroup::HEAD_CROSS)
                            note->setPlay(false);
    m_input.remove("draftSource");
    score->setMetaTag(u"workTitle", muse::String::fromQString(parsed.title));
    score->setMetaTag(u"jianpuSubtitles",
                      muse::String::fromQString(QString::fromUtf8(
                          QJsonDocument(QJsonArray::fromStringList(parsed.subtitles))
                              .toJson(QJsonDocument::Compact))));
    score->setMetaTag(u"jianpuAuthors",
                      muse::String::fromQString(QString::fromUtf8(
                          QJsonDocument(QJsonArray::fromStringList(parsed.authors))
                              .toJson(QJsonDocument::Compact))));
    QJsonArray bindings;
    int track = 0;
    QMap<int, int> tracks;
    for (auto it = parsed.voiceNames.begin(); it != parsed.voiceNames.end(); ++it)
    {
        tracks[it.key()] = track;
        track += 4;
    }
    for (const auto &note : parsed.notes)
        bindings << binding(note, tracks.value(note.voice));
    QJsonObject verseRows;
    for (auto it = parsed.verseRows.begin(); it != parsed.verseRows.end(); ++it)
        verseRows[QString::number(tracks.value(it.key()))] = it.value();
    m_input["verseRows"] = verseRows;
    QJsonArray pending;
    for (const auto &span : parsed.spans)
        if (!span.closed && span.first < parsed.notes.size())
            pending << QJsonObject{{"kind", span.kind},
                                   {"track", tracks.value(parsed.notes[span.first].voice)},
                                   {"tick", parsed.notes[span.first].position.ticks()}};
    m_input["habits"] = m_view.value("inputHabits").toObject();
    QJsonArray automaticBars;
    for (const auto &b : parsed.boundaries)
        if (b.automatic)
            automaticBars << b.offset;
    m_input["automaticBarOffsets"] = automaticBars;
    m_input["format"] = "fanqie-1.0";
    m_input["source"] = source;
    m_input["bindings"] = bindings;
    m_input["protected"] = protectedContent;
    m_input["hasErrors"] = parsed.hasErrors() || !parsed.pendingLyrics.isEmpty();
    m_input["messages"] = QJsonArray::fromStringList(parsed.messages());
    m_input["pendingSpans"] = pending;
    m_input["pendingLyrics"] = QJsonArray::fromStringList(parsed.pendingLyrics);
    m_input["overflow"] = parsed.overflowJson();
    if (!parsed.pendingLyrics.isEmpty() && !parsed.hasErrors())
    {
        auto messages = m_input["messages"].toArray();
        messages << tr("已删除音符的歌词待分配：%1").arg(parsed.pendingLyrics.join(" "));
        m_input["messages"] = messages;
    }
    syncSourceLayout();
    recordChange(previous, false, mergeKey);
    notify();
    return true;
}
void ScoreDocument::syncSourceAfterEdit(const EditRequest &request, const ScoreSnapshot &before)
{
    if (!m_input.contains("source"))
        return;
    using K = EditRequest::Kind;
    auto protect = [&]
    {
        m_input["protected"] = true;
        auto messages = m_input["messages"].toArray();
        messages << tr("此谱面操作包含文字稿不能完整表示的对象，已保护原始曲谱；可继续谱面编辑。");
        m_input["messages"] = messages;
    };
    if (request.kind == K::AddChordNote || request.kind == K::Paste ||
        request.kind == K::AddVoice || request.kind == K::AddPart ||
        request.kind == K::AppendMeasure || request.kind == K::DeleteMeasure)
    {
        protect();
        return;
    }
    auto source = m_input["source"].toString();
    auto parsed = parseText(source);
    struct Replacement
    {
        int start, length;
        QString text;
    };
    QVector<Replacement> replacements;
    auto after = snapshot();
    if (parsed.notes.isEmpty() && after.noteCount() > 0)
    {
        bool protectedContent = false;
        auto projection = FanqieFormat().fromSnapshot(after, &protectedContent);
        auto projected = FanqieFormat().parse(projection);
        QJsonArray bindings;
        for (auto n : projected.notes)
            bindings << binding(n, after.voices.value(n.voice - 1).track);
        m_input["source"] = projection;
        m_input["bindings"] = bindings;
        m_input["overflow"] = projected.overflowJson();
        m_input["protected"] = protectedContent;
        return;
    }
    auto selectedEvent = [&](bool last) -> const EventData *
    {
        const EventData *result = nullptr;
        for (const auto &measure : before.measures)
            for (const auto &event : measure.events)
            {
                bool selected = request.ids.contains(event.id);
                for (auto n : event.notes)
                    selected |= request.ids.contains(n.id);
                if (selected && event.sourceStart >= 0 &&
                    (!result || (last ? event.position > result->position
                                      : event.position < result->position)))
                    result = &event;
            }
        return result;
    };
    auto first = selectedEvent(false), last = selectedEvent(true);
    if (request.kind == K::AddMark)
    {
        if (!first)
        {
            protect();
            return;
        }
        auto kind = request.text;
        if (kind.startsWith("dynamic:"))
            replacements << Replacement{first->sourceStart + first->sourceLength, 0,
                                        "&" + kind.mid(8)};
        else if ((kind == "slur" || kind == "crescendo" || kind == "diminuendo") && last)
        {
            replacements << Replacement{first->sourceStart, 0,
                                        kind == "slur"        ? "("
                                        : kind == "crescendo" ? "<"
                                                              : ">"};
            replacements << Replacement{last->sourceStart + last->sourceLength, 0,
                                        kind == "slur" ? ")" : "!"};
        }
        else
        {
            protect();
            return;
        }
    }
    else if (request.kind == K::Grace)
    {
        if (!first)
        {
            protect();
            return;
        }
        for (const auto &measure : after.measures)
            for (const auto &event : measure.events)
                if (event.grace && event.track == first->track && event.tick == first->tick &&
                    !event.notes.isEmpty())
                {
                    auto note = event.notes.last();
                    int tonic = ((event.keyFifths * 4) % 7 + 7) % 7;
                    int relative = note.octave * 7 + note.step - 28 - tonic;
                    TextNote g;
                    g.degree = (relative % 7 + 7) % 7 + 1;
                    g.octave = int(std::floor(relative / 7.0));
                    replacements << Replacement{first->sourceStart + first->sourceLength, 0,
                                                "[" + FanqieFormat::token(g) + "]"};
                    break;
                }
    }
    else if (request.kind == K::UpdateMark || request.kind == K::RemoveMark)
    {
        protect();
        return;
    }
    if (request.kind == K::SetKey || request.kind == K::SetTime)
    {
        bool atStart =
            !first || first->position < MusicalTime(before.measures.value(0).duration, 1920);
        if (!atStart)
        {
            protect();
            return;
        }
        auto projected = FanqieFormat().fromSnapshot(after);
        QString field = request.kind == K::SetKey ? "D" : "P";
        auto re = QRegularExpression("^" + field + ":.*$", QRegularExpression::MultilineOption);
        auto old = re.match(source), current = re.match(projected);
        if (current.hasMatch())
        {
            if (old.hasMatch())
                replacements << Replacement{int(old.capturedStart()), int(old.capturedLength()),
                                            current.captured()};
            else
                replacements << Replacement{0, 0, current.captured() + "\n"};
        }
    }
    for (const auto &measure : before.measures)
        for (const auto &old : measure.events)
        {
            bool selected = request.ids.contains(old.id);
            for (const auto &n : old.notes)
                selected |= request.ids.contains(n.id);
            if (!selected || old.sourceStart < 0 || request.kind == K::AddMark ||
                request.kind == K::Grace || request.kind == K::SetKey || request.kind == K::SetTime)
                continue;
            const EventData *current = nullptr;
            for (const auto &m : after.measures)
                for (const auto &e : m.events)
                    if (!e.grace && e.track == old.track && e.position == old.position)
                        current = &e;
            if (!current)
                continue;
            for (auto &n : parsed.notes)
                if (n.offset == old.sourceStart)
                {
                    if (request.kind == K::Lyrics)
                    {
                        QString value;
                        for (const auto &l : current->lyrics)
                            if (l.verse == request.value)
                                value = l.text;
                        n.lyrics[request.value] = value;
                        QStringList words;
                        for (const auto &target : parsed.notes)
                            if (target.segment == n.segment && target.voice == n.voice &&
                                target.degree != 0 && target.degree != 8 && !target.tieBack)
                            {
                                auto word = target.lyrics.value(request.value, "@");
                                QStringList chars;
                                for (auto ch : word)
                                    chars << QString(ch);
                                words << chars.join("~");
                            }
                        int offset = 0, segment = 0, activeVoice = 1, lastEnd = -1;
                        bool written = false;
                        QMap<int, int> lastSegment, verse;
                        for (const auto &line : source.split('\n'))
                        {
                            auto q = QRegularExpression("^\\s*Q(\\d*)\\s*[:：]").match(line);
                            if (q.hasMatch())
                            {
                                ++segment;
                                if (!q.captured(1).isEmpty())
                                    activeVoice = q.captured(1).toInt();
                                lastSegment[activeVoice] = segment;
                                if (segment == n.segment)
                                    lastEnd = offset + line.size();
                            }
                            auto c = QRegularExpression("^\\s*C(\\d*)\\s*[:：]\\s*").match(line);
                            if (c.hasMatch())
                            {
                                int voice =
                                    c.captured(1).isEmpty() ? activeVoice : c.captured(1).toInt();
                                int seg = lastSegment.value(voice);
                                if (seg == n.segment && voice == n.voice)
                                {
                                    lastEnd = offset + line.size();
                                    if (verse[seg] == request.value)
                                    {
                                        replacements << Replacement{
                                            offset + int(c.capturedLength()),
                                            int(line.size() - c.capturedLength()), words.join(" ")};
                                        written = true;
                                    }
                                    ++verse[seg];
                                }
                            }
                            offset += line.size() + 1;
                        }
                        if (!written && lastEnd >= 0)
                        {
                            QString addition;
                            for (int v = verse[n.segment]; v < request.value; ++v)
                                addition += QString("\nC%1: ").arg(n.voice);
                            addition += QString("\nC%1: %2").arg(n.voice).arg(words.join(" "));
                            replacements << Replacement{lastEnd, 0, addition};
                        }
                    }
                    else if (request.kind == K::SetKey || request.kind == K::SetTime)
                    {
                        m_input["protected"] = true;
                    }
                    else
                    {
                        auto token = n;
                        n.degree = current->rest     ? (current->visible ? 0 : 8)
                                   : current->rhythm ? 9
                                                     : 1;
                        if (!current->notes.isEmpty())
                        {
                            auto note = current->notes[0];
                            int tonic = ((current->keyFifths * 4) % 7 + 7) % 7;
                            int relative = note.octave * 7 + note.step - 28 - tonic;
                            n.degree = current->rhythm ? 9 : (relative % 7 + 7) % 7 + 1;
                            n.octave = int(std::floor(relative / 7.0));
                            static const int sharp[]{3, 0, 4, 1, 5, 2, 6},
                                flat[]{6, 2, 5, 1, 4, 0, 3};
                            int keyAlter = 0;
                            for (int i = 0; i < std::abs(current->keyFifths); ++i)
                                if ((current->keyFifths > 0 ? sharp[i] : flat[i]) == note.step)
                                    keyAlter = current->keyFifths > 0 ? 1 : -1;
                            int delta = note.alter - keyAlter;
                            n.accidental = delta == 0        ? 99
                                           : note.alter == 0 ? 0
                                           : delta > 0       ? 1
                                                             : -1;
                        }
                        if (request.kind == K::Duration)
                        {
                            n.slashes = request.value >= 4 ? int(std::log2(request.value)) - 2 : 0;
                            n.extensions = request.value < 4 ? 4 / request.value - 1 : 0;
                            n.dots = request.secondary;
                            auto end = n.position + MusicalTime((1LL << (n.dots + 1)) - 1,
                                                                request.value * (1LL << n.dots));
                            MusicalTime next;
                            bool hasNext = false;
                            for (const auto &target : parsed.notes)
                                if (target.voice == n.voice && target.position > n.position)
                                {
                                    if (target.position < end)
                                        replacements
                                            << Replacement{target.offset, target.length, ""};
                                    else if (!hasNext || target.position < next)
                                    {
                                        next = target.position;
                                        hasNext = true;
                                    }
                                }
                            if (!hasNext)
                                for (const auto &m : parsed.measures)
                                    if (m.start <= n.position && m.start + m.duration > n.position)
                                    {
                                        next = m.start + m.duration;
                                        hasNext = true;
                                        break;
                                    }
                            QString fill;
                            if (hasNext && next > end)
                                for (const auto &m : after.measures)
                                    for (const auto &event : m.events)
                                        if (event.track == old.track && !event.grace &&
                                            event.position >= end && event.position < next)
                                        {
                                            if (!event.rest)
                                            {
                                                protect();
                                                return;
                                            }
                                            TextNote rest;
                                            rest.degree = event.visible ? 0 : 8;
                                            rest.slashes =
                                                event.denominator >= 4
                                                    ? int(std::log2(event.denominator)) - 2
                                                    : 0;
                                            rest.extensions =
                                                event.denominator < 4
                                                    ? std::max(0, int(event.duration / 480) - 1)
                                                    : 0;
                                            rest.dots = event.dots;
                                            fill += " " + FanqieFormat::token(rest);
                                        }
                            if (end > n.position + n.duration &&
                                (!parsed.spans.isEmpty() || !n.graces.isEmpty()))
                            {
                                protect();
                                return;
                            }
                            if (!fill.isEmpty())
                                replacements << Replacement{n.offset + n.length, 0, fill};
                        }
                        auto prefix =
                            QRegularExpression("^[0-9](?:[/\\-.,'#\\$=^]|\\s+(?=[/\\-.,'#\\$=^]))*")
                                .match(token.raw);
                        replacements << Replacement{token.offset, int(prefix.capturedLength()),
                                                    FanqieFormat::token(n)};
                    }
                }
        }
    std::sort(replacements.begin(), replacements.end(),
              [](auto a, auto b) { return a.start > b.start; });
    SourceChanges changes;
    changes.before = source;
    for (const auto &r : replacements)
    {
        changes.edits << SourceChange{r.start, r.length, int(r.text.size())};
        source.replace(r.start, r.length, r.text);
    }
    if (!replacements.isEmpty())
    {
        auto updated = parseText(source, changes);
        applyBindings(updated, changes);
        QJsonArray bindings;
        auto oldBindings = m_input["bindings"].toArray();
        QMap<int, int> tracks;
        for (const auto &old : parsed.notes)
            for (auto value : oldBindings)
            {
                auto b = value.toObject();
                if (b["offset"].toInt() == old.offset)
                {
                    tracks[old.voice] = b["track"].toInt();
                    break;
                }
            }
        for (const auto &n : updated.notes)
        {
            bindings << binding(n, tracks.value(n.voice, (n.voice - 1) * 4));
        }
        m_input["source"] = source;
        QJsonArray automaticBars;
        for (const auto &b : updated.boundaries)
            if (b.automatic)
                automaticBars << b.offset;
        m_input["automaticBarOffsets"] = automaticBars;
        m_input["bindings"] = bindings;
        m_input["overflow"] = updated.overflowJson();
        if (request.kind == K::Duration && first)
            for (const auto &boundary : parsed.boundaries)
                if (boundary.position > first->position &&
                    boundary.position < first->position + MusicalTime(1, request.value))
                {
                    protect();
                    break;
                }
        if (!updated.pendingLyrics.isEmpty())
        {
            m_input["pendingLyrics"] = QJsonArray::fromStringList(updated.pendingLyrics);
            m_input["hasErrors"] = true;
            m_input["messages"] = QJsonArray::fromStringList(updated.messages());
        }
    }
}
} // namespace jianpu
