// SPDX-License-Identifier: GPL-3.0-only
#include "scoreentrycontroller.h"
#include "core/scoredocument.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <algorithm>
namespace jianpu
{
ScoreEntryController::ScoreEntryController(ScoreDocument *document, IScoreEntryView *view,
                                           QObject *parent)
    : QObject(parent), m_document(document), m_view(view)
{
    connect(document, &ScoreDocument::changed, this,
            [this]
            {
                if (m_applying)
                    return;
                m_lastCursor = -1;
                m_lastInputTick = -1;
                m_lastInputNoteId.clear();
                m_draftChanges = {};
                m_draftSource.clear();
            });
    connect(document, &ScoreDocument::loaded, this, [this] { setMode(EntryMode::Append); });
}
void ScoreEntryController::focus(InputSurface surface) { m_session.surface = surface; }
void ScoreEntryController::setMode(EntryMode mode)
{
    m_session.mode = mode;
    m_lastInputTick = -1;
    m_lastCursor = -1;
    m_document->sealInputGroup();
    emit modeChanged(mode);
}
void ScoreEntryController::selectTarget(bool atEnd)
{
    setMode(atEnd ? EntryMode::Append : EntryMode::Overwrite);
}
void ScoreEntryController::setInputDuration(int denominator, int dots)
{
    m_denominator = denominator;
    m_dots = dots;
}
int ScoreEntryController::voiceNumber() const
{
    auto voices = FanqieFormat().parse(m_document->sourceText()).voiceNames.keys();
    int index = 0;
    for (const auto &v : m_view->score().voices)
    {
        if (v.percussion)
            continue;
        if (v.track == m_view->activeTrack())
            return index < voices.size() ? voices[index] : index + 1;
        ++index;
    }
    return 1;
}
bool ScoreEntryController::overwrite(int digit)
{
    auto ids = m_view->modelSelection();
    if (ids.isEmpty())
        return false;
    qint64 tick = -1;
    int denominator = 0;
    for (const auto &m : m_view->score().measures)
        for (const auto &e : m.events)
        {
            bool selected = ids.contains(e.id);
            for (const auto &n : e.notes)
                selected |= ids.contains(n.id);
            if (selected)
            {
                tick = e.tick;
                if (e.denominator == 0 && e.logicalId.isEmpty() &&
                    !m_document->inputState().value("protected").toBool())
                    denominator = m_denominator;
            }
        }
    auto inputId = ids.first();
    if (!m_document->edit(
            {digit == 0 ? EditRequest::Kind::InsertRest : EditRequest::Kind::SetDegree,
             ids,
             digit,
             denominator,
             {},
             m_dots}))
    {
        emit error(m_document->error());
        return false;
    }
    m_lastInputTick = tick;
    m_lastInputTrack = m_view->activeTrack();
    m_lastInputNoteId = inputId;
    advance(tick, m_lastInputTrack);
    return true;
}
bool ScoreEntryController::applySource(MusicTextEdit edit)
{
    if (!edit.changed)
        return false;
    auto changes = edit.changes;
    if (m_draftSource == changes.before && !m_draftChanges.before.isEmpty())
    {
        auto more = changes.edits;
        changes = m_draftChanges;
        changes.edits += more;
    }
    auto draft = m_document->analyzeText(edit.source, changes);
    bool unfinishedSuffix =
        std::any_of(draft.diagnostics.begin(), draft.diagnostics.end(),
                    [](const auto &d)
                    {
                        return d.category == InputDiagnostic::Category::Incomplete &&
                               (d.message.startsWith(QStringLiteral("倚音")) ||
                                d.message.startsWith(QStringLiteral("注释")));
                    });
    if (unfinishedSuffix)
    {
        m_draftChanges = changes;
        m_draftSource = edit.source;
        m_lastCursor = edit.cursor;
        m_document->storeInputDraft(edit.source, draft.messages().join('\n'),
                                    QString("entry:%1").arg(m_group));
        emit sourceCursorChanged(m_lastCursor);
        return true;
    }
    int relative = 0;
    for (const auto &n : draft.notes)
        if (n.voice == voiceNumber() && n.offset < edit.cursor)
        {
            edit.onset = n.position;
            relative = edit.cursor - n.offset;
        }
    if (!m_document->applyText(edit.source, QString("entry:%1").arg(m_group), changes))
    {
        emit error(m_document->error());
        return false;
    }
    m_draftChanges = {};
    m_draftSource.clear();
    // Canonical wrapping may have inserted Q/C lines and barlines. Resolve by musical time.
    auto parsed = m_document->textSnapshot();
    m_lastCursor = -1;
    for (const auto &n : parsed.notes)
        if (n.voice == voiceNumber() && n.position == edit.onset)
        {
            int lineEnd = parsed.source.indexOf('\n', n.offset);
            if (lineEnd < 0)
                lineEnd = parsed.source.size();
            m_lastCursor = std::min(n.offset + std::max(n.length, relative), lineEnd);
            for (const auto &m : m_view->score().measures)
                for (const auto &e : m.events)
                    if (e.track == m_view->activeTrack() && e.position == n.position)
                        m_view->selectId(e.notes.isEmpty() ? e.id : e.notes.first().id);
            break;
        }
    if (m_session.mode == EntryMode::Append)
        m_view->selectAppendPosition(m_view->activeTrack());
    else if (m_session.mode == EntryMode::Overwrite && m_lastInputTick >= 0)
        advance(m_lastInputTick, m_lastInputTrack);
    if (m_lastCursor < 0)
        m_lastCursor = MusicTextEditing::appendPosition(parsed.source, voiceNumber());
    emit sourceCursorChanged(std::max(0, m_lastCursor));
    return true;
}
bool ScoreEntryController::insertText(const QString &text)
{
    auto source = m_document->sourceText();
    auto parsed = m_document->textSnapshot();
    int voice = voiceNumber();
    int position = -1;
    MusicalTime onset;
    const TextNote *target = nullptr;
    if (m_lastCursor >= 0)
    {
        for (const auto &n : parsed.notes)
            if (n.voice == voice && n.offset < m_lastCursor && n.offset + n.length >= m_lastCursor)
                target = &n;
    }
    if (!target && m_session.mode != EntryMode::Append)
    {
        auto ids = m_view->modelSelection();
        for (const auto &m : m_view->score().measures)
            for (const auto &e : m.events)
            {
                bool selected = ids.contains(e.id);
                for (const auto &n : e.notes)
                    selected |= ids.contains(n.id);
                if ((m_lastInputTick >= 0 && e.tick == m_lastInputTick &&
                     e.track == m_lastInputTrack) ||
                    (m_lastInputTick < 0 && selected))
                    for (const auto &n : parsed.notes)
                        if (n.voice == voice && n.position == e.position)
                            target = &n;
            }
    }
    if (!target && m_session.mode == EntryMode::Append)
        for (const auto &n : parsed.notes)
            if (n.voice == voice)
                target = &n;
    bool digit = text.size() == 1 && text[0].isDigit();
    bool grace = inGrace();
    if (target)
    {
        position = target->offset + target->length;
        onset = target->position;
        if (digit)
            onset += target->duration;
    }
    bool noteSuffix = !text.isEmpty() && QString("/-.',#$=^[&\"").contains(text[0]);
    if (m_session.mode == EntryMode::Append && !noteSuffix)
        position = MusicTextEditing::appendPosition(source, voice);
    else if (!noteSuffix && m_lastCursor >= 0)
        position = m_lastCursor;
    if (wantsLiteralLetters() && !text.isEmpty() && text[0].isLetter() && m_lastCursor >= 0)
        position = m_lastCursor;
    if (grace && m_lastCursor >= 0)
        position = m_lastCursor;
    if (position < 0)
    {
        auto addition = QString("\nQ%1: ").arg(voice) + text;
        auto edit = MusicTextEditing::insert(source, source.size(), addition);
        edit.onset = onset;
        return applySource(edit);
    }
    QString addition = text;
    if (digit && !grace)
    {
        addition = (m_session.mode == EntryMode::Append ? QString() : QString(" ")) + text;
        for (int d = 4; d < m_denominator; d *= 2)
            addition += '/';
        if (m_denominator == 2)
            addition += '-';
        if (m_denominator == 1)
            addition += "---";
        addition += QString(m_dots, '.');
    }
    QVector<int> discarded;
    if (text == "[" && !grace)
    {
        int lineEnd = source.indexOf('\n', position);
        if (lineEnd < 0)
            lineEnd = source.size();
        for (auto value : m_document->inputState()["automaticBarOffsets"].toArray())
            if (value.toInt() >= position && value.toInt() < lineEnd)
                discarded << value.toInt();
    }
    auto edit = MusicTextEditing::insert(source, position, addition, discarded);
    edit.onset = onset;
    return applySource(edit);
}
bool ScoreEntryController::input(const QString &text)
{
    QScopedValueRollback<bool> applying(m_applying, true);
    if (text.isEmpty())
        return false;
    bool digit = text.size() == 1 && text[0].isDigit();
    if (digit && !inGrace())
    {
        ++m_group;
        if (m_view->atAppendPosition() && m_session.mode == EntryMode::Overwrite)
        {
            m_session.mode = EntryMode::Append;
            emit modeChanged(m_session.mode);
        }
    }
    if (!inGrace() && m_session.mode == EntryMode::Overwrite && digit &&
        !m_view->atAppendPosition())
        return overwrite(text[0].digitValue());
    if (!inGrace() && m_session.mode == EntryMode::Overwrite && text.size() == 1 &&
        QString("/-.',#$=").contains(text[0]))
        return suffix(text[0]);
    return insertText(text);
}
bool ScoreEntryController::backspace()
{
    QScopedValueRollback<bool> applying(m_applying, true);
    auto parsed = m_document->textSnapshot();
    int position = m_session.mode == EntryMode::Append
                       ? MusicTextEditing::appendPosition(parsed.source, voiceNumber())
                       : m_lastCursor;
    if (position < 0)
    {
        auto ids = m_view->modelSelection();
        for (const auto &m : m_view->score().measures)
            for (const auto &e : m.events)
            {
                bool selected = ids.contains(e.id);
                for (const auto &n : e.notes)
                    selected |= ids.contains(n.id);
                if (selected)
                    position = e.sourceStart;
            }
    }
    ++m_group;
    auto edit = MusicTextEditing::eraseBefore(parsed, voiceNumber(), position);
    bool ok = applySource(edit);
    m_lastInputTick = -1;
    return ok;
}
bool ScoreEntryController::inGrace() const
{
    if (m_lastCursor < 0)
        return false;
    auto prefix = m_document->sourceText().left(m_lastCursor);
    return prefix.lastIndexOf('[') > prefix.lastIndexOf(']');
}
bool ScoreEntryController::wantsLiteralLetters() const
{
    const auto source = m_document->sourceText();
    int cursor = m_lastCursor;
    if (cursor < 0)
        cursor = MusicTextEditing::appendPosition(source, voiceNumber());
    return inGrace() || QRegularExpression("&[A-Za-z]*$").match(source.left(cursor)).hasMatch();
}
void ScoreEntryController::advance(qint64 tick, int track)
{
    const EventData *current = nullptr, *next = nullptr;
    bool protectedScore = m_document->inputState().value("protected").toBool();
    for (const auto &m : m_view->score().measures)
        for (const auto &e : m.events)
            if (!e.grace && e.track == track && e.tick == tick)
                current = &e;
    if (!current)
        return;
    auto end = current->position + current->length;
    for (const auto &m : m_view->score().measures)
        for (const auto &e : m.events)
            if (!e.grace && e.track == track && e.position >= end &&
                (!e.logicalId.isEmpty() || protectedScore) &&
                (e.notes.isEmpty() || std::any_of(e.notes.begin(), e.notes.end(),
                                                  [](const auto &n) { return !n.tieEnd; })) &&
                (!next || e.position < next->position))
                next = &e;
    if (next)
        m_view->selectId(next->notes.isEmpty() ? next->id : next->notes.first().id);
    else
        m_view->selectAppendPosition(track);
}
bool ScoreEntryController::suffix(QChar suffix)
{
    const EventData *target = nullptr;
    for (const auto &measure : m_view->score().measures)
        for (const auto &e : measure.events)
        {
            if (e.grace)
                continue;
            bool selected = m_view->modelSelection().contains(e.id);
            for (const auto &n : e.notes)
                selected |= m_view->modelSelection().contains(n.id);
            if ((m_lastInputTick >= 0 && e.tick == m_lastInputTick &&
                 e.track == m_lastInputTrack) ||
                (m_lastInputTick < 0 && selected))
                target = &e;
        }
    if (!target)
        return false;
    const auto current = *target;
    QStringList ids{current.notes.isEmpty() ? current.id : current.notes.first().id};
    for (const auto &n : current.notes)
        if ((m_lastInputTick >= 0 && n.id == m_lastInputNoteId) ||
            (m_lastInputTick < 0 && m_view->modelSelection().contains(n.id)))
            ids = {n.id};
    using K = EditRequest::Kind;
    auto applySuffix = [&](EditRequest request)
    {
        bool ok = m_document->edit(request);
        if (ok && m_lastInputTick >= 0)
            advance(m_lastInputTick, m_lastInputTrack);
        return ok;
    };
    if (suffix == '\'' || suffix == ',' || suffix == '#' || suffix == '$' || suffix == '=')
        return applySuffix({suffix == '\''  ? K::OctaveUp
                            : suffix == ',' ? K::OctaveDown
                            : suffix == '#' ? K::Sharp
                            : suffix == '$' ? K::Flat
                                            : K::Natural,
                            ids});
    MusicalTime written = current.denominator > 0
                              ? MusicalTime((1LL << (current.dots + 1)) - 1,
                                            current.denominator * (1LL << current.dots))
                              : current.length;
    if (suffix == '/')
        written = written / MusicalTime(2);
    else if (suffix == '-')
        written += MusicalTime(1, 4);
    else if (suffix == '.')
    {
        if (current.dots >= 2)
        {
            emit error(tr("谱面覆盖输入目前支持两个附点；更复杂时值可在文字稿中填写。"));
            return false;
        }
        return applySuffix({K::Duration, ids, std::max(1, current.denominator), current.dots + 1});
    }
    for (int denominator = 1; denominator <= 128; denominator *= 2)
        for (int dots = 0; dots <= 2; ++dots)
            if (written == MusicalTime((1LL << (dots + 1)) - 1, denominator * (1LL << dots)))
                return applySuffix({K::Duration, ids, denominator, dots});
    emit error(tr("此复合时值请在文字稿中填写；当前音符未修改。"));
    return false;
}
} // namespace jianpu
