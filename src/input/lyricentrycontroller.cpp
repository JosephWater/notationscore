// SPDX-License-Identifier: GPL-3.0-only
#include "lyricentrycontroller.h"
#include "core/scoredocument.h"
#include "input/textformat.h"
#include <algorithm>
namespace jianpu
{
LyricEntryController::LyricEntryController(ScoreDocument *d, QObject *parent)
    : QObject(parent), m_document(d)
{
}
QVector<EventData> LyricEntryController::events() const
{
    QVector<EventData> result;
    for (const auto &m : m_document->snapshot().measures)
        for (const auto &e : m.events)
            if (e.track == m_target.track && !e.grace && !e.rest &&
                std::any_of(e.notes.begin(), e.notes.end(),
                            [](const auto &n) { return !n.tieEnd; }))
                result << e;
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) { return a.position < b.position; });
    return result;
}
bool LyricEntryController::commit(const QVector<EventData> &notes, const QStringList &words,
                                  int index, const QStringList &extra)
{
    QMap<QString, QString> values;
    for (int i = 0; i < notes.size(); ++i)
        values[notes[i].id] = words.value(i);
    if (!m_document->setLyrics(m_target.track, m_target.verse, values, extra))
    {
        emit error(m_document->error());
        return false;
    }
    auto layout = m_document->presentation();
    QString id;
    if (index < notes.size())
        for (const auto &slot : layout.lyrics)
            if (slot.eventId == notes[std::max(0, index)].id && slot.verse == m_target.verse)
            {
                m_target = slot;
                id = slot.id;
                break;
            }
    if (id.isEmpty())
    {
        const LyricSlot *last = nullptr;
        for (const auto &slot : layout.lyrics)
            if (slot.track == m_target.track && slot.verse == m_target.verse && slot.tail &&
                (!last || slot.tick > last->tick))
                last = &slot;
        if (last)
        {
            m_target = *last;
            id = last->id;
        }
    }
    emit selectRequested(id);
    return true;
}
bool LyricEntryController::input(const QString &text, EntryMode mode)
{
    auto notes = events();
    QStringList words;
    for (const auto &e : notes)
    {
        QString word;
        for (const auto &l : e.lyrics)
            if (l.verse == m_target.verse)
                word = l.text;
        words << word;
    }
    int index = notes.size();
    for (int i = 0; i < notes.size(); ++i)
        if (notes[i].id == m_target.eventId ||
            (m_target.eventId.isEmpty() && notes[i].position >= m_target.position &&
             index == notes.size()))
            index = i;
    if (mode == EntryMode::Insert && !m_target.tail && !m_target.text.isEmpty())
        ++index;
    auto input = FanqieFormat::lyricTokens(text);
    if (input.isEmpty() && !text.isEmpty() && text.front().isPunct() && index > 0)
    {
        words[index - 1] += text;
        return commit(notes, words, index);
    }
    int start = index;
    for (const auto &word : input)
    {
        auto value = word == "@" ? QString() : word;
        if (mode == EntryMode::Insert)
            words.insert(index, value);
        else
        {
            while (words.size() <= index)
                words << QString();
            words[index] = value;
        }
        ++index;
    }
    QStringList extra;
    for (int i = notes.size(); i < words.size(); ++i)
        if (!words[i].isEmpty())
            extra << words[i];
    bool ok =
        commit(notes, words, mode == EntryMode::Insert ? std::max(start, index - 1) : index, extra);
    m_inserted = ok;
    return ok;
}
bool LyricEntryController::erase(bool backward, EntryMode mode)
{
    auto notes = events();
    QStringList words;
    for (const auto &e : notes)
    {
        QString word;
        for (const auto &l : e.lyrics)
            if (l.verse == m_target.verse)
                word = l.text;
        words << word;
    }
    int index = notes.size();
    for (int i = 0; i < notes.size(); ++i)
        if (notes[i].id == m_target.eventId ||
            (m_target.eventId.isEmpty() && notes[i].position >= m_target.position &&
             index == notes.size()))
            index = i;
    if (backward && !(mode == EntryMode::Insert && m_inserted))
        --index;
    if (index < 0 || index >= words.size())
        return false;
    words.removeAt(index);
    words << QString();
    m_inserted = false;
    return commit(notes, words, index);
}
bool LyricEntryController::replaceRange(const QStringList &ids, const QString &text)
{
    auto notes = events();
    QStringList words;
    for (const auto &e : notes)
    {
        QString word;
        for (const auto &l : e.lyrics)
            if (l.verse == m_target.verse)
                word = l.text;
        words << word;
    }
    int first = notes.size();
    for (int i = notes.size() - 1; i >= 0; --i)
        if (ids.contains(notes[i].id))
        {
            first = i;
            words.removeAt(i);
        }
    auto incoming = FanqieFormat::lyricTokens(text);
    int at = first;
    for (const auto &word : incoming)
        words.insert(at++, word == "@" ? QString() : word);
    QStringList extra;
    for (int i = notes.size(); i < words.size(); ++i)
        if (!words[i].isEmpty())
            extra << words[i];
    return commit(notes, words, std::min(at, int(notes.size())), extra);
}
int LyricEntryController::newVerse(int track)
{
    bool eligible = false;
    for (const auto &m : m_document->snapshot().measures)
        for (const auto &e : m.events)
            eligible |= e.track == track && !e.rest && !e.grace;
    if (!eligible)
    {
        emit error(tr("请先在此声部输入音符，再填写歌词"));
        return -1;
    }
    int verse = m_document->snapshot().lyricRows.value(track);
    if (!m_document->setLyrics(track, verse, {}))
    {
        emit error(m_document->error());
        return -1;
    }
    return verse;
}
} // namespace jianpu
