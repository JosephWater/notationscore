// SPDX-License-Identifier: GPL-3.0-only
#include "engraving/dom/chord.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/lyrics.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/segment.h"
#include "engraving/editing/paste.h"
#include "engraving/editing/transaction/transaction.h"
#include "engraving/engravingproject.h"
#include "engraving/infrastructure/eidregister.h"
#include "engraving/internal/qmimedataadapter.h"
#include "scoredocument.h"
#include <QDomDocument>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>
#include <stdexcept>
namespace jianpu
{
namespace
{
using namespace mu::engraving;
QString eid(const EngravingObject *e) { return QString::fromStdString(e->eid().toStdString()); }
ChordRest *crFor(MasterScore *score, const QString &id)
{
    auto *e = score->eidRegister()->itemFromEID(EID::fromStdString(id.toStdString()));
    if (!e)
        return nullptr;
    return e->isNote() ? toNote(e)->chord() : e->isChordRest() ? toChordRest(e) : nullptr;
}
QVector<Measure *> measures(MasterScore *score)
{
    QVector<Measure *> result;
    for (auto *m = score->firstMeasure(); m; m = m->nextMeasure())
        result << m;
    return result;
}
ChordRest *at(MasterScore *, const Fraction &, int);
QJsonObject rangeData(MasterScore *score, const Fraction &start, const Fraction &end,
                      int track = -1, const QStringList &noteIds = {})
{
    auto filter = score->selectionFilter();
    score->selectionFilter().setFiltered(VoicesSelectionFilterTypes::ALL, track < 0);
    if (track >= 0)
        score->selectionFilter().setFiltered(
            static_cast<VoicesSelectionFilterTypes>(1u << (track % VOICES)), true);
    if (!noteIds.isEmpty())
    {
        score->selectionFilter().setFiltered(NotesInChordSelectionFilterTypes::ALL, true);
        score->selectionFilter().setFiltered(ElementsSelectionFilterTypes::LYRICS, false);
        score->selectionFilter().setFiltered(ElementsSelectionFilterTypes::GRACE_NOTE, false);
    }
    score->deselectAll();
    score->selection().setRangeTicks(start, end, track < 0 ? 0 : track / VOICES,
                                     track < 0 ? score->nstaves() : track / VOICES + 1);
    score->selection().updateSelectedElements();
    score->selection().update();
    auto bytes = score->selection().mimeData().toQByteArray();
    auto type = score->selection().mimeType().toQString();
    score->selectionFilter() = filter;
    if (bytes.isEmpty())
        return {};
    if (!noteIds.isEmpty())
    {
        QMap<int, int> pitches;
        if (auto *cr = at(score, start, track); cr && cr->isChord())
            for (auto *n : toChord(cr)->notes())
                if (noteIds.contains(eid(n)))
                    ++pitches[n->pitch()];
        QDomDocument document;
        if (!document.setContent(bytes))
            return {};
        auto notes = document.elementsByTagName("Note");
        for (int i = notes.size() - 1; i >= 0; --i)
        {
            auto note = notes.at(i);
            int pitch = note.firstChildElement("pitch").text().toInt();
            if (pitches.value(pitch) > 0)
                --pitches[pitch];
            else
                note.parentNode().removeChild(note);
        }
        bytes = document.toByteArray();
    }
    return {{"format", type},
            {"data", QString::fromLatin1(bytes.toBase64())},
            {"n", (end - start).numerator()},
            {"d", (end - start).denominator()},
            {"voice", track < 0 ? -1 : int(track % VOICES)}};
}
Fraction length(const QJsonObject &o) { return Fraction(o["n"].toInt(), o["d"].toInt(1)); }
void check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
ChordRest *at(MasterScore *score, const Fraction &time, int track)
{
    auto *m = score->tick2measure(time);
    return m ? m->findChordRest(time, track) : nullptr;
}
void ensureEnd(MasterScore *score, const Fraction &end)
{
    int guard = 0;
    while (score->lastMeasure()->endTick() < end && ++guard < 10000)
    {
        Score::InsertMeasureOptions options;
        options.createMeasureRests = false;
        score->insertMeasure(nullptr, options);
    }
}
QByteArray remapVoice(const QByteArray &data, int difference)
{
    if (!difference)
        return data;
    QXmlStreamReader in(data);
    QByteArray output;
    QXmlStreamWriter out(&output);
    bool staff = false, adjusted = false, location = false, voices = false;
    int depth = 0, staffDepth = 0;
    while (!in.atEnd())
    {
        auto token = in.readNext();
        if (token == QXmlStreamReader::StartElement)
        {
            ++depth;
            auto name = in.name().toString();
            if (name == "Staff")
            {
                staff = true;
                staffDepth = depth;
                adjusted = false;
            }
            if (staff && depth == staffDepth + 1 && !adjusted &&
                (name == "Chord" || name == "Rest" || name == "Tuplet"))
            {
                out.writeStartElement("location");
                out.writeTextElement("voices", QString::number(difference));
                out.writeEndElement();
                adjusted = true;
            }
            if (staff && name == "location" && depth == staffDepth + 1 && !adjusted)
            {
                location = true;
                voices = false;
            }
            if ((location && name == "voices") || name == "track" || name == "track2")
            {
                int value = in.readElementText().toInt();
                out.writeTextElement(name, QString::number(value + difference));
                if (location && name == "voices")
                    voices = true;
                --depth;
                continue;
            }
        }
        if (token == QXmlStreamReader::EndElement)
        {
            if (location && in.name() == "location")
            {
                if (!voices)
                    out.writeTextElement("voices", QString::number(difference));
                location = false;
                adjusted = true;
            }
            if (in.name() == "Staff")
                staff = false;
        }
        if (token != QXmlStreamReader::Invalid)
            out.writeCurrentToken(in);
        if (token == QXmlStreamReader::EndElement)
            --depth;
    }
    check(!in.hasError(), "剪贴板音乐内容损坏");
    return output;
}
void paste(MasterScore *score, const QJsonObject &data, const Fraction &time, int track,
           Transaction &tx)
{
    auto *target = at(score, time, track);
    check(target, "粘贴起点不在可编辑的节奏位置");
    score->deselectAll();
    score->select(target);
    QMimeData mime;
    auto bytes = QByteArray::fromBase64(data["data"].toString().toLatin1());
    int voice = data["voice"].toInt(-1);
    if (voice >= 0)
        bytes = remapVoice(bytes, int(track % VOICES) - voice);
    mime.setData(data["format"].toString(), bytes);
    QMimeDataAdapter adapter(&mime);
    check(Paste::paste(tx, score, &adapter, nullptr), "片段无法粘贴到此位置，请完整选择连音组");
}
void clear(MasterScore *score, int track, const Fraction &start, const Fraction &end)
{
    if (start >= end)
        return;
    auto filter = score->selectionFilter();
    score->selectionFilter().setFiltered(VoicesSelectionFilterTypes::ALL, false);
    score->selectionFilter().setFiltered(
        static_cast<VoicesSelectionFilterTypes>(1u << (track % VOICES)), true);
    score->deselectAll();
    score->selection().setRangeTicks(start, end, track / VOICES, track / VOICES + 1);
    score->selection().updateSelectedElements();
    score->selection().update();
    score->cmdDeleteSelection();
    score->selectionFilter() = filter;
}
QVector<ChordRest *> voiceEvents(MasterScore *score, int track)
{
    QVector<ChordRest *> result;
    for (auto *m = score->firstMeasure(); m; m = m->nextMeasure())
        for (auto *s = m->first(); s; s = s->next())
            if (s->isChordRestType())
                if (auto *e = s->element(track); e && e->isChordRest())
                    result << toChordRest(e);
    return result;
}
} // namespace
QByteArray ScoreDocument::copyFragment(const ScoreSelection &selection) const
{
    if (!m_project)
        return {};
    auto *score = m_project->masterScore();
    QJsonObject root{{"version", 1},
                     {"track", selection.track},
                     {"verse", selection.verse},
                     {"textOwned", m_input.value("textOrigin").toBool(snapshot().textBacked)}};
    QJsonArray blocks, ids, fullIds, partialIds;
    if (selection.kind == SelectionKind::Lyrics)
    {
        root["kind"] = "lyrics";
        QJsonArray words;
        QVector<ChordRest *> notes;
        for (const auto &id : selection.ids)
            if (auto *cr = crFor(score, id); cr && cr->isChord() && !notes.contains(cr))
                notes << cr;
        std::sort(notes.begin(), notes.end(),
                  [](auto *a, auto *b) { return a->tick() < b->tick(); });
        for (auto *cr : notes)
        {
            QString word;
            for (auto *l : cr->lyrics())
                if (l->verse() == selection.verse)
                    word = l->plainText().toQString();
            words << word;
            ids << eid(cr);
        }
        root["words"] = words;
    }
    else if (selection.kind == SelectionKind::Measures)
    {
        root["kind"] = "measures";
        auto all = measures(score);
        auto indices = selection.measures;
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        for (int index : indices)
            if (index >= 0 && index < all.size())
            {
                auto *m = all[index];
                auto block = rangeData(score, m->tick(), m->endTick());
                if (block.isEmpty())
                    return {};
                block["num"] = m->timesig().numerator();
                block["den"] = m->timesig().denominator();
                block["repeatStart"] = m->repeatStart();
                block["repeatEnd"] = m->repeatEnd();
                block["barline"] = int(m->endBarLineType());
                blocks << block;
                ids << eid(m);
            }
    }
    else if (selection.kind == SelectionKind::Notes)
    {
        root["kind"] = "notes";
        QVector<ChordRest *> notes;
        for (const auto &id : selection.ids)
            if (auto *cr = crFor(score, id); cr && !notes.contains(cr))
                notes << cr;
        std::sort(notes.begin(), notes.end(),
                  [](auto *a, auto *b) { return a->tick() < b->tick(); });
        int track = notes.isEmpty() ? selection.track : int(notes.first()->track());
        root["track"] = track;
        Fraction groupStart(-1, 1), groupEnd;
        auto appendBlock = [&](const Fraction &start, const Fraction &end,
                               const QStringList &tones = QStringList{})
        {
            auto block = rangeData(score, start, end, track, tones);
            if (block.isEmpty())
                return false;
            block["sourceN"] = start.numerator();
            block["sourceD"] = start.denominator();
            blocks << block;
            return true;
        };
        auto flush = [&]
        {
            if (groupStart < Fraction(0, 1))
                return true;
            bool ok = appendBlock(groupStart, groupEnd);
            groupStart = Fraction(-1, 1);
            return ok;
        };
        for (auto *cr : notes)
        {
            if (int(cr->track()) != track)
                return {};
            if (cr->actualTicks() <= Fraction(0, 1))
                return {};
            QStringList selectedNotes;
            if (cr->isChord() && !selection.ids.contains(eid(cr)))
            {
                for (auto *n : toChord(cr)->notes())
                    if (selection.ids.contains(eid(n)))
                        selectedNotes << eid(n);
                if (selectedNotes.size() == int(toChord(cr)->notes().size()))
                    selectedNotes.clear();
            }
            if (selectedNotes.isEmpty())
            {
                fullIds << eid(cr);
                if (groupStart >= Fraction(0, 1) && cr->tick() != groupEnd && !flush())
                    return {};
                if (groupStart < Fraction(0, 1))
                    groupStart = cr->tick();
                groupEnd = cr->tick() + cr->actualTicks();
            }
            else
            {
                if (!flush() ||
                    !appendBlock(cr->tick(), cr->tick() + cr->actualTicks(), selectedNotes))
                    return {};
                for (const auto &id : selectedNotes)
                    partialIds << id;
            }
            ids << eid(cr);
        }
        if (!flush())
            return {};
    }
    else
        return {};
    root["blocks"] = blocks;
    root["ids"] = ids;
    root["fullIds"] = fullIds;
    root["partialIds"] = partialIds;
    if (ids.isEmpty())
        return {};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
bool ScoreDocument::pasteFragment(const QByteArray &bytes, const ScoreDropTarget &target,
                                  bool insert, bool move)
{
    auto fragment = QJsonDocument::fromJson(bytes).object();
    if (fragment["version"].toInt() != 1 || !m_project)
        return fail(tr("无效的乐谱片段"));
    auto kind = fragment["kind"].toString();
    if (m_input.contains("draftSource"))
        return fail(tr("文字稿仍有未提交的修改，请完成当前输入后再搬移或粘贴"));
    if (!QStringList{"notes", "measures", "lyrics", "deleteNotes", "deleteMeasures"}.contains(kind))
        return fail(tr("未知乐谱片段类型"));
    if (target.position < MusicalTime() || target.track < 0 ||
        target.track >= int(m_project->masterScore()->nstaves() * VOICES) || target.verse < 0)
        return fail(tr("粘贴目标无效"));
    if (kind == "lyrics")
    {
        if (fragment["words"].toArray().isEmpty())
            return fail(tr("片段没有歌词内容"));
    }
    else
    {
        auto blocks = fragment["blocks"].toArray();
        if (blocks.isEmpty())
            return fail(tr("片段没有音乐内容"));
        for (auto value : blocks)
        {
            auto block = value.toObject();
            QDomDocument xml;
            if (block["n"].toInt() <= 0 || block["d"].toInt() <= 0 ||
                block["format"].toString() != QStringLiteral("application/musescore/stafflist") ||
                !xml.setContent(QByteArray::fromBase64(block["data"].toString().toLatin1())) ||
                xml.documentElement().tagName() != QStringLiteral("StaffList"))
                return fail(tr("剪贴板音乐内容损坏"));
        }
    }
    auto previous = historyState();
    auto oldSnapshot = snapshot();
    for (const auto &voice : oldSnapshot.voices)
        if (voice.track == target.track && voice.percussion)
            return fail(tr("暂不支持粘贴到打击乐声部"));
    int last = -1;
    for (int index : target.measures)
    {
        if (index <= last || index < 0 || index >= oldSnapshot.measures.size())
            return fail(tr("覆盖小节范围无效"));
        last = index;
    }
    QMap<int, MusicalTime> limits;
    for (const auto &m : oldSnapshot.measures)
        for (const auto &e : m.events)
            if (!e.grace && (!oldSnapshot.textBacked || !e.logicalId.isEmpty()))
                limits[e.track] = std::max(limits.value(e.track), e.position + e.length);
    auto clone = cloneProject();
    if (!clone)
        return false;
    ScoreDocument staging;
    staging.m_project = clone;
    staging.m_input = m_input;
    staging.m_view = m_view;
    auto *score = clone->masterScore();
    try
    {
        if (kind == "lyrics")
        {
            auto snap = staging.snapshot();
            QVector<EventData> notes;
            for (const auto &m : snap.measures)
                for (const auto &e : m.events)
                    if (!e.rest && !e.grace && e.track == target.track &&
                        std::any_of(e.notes.begin(), e.notes.end(),
                                    [](const auto &n) { return !n.tieEnd; }))
                        notes << e;
            QStringList words;
            for (const auto &e : notes)
            {
                QString word;
                for (const auto &l : e.lyrics)
                    if (l.verse == target.verse)
                        word = l.text;
                words << word;
            }
            int index = notes.size();
            for (int i = 0; i < notes.size(); ++i)
                if (notes[i].position >= target.position)
                {
                    index = i;
                    break;
                }
            auto ids = fragment["ids"].toArray();
            int sourceTrack = fragment["track"].toInt(), sourceVerse = fragment["verse"].toInt();
            if (move && sourceTrack == target.track && sourceVerse == target.verse)
            {
                if (index < notes.size() && ids.contains(notes[index].id))
                    return true;
                for (int i = notes.size() - 1; i >= 0; --i)
                    if (ids.contains(notes[i].id))
                    {
                        words.removeAt(i);
                        if (i < index)
                            --index;
                    }
            }
            else if (move)
            {
                QMap<QString, QString> from;
                QVector<EventData> sourceNotes;
                QStringList sourceWords;
                for (const auto &m : snap.measures)
                    for (const auto &e : m.events)
                        if (!e.rest && !e.grace && e.track == sourceTrack &&
                            std::any_of(e.notes.begin(), e.notes.end(),
                                        [](const auto &n) { return !n.tieEnd; }))
                        {
                            QString word;
                            for (const auto &l : e.lyrics)
                                if (l.verse == sourceVerse)
                                    word = l.text;
                            sourceNotes << e;
                            if (!ids.contains(e.id))
                                sourceWords << word;
                        }
                for (int i = 0; i < sourceNotes.size(); ++i)
                    from[sourceNotes[i].id] = sourceWords.value(i);
                check(staging.setLyrics(sourceTrack, sourceVerse, from), "无法搬移原歌词");
            }
            auto incoming = fragment["words"].toArray();
            for (auto word : incoming)
            {
                if (insert)
                    words.insert(index, word.toString());
                else
                {
                    while (words.size() <= index)
                        words << QString();
                    words[index] = word.toString();
                }
                ++index;
            }
            QMap<QString, QString> to;
            for (int i = 0; i < notes.size(); ++i)
                to[notes[i].id] = words.value(i);
            QStringList extra;
            for (int i = notes.size(); i < words.size(); ++i)
                if (!words[i].isEmpty())
                    extra << words[i];
            check(staging.setLyrics(target.track, target.verse, to, extra), "无法粘贴歌词");
        }
        else
        {
            auto blocks = fragment["blocks"].toArray();
            check(!blocks.isEmpty(), "片段没有音乐内容");
            score->startCmd(muse::TranslatableString::untranslatable("搬移或粘贴乐谱片段"));
            auto &tx = *score->transactionManager()->currentTransaction();
            bool ok = false;
            try
            {
                if (kind == "measures" || kind == "deleteMeasures")
                {
                    auto old = measures(score);
                    int index = std::clamp(target.measure, 0, int(old.size()));
                    auto ids = fragment["ids"].toArray();
                    QVector<Measure *> source;
                    for (auto *m : old)
                        if (ids.contains(eid(m)))
                            source << m;
                    if (kind == "deleteMeasures")
                    {
                        if (source.size() == old.size())
                        {
                            Score::InsertMeasureOptions options;
                            options.createMeasureRests = false;
                            score->insertMeasure(nullptr, options);
                        }
                        for (auto it = source.crbegin(); it != source.crend(); ++it)
                            score->deleteMeasures(*it, *it);
                    }
                    else
                    {
                        if (move && index < old.size() && source.contains(old[index]))
                        {
                            score->endCmd(true);
                            return true;
                        }
                        QVector<Measure *> replaced;
                        if (!insert && target.measures.size() <= 1)
                            for (int i = index;
                                 i < std::min(int(old.size()), index + int(blocks.size())); ++i)
                                replaced << old[i];
                        Measure *before = index < old.size() ? old[index] : nullptr;
                        int blockIndex = 0;
                        for (auto value : blocks)
                        {
                            if (!insert && target.measures.size() > 1)
                            {
                                int destination = blockIndex < target.measures.size()
                                                      ? target.measures[blockIndex]
                                                      : target.measures.last() + blockIndex -
                                                            target.measures.size() + 1;
                                before = destination < int(old.size()) ? old[destination] : nullptr;
                                if (before && !replaced.contains(toMeasure(before)))
                                    replaced << toMeasure(before);
                            }
                            ++blockIndex;
                            auto block = value.toObject();
                            Score::InsertMeasureOptions options;
                            options.createMeasureRests = false;
                            auto *m = toMeasure(score->insertMeasure(before, options));
                            check(m, "无法插入小节");
                            if (m->ticks() != length(block))
                                m->adjustToLen(length(block), true);
                            m->undoChangeProperty(
                                Pid::TIMESIG_NOMINAL,
                                Fraction(block["num"].toInt(4), block["den"].toInt(4)));
                            m->undoChangeProperty(Pid::REPEAT_START, block["repeatStart"].toBool());
                            m->undoChangeProperty(Pid::REPEAT_END, block["repeatEnd"].toBool());
                            paste(score, block, m->tick(), 0, tx);
                            if (block["barline"].toInt() == 32)
                                for (size_t staff = 0; staff < score->nstaves(); ++staff)
                                    m->setEndBarLineType(BarLineType::END, staff * VOICES);
                        }
                        if (move)
                            for (auto it = source.crbegin(); it != source.crend(); ++it)
                                score->deleteMeasures(*it, *it);
                        for (auto it = replaced.crbegin(); it != replaced.crend(); ++it)
                            if (!source.contains(*it) || !move)
                                score->deleteMeasures(*it, *it);
                    }
                    for (const auto &v : oldSnapshot.voices)
                        limits[v.track] =
                            MusicalTime(score->lastMeasure()->endTick().numerator(),
                                        score->lastMeasure()->endTick().denominator());
                }
                else if (kind == "notes" || kind == "deleteNotes")
                {
                    int sourceTrack = fragment["track"].toInt();
                    auto ids = fragment["ids"].toArray();
                    Fraction destination(target.position.n, target.position.d), total;
                    for (auto b : blocks)
                        total += length(b.toObject());
                    if (move || kind == "deleteNotes")
                    {
                        auto notes = voiceEvents(score, sourceTrack);
                        QVector<ChordRest *> selected;
                        auto full = fragment["fullIds"].toArray();
                        auto partial = fragment["partialIds"].toArray();
                        for (auto *cr : notes)
                            if (full.contains(eid(cr)))
                                selected << cr;
                        check(!selected.isEmpty() || !partial.isEmpty(), "原音符已失效");
                        if (move && sourceTrack == target.track)
                            for (auto id : partial)
                                if (auto *cr = crFor(score, id.toString());
                                    cr && destination >= cr->tick() &&
                                    destination < cr->tick() + cr->actualTicks())
                                {
                                    score->endCmd(true);
                                    return true;
                                }
                        for (auto id : partial)
                        {
                            auto *element = score->eidRegister()->itemFromEID(
                                EID::fromStdString(id.toString().toStdString()));
                            check(element && element->isNote(), "和弦内音符已失效");
                            score->deleteItem(toNote(element));
                        }
                        if (!selected.isEmpty())
                        {
                            if (move && sourceTrack == target.track)
                                for (auto *cr : selected)
                                    if (destination >= cr->tick() &&
                                        destination < cr->tick() + cr->actualTicks())
                                    {
                                        score->endCmd(true);
                                        return true;
                                    }
                            auto begin = selected.first()->tick(),
                                 end = score->lastMeasure()->endTick();
                            QVector<QJsonObject> remainder;
                            Fraction chunkStart(-1, 1), chunkEnd;
                            auto flush = [&]
                            {
                                if (chunkStart >= Fraction(0, 1))
                                {
                                    remainder
                                        << rangeData(score, chunkStart, chunkEnd, sourceTrack);
                                    chunkStart = Fraction(-1, 1);
                                }
                            };
                            auto sourceLimit =
                                Fraction(limits.value(sourceTrack).n, limits.value(sourceTrack).d);
                            for (auto *cr : notes)
                                if (cr->tick() >= begin && cr->tick() < sourceLimit)
                                {
                                    if (selected.contains(cr))
                                        flush();
                                    else
                                    {
                                        if (chunkStart < Fraction(0, 1))
                                            chunkStart = cr->tick();
                                        chunkEnd = cr->tick() + cr->actualTicks();
                                    }
                                }
                            flush();
                            Fraction offset;
                            if (sourceTrack == target.track)
                                for (auto *cr : selected)
                                    if (cr->tick() + cr->actualTicks() <= destination)
                                        offset += cr->actualTicks();
                            destination -= offset;
                            Fraction removed;
                            for (auto *cr : selected)
                                removed += cr->actualTicks();
                            limits[sourceTrack] =
                                limits.value(sourceTrack) -
                                MusicalTime(removed.numerator(), removed.denominator());
                            clear(score, sourceTrack, begin, end);
                            auto cursor = begin;
                            for (const auto &block : remainder)
                            {
                                paste(score, block, cursor, sourceTrack, tx);
                                cursor += length(block);
                            }
                        }
                    }
                    if (kind != "deleteNotes")
                    {
                        auto end =
                            Fraction(limits.value(target.track).n, limits.value(target.track).d);
                        QJsonObject tail;
                        if (insert && destination < end)
                            tail = rangeData(score, destination, end, target.track);
                        auto newEnd = insert ? std::max(destination, end) + total
                                             : std::max(end, destination + total);
                        ensureEnd(score, newEnd);
                        if (insert)
                            clear(score, target.track, destination,
                                  score->lastMeasure()->endTick());
                        auto cursor = destination;
                        for (auto b : blocks)
                        {
                            paste(score, b.toObject(), cursor, target.track, tx);
                            cursor += length(b.toObject());
                        }
                        if (insert && !tail.isEmpty())
                            paste(score, tail, cursor, target.track, tx);
                        limits[target.track] =
                            MusicalTime(newEnd.numerator(), newEnd.denominator());
                    }
                }
                else
                    throw std::runtime_error("未知乐谱片段类型");
                int number =
                    oldSnapshot.measures.isEmpty() ? 0 : oldSnapshot.measures.first().number - 1;
                for (auto *m = score->firstMeasure(); m; m = m->nextMeasure())
                    m->setMeasureNumber(number++);
                check(bool(score->sanityCheck()), "修改后的乐谱节奏检查失败");
                ok = true;
            }
            catch (...)
            {
                score->endCmd(true);
                throw;
            }
            score->endCmd(!ok);
            auto projected = staging.snapshot();
            bool owned = m_input.value("textOrigin").toBool(oldSnapshot.textBacked) &&
                         fragment["textOwned"].toBool();
            if (owned)
                projected.sourceEnds = limits;
            bool opaque = false;
            auto source = FanqieFormat().fromSnapshot(projected, &opaque);
            if (owned)
                source = FanqieFormat::replaceMusic(m_input["source"].toString(), source);
            auto parsed = FanqieFormat().parse(source);
            check(parsed.canProject(), "此片段当前无法生成一致的文字投影");
            QMap<QString, QString> anchors;
            for (const auto &m : oldSnapshot.measures)
                for (const auto &e : m.events)
                    anchors[e.id] = e.logicalId;
            QVector<int> tracks;
            for (const auto &v : projected.voices)
                if (!v.percussion)
                    tracks << v.track;
            auto voices = parsed.voiceNames.keys();
            QJsonArray bindings, automatic;
            for (auto &n : parsed.notes)
            {
                int track = tracks.value(voices.indexOf(n.voice));
                for (const auto &m : projected.measures)
                    for (const auto &e : m.events)
                        if (!e.grace && e.track == track && e.position == n.position)
                            n.id = anchors.value(e.id).isEmpty() ? "native:" + e.id
                                                                 : anchors.value(e.id);
                QJsonObject lyrics;
                for (auto it = n.lyrics.begin(); it != n.lyrics.end(); ++it)
                    lyrics[QString::number(it.key())] = it.value();
                bindings << QJsonObject{{"id", n.id},
                                        {"offset", n.offset},
                                        {"length", n.length},
                                        {"track", track},
                                        {"n", n.position.n},
                                        {"d", n.position.d},
                                        {"break", n.sourceBreak},
                                        {"lyrics", lyrics}};
            }
            for (const auto &boundary : parsed.boundaries)
                if (owned && boundary.type == 1)
                {
                    for (const auto &m : parsed.measures)
                        if (m.start + m.duration == boundary.position &&
                            m.duration == MusicalTime(parsed.numerator, parsed.denominator))
                            automatic << boundary.offset;
                }
            staging.m_input.remove("draftSource");
            staging.m_input["source"] = source;
            staging.m_input["bindings"] = bindings;
            staging.m_input["automaticBarOffsets"] = automatic;
            staging.m_input["textOrigin"] = owned;
            staging.m_input["protected"] = !owned || opaque;
            if (kind == "deleteNotes" || kind == "deleteMeasures")
            {
                auto pending = staging.m_input["pendingLyrics"].toArray();
                auto removed = fragment[kind == "deleteNotes" ? "fullIds" : "ids"].toArray();
                for (const auto &m : oldSnapshot.measures)
                    for (const auto &e : m.events)
                        if (removed.contains(kind == "deleteNotes" ? e.id : m.id))
                            for (const auto &l : e.lyrics)
                                if (!l.text.isEmpty())
                                    pending << l.text;
                staging.m_input["pendingLyrics"] = pending;
            }
            staging.syncSourceLayout();
        }
        m_project = staging.m_project;
        m_input = staging.m_input;
        recordChange(previous, false);
        notify();
        return true;
    }
    catch (const std::exception &e)
    {
        return fail(QString::fromUtf8(e.what()));
    }
    catch (const QString &e)
    {
        return fail(e);
    }
}
bool ScoreDocument::deleteFragment(const ScoreSelection &selection)
{
    auto fragment = copyFragment(selection);
    if (fragment.isEmpty())
        return false;
    if (selection.kind == SelectionKind::Lyrics)
    {
        QMap<QString, QString> words;
        for (const auto &id : selection.ids)
            words[id] = QString();
        return setLyrics(selection.track, selection.verse, words);
    }
    if (selection.kind == SelectionKind::Notes || selection.kind == SelectionKind::Measures)
    {
        auto data = QJsonDocument::fromJson(fragment).object();
        data["kind"] = selection.kind == SelectionKind::Notes ? "deleteNotes" : "deleteMeasures";
        return pasteFragment(QJsonDocument(data).toJson(QJsonDocument::Compact), {}, false, false);
    }
    return fail(tr("请选择有效的音乐内容"));
}
} // namespace jianpu
