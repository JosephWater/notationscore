// SPDX-License-Identifier: GPL-3.0-only
#include "layout/visualelement.h"
#include "scorecanvas.h"
#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QMouseEvent>
#include <algorithm>
namespace jianpu
{
QVector<SelectionTarget> ScoreCanvas::selectionTargets() const
{
    QVector<SelectionTarget> result;
    QHash<QString, bool> visible;
    QHash<QString, MusicalTime> positions;
    for (const auto &m : m_score.measures)
        for (const auto &e : m.events)
        {
            visible[e.id] = e.visible || !e.rest;
            positions[e.id] = e.position;
        }
    for (const auto &hit : m_layout.hits)
        if (!hit.eventId.isEmpty() && visible.value(hit.eventId, true))
            result << SelectionTarget{hit.id, QString("notes:%1").arg(hit.track),
                                      SelectionKind::Notes, positions.value(hit.eventId), hit.rect};
    for (const auto &s : m_layout.lyrics)
        result << SelectionTarget{s.id, QString("lyrics:%1:%2").arg(s.track).arg(s.verse),
                                  SelectionKind::Lyrics, s.position, s.rect};
    for (const auto &m : m_layout.measures)
        result << SelectionTarget{"@measure:" + m.id, "measures", SelectionKind::Measures, m.index,
                                  m.rect};
    return result;
}
SelectionTarget ScoreCanvas::targetAt(QPointF point, SelectionKind forced) const
{
    auto all = selectionTargets();
    if (forced != SelectionKind::Measures)
    {
        if (const auto *hit = m_layout.hitTest(point))
        {
            for (const auto &t : all)
                if (t.id == hit->id && (forced == SelectionKind::None || forced == t.kind))
                    return t;
            auto element = m_layout.elements.value(hit->id);
            if (element && element->identity().movable)
                return {};
        }
    }
    for (const auto &t : all)
        if (t.kind == SelectionKind::Measures && t.rect.contains(point))
            return t;
    return {};
}
ScoreSelection ScoreCanvas::contentSelection() const
{
    ScoreSelection result;
    QHash<QString, SelectionTarget> targets;
    for (const auto &t : selectionTargets())
        targets[t.id] = t;
    QHash<QString, LyricSlot> lyrics;
    for (const auto &s : m_layout.lyrics)
        lyrics[s.id] = s;
    QHash<QString, int> tracks;
    for (const auto &h : m_layout.hits)
        tracks[h.id] = h.track;
    for (const auto &id : m_selection)
    {
        if (!targets.contains(id))
            continue;
        const auto &t = targets[id];
        if (result.kind == SelectionKind::None)
            result.kind = t.kind;
        if (t.kind != result.kind)
            continue;
        if (t.kind == SelectionKind::Measures)
        {
            if (!result.measures.contains(int(t.order.n)))
                result.measures << int(t.order.n);
        }
        else if (t.kind == SelectionKind::Lyrics)
        {
            auto s = lyrics.value(id);
            if (!s.eventId.isEmpty())
            {
                result.ids << s.eventId;
                result.track = s.track;
                result.verse = s.verse;
            }
        }
        else
        {
            result.ids << id;
            result.track = tracks.value(id);
        }
    }
    std::sort(result.measures.begin(), result.measures.end());
    return result;
}
bool ScoreCanvas::beginContentSelection(QPointF point, Qt::KeyboardModifiers modifiers)
{
    auto target = targetAt(point);
    if (target.kind == SelectionKind::None)
        return false;
    auto current = contentSelection();
    if ((current.kind == SelectionKind::None && !m_selection.isEmpty()) ||
        (current.kind != SelectionKind::None &&
         (current.kind != target.kind ||
          (target.kind == SelectionKind::Lyrics && selectedLyric() &&
           target.group !=
               QString("lyrics:%1:%2").arg(selectedLyric()->track).arg(selectedLyric()->verse)))))
    {
        m_selection.clear();
        m_rangeAnchor.clear();
    }
    bool selected = m_selection.contains(target.id), control = modifiers & Qt::ControlModifier,
         shift = modifiers & Qt::ShiftModifier;
    m_selectionBeforeDrag = m_selection;
    m_pressedTarget = target;
    m_pressModifiers = modifiers;
    m_dragStart = m_dragEnd = point;
    m_gestureMoved = false;
    m_pendingToggle = false;
    m_dropValid = false;
    m_dragFragment.clear();
    m_contentGesture = selected && !shift ? 2 : 1;
    if (selected && control && !shift)
        m_pendingToggle = true;
    if (!selected || shift)
        m_selection = SelectionPolicy::click(m_selection, m_rangeAnchor, target, selectionTargets(),
                                             control, shift);
    if (!shift || m_rangeAnchor.isEmpty())
        m_rangeAnchor = target.id;
    if (auto *lyric = selectedLyric())
    {
        m_track = lyric->track;
        m_lyricEntry->select(*lyric);
    }
    else if (target.kind == SelectionKind::Notes)
    {
        for (const auto &h : m_layout.hits)
            if (h.id == target.id)
                m_track = h.track;
    }
    else if (target.kind == SelectionKind::Measures)
    {
        for (const auto &m : m_layout.measures)
            if ("@measure:" + m.id == target.id)
            {
                m_track = m.track;
                break;
            }
    }
    m_entry->selectTarget(selectedLyric() && selectedLyric()->tail);
    m_keyboardInput = true;
    update();
    emit selectionChanged();
    return true;
}
bool ScoreCanvas::dropTargetAt(QPointF point, const QByteArray &fragment, ScoreDropTarget &drop,
                               QRectF &bounds) const
{
    auto root = QJsonDocument::fromJson(fragment).object();
    auto kind = root["kind"].toString();
    const MeasureRegion *measure = nullptr;
    for (const auto &m : m_layout.measures)
        if (m.rect.contains(point))
        {
            measure = &m;
            break;
        }
    if (kind == "measures")
    {
        if (measure)
        {
            drop.measure = measure->index;
            drop.track = measure->track;
            drop.position = MusicalTime(measure->tick, 1920);
            bounds = measure->rect;
            return true;
        }
        for (const auto &m : m_layout.measures)
            if (point.x() > m.rect.right() &&
                std::abs(point.y() - m.rect.center().y()) < m.rect.height() / 2 &&
                m.index == m_score.measures.size() - 1)
            {
                drop.measure = m_score.measures.size();
                drop.track = m.track;
                drop.position = MusicalTime(m.tick + m.duration, 1920);
                bounds = QRectF(m.rect.right(), m.rect.top(), 20, m.rect.height());
                return true;
            }
        return false;
    }
    if (kind == "lyrics")
    {
        const LyricSlot *nearest = nullptr;
        qreal distance = 1e9;
        for (const auto &s : m_layout.lyrics)
        {
            qreal d = QLineF(point, s.rect.center()).length();
            if (d < distance &&
                (s.rect.adjusted(-12, -8, 12, 8).contains(point) ||
                 (measure && s.track == measure->track && s.system == measure->system)))
            {
                distance = d;
                nearest = &s;
            }
        }
        if (!nearest)
            return false;
        drop.track = nearest->track;
        drop.verse = nearest->verse;
        drop.position = nearest->position;
        bounds = nearest->rect;
        return true;
    }
    if (kind == "notes")
    {
        const HitRegion *nearest = nullptr;
        qreal distance = 1e9;
        for (const auto &h : m_layout.hits)
            if (!h.eventId.isEmpty() && (!measure || (h.track == measure->track && h.measure >= 0)))
            {
                qreal d = QLineF(point, h.rect.center()).length();
                if (d < distance &&
                    (h.rect.adjusted(-12, -8, 12, 8).contains(point) ||
                     (measure &&
                      measure->rect.adjusted(-20, -10, 20, 10).contains(h.rect.center()))))
                {
                    nearest = &h;
                    distance = d;
                }
            }
        for (auto it = m_inputSlots.begin(); it != m_inputSlots.end(); ++it)
            if (it.value().adjusted(-5, -5, 5, 5).contains(point))
            {
                MusicalTime end;
                for (const auto &m : m_score.measures)
                    for (const auto &e : m.events)
                        if (e.track == it.key() && !e.logicalId.isEmpty())
                            end = std::max(end, e.position + e.length);
                drop.track = it.key();
                drop.position = end;
                bounds = it.value();
                return true;
            }
        if (!nearest)
            return false;
        drop.track = nearest->track;
        bounds = nearest->rect;
        for (const auto &m : m_score.measures)
            for (const auto &e : m.events)
                if (e.id == nearest->eventId)
                {
                    drop.position = e.position;
                    return true;
                }
    }
    return false;
}
void ScoreCanvas::moveContentSelection(QMouseEvent *event)
{
    if (!m_contentGesture)
        return;
    auto point = event->position() / m_zoom;
    m_dragEnd = point;
    if (!m_gestureMoved &&
        QLineF(point, m_dragStart).length() * m_zoom < QApplication::startDragDistance())
        return;
    m_gestureMoved = true;
    if (m_contentGesture == 2)
    {
        if (m_dragFragment.isEmpty())
            m_dragFragment = m_document->copyFragment(contentSelection());
        m_dropValid = !m_dragFragment.isEmpty() &&
                      dropTargetAt(point, m_dragFragment, m_dropTarget, m_dropBounds);
        setCursor(m_dropValid ? (event->modifiers() & Qt::ControlModifier ? Qt::DragCopyCursor
                                                                          : Qt::DragMoveCursor)
                              : Qt::ForbiddenCursor);
    }
    else
    {
        m_dragging = true;
        auto target = targetAt(point, m_pressedTarget.kind);
        auto selected =
            target.kind == m_pressedTarget.kind && target.group == m_pressedTarget.group
                ? SelectionPolicy::range(m_pressModifiers & Qt::ShiftModifier ? m_rangeAnchor
                                                                              : m_pressedTarget.id,
                                         target.id, selectionTargets())
                : SelectionPolicy::rectangle(QRectF(m_dragStart, point).normalized(),
                                             m_pressedTarget, selectionTargets());
        if (m_pressModifiers & Qt::ControlModifier)
            for (const auto &id : m_selectionBeforeDrag)
                if (!selected.contains(id))
                    selected << id;
        if (!selected.isEmpty())
            m_selection = selected;
        emit selectionChanged();
    }
    update();
}
bool ScoreCanvas::releaseContentSelection(QMouseEvent *event)
{
    if (!m_contentGesture)
        return false;
    int gesture = m_contentGesture;
    m_contentGesture = 0;
    m_dragging = false;
    setCursor(Qt::ArrowCursor);
    if (gesture == 2 && m_gestureMoved && m_dropValid)
    {
        bool moving = !(event->modifiers() & Qt::ControlModifier);
        auto before = m_score;
        if (m_document->pasteFragment(m_dragFragment, m_dropTarget, true, moving))
        {
            if (m_score.revision != before.revision)
                finishFragmentSelection(m_dragFragment, m_dropTarget, moving, before);
        }
        else
            emit operationFailed(m_document->error());
    }
    else if (!m_gestureMoved)
    {
        if (m_pendingToggle)
            m_selection.removeAll(m_pressedTarget.id);
        else if (!(m_pressModifiers & (Qt::ControlModifier | Qt::ShiftModifier)))
            m_selection = {m_pressedTarget.id};
        if (auto *lyric = selectedLyric())
            m_lyricEntry->select(*lyric);
        emit selectionChanged();
    }
    m_dropValid = false;
    m_dragFragment.clear();
    update();
    return true;
}
void ScoreCanvas::finishFragmentSelection(const QByteArray &bytes, const ScoreDropTarget &drop,
                                          bool move, const ScoreSnapshot &before)
{
    auto root = QJsonDocument::fromJson(bytes).object();
    auto kind = root["kind"].toString();
    m_selection.clear();
    if (kind == "measures")
    {
        int start = drop.measure;
        if (move)
            for (int i = 0; i < before.measures.size() && i < drop.measure; ++i)
                if (root["ids"].toArray().contains(before.measures[i].id))
                    --start;
        if (drop.measures.size() > 1)
        {
            int count = root["blocks"].toArray().size();
            for (int i = 0; i < count; ++i)
            {
                int index = i < drop.measures.size()
                                ? drop.measures[i]
                                : drop.measures.last() + i - drop.measures.size() + 1;
                if (index >= 0 && index < m_score.measures.size())
                    m_selection << "@measure:" + m_score.measures[index].id;
            }
        }
        else
            for (int i = start; i < std::min(int(m_score.measures.size()),
                                             start + int(root["blocks"].toArray().size()));
                 ++i)
                if (i >= 0)
                    m_selection << "@measure:" + m_score.measures[i].id;
    }
    else if (kind == "notes")
    {
        auto start = drop.position;
        MusicalTime total;
        for (auto b : root["blocks"].toArray())
        {
            auto o = b.toObject();
            total += MusicalTime(o["n"].toInteger(), o["d"].toInteger(1));
        }
        if (move && root["track"].toInt() == drop.track)
            for (const auto &m : before.measures)
                for (const auto &e : m.events)
                    if (root["fullIds"].toArray().contains(e.id) &&
                        e.position + e.length <= drop.position)
                        start = start - e.length;
        for (const auto &m : m_score.measures)
            for (const auto &e : m.events)
                if (e.track == drop.track && !e.grace && e.position >= start &&
                    e.position < start + total)
                {
                    if (e.notes.isEmpty())
                        m_selection << e.id;
                    else
                        for (const auto &n : e.notes)
                            m_selection << n.id;
                }
    }
    else
    {
        QVector<EventData> notes;
        for (const auto &m : before.measures)
            for (const auto &e : m.events)
                if (e.track == drop.track && !e.rest && !e.grace &&
                    std::any_of(e.notes.begin(), e.notes.end(),
                                [](const auto &n) { return !n.tieEnd; }))
                    notes << e;
        int start = notes.size();
        for (int i = 0; i < notes.size(); ++i)
            if (notes[i].position >= drop.position)
            {
                start = i;
                break;
            }
        int originalStart = start;
        if (move && root["track"].toInt() == drop.track && root["verse"].toInt() == drop.verse)
            for (int i = 0; i < originalStart; ++i)
                if (root["ids"].toArray().contains(notes[i].id))
                    --start;
        for (int i = start;
             i < std::min(int(notes.size()), start + int(root["words"].toArray().size())); ++i)
            for (const auto &s : m_layout.lyrics)
                if (s.track == drop.track && s.verse == drop.verse &&
                    s.position == notes[i].position && !s.eventId.isEmpty())
                    m_selection << s.id;
    }

    if (!m_selection.isEmpty())
        m_rangeAnchor = m_selection.first();
    if (auto *s = selectedLyric())
        m_lyricEntry->select(*s);
    emit selectionChanged();
    update();
}
void ScoreCanvas::pasteContent(bool forceInsert)
{
    auto bytes = QApplication::clipboard()->mimeData()->data("application/x-jianpu-fragment");
    if (bytes.isEmpty())
    {
        paste();
        return;
    }
    auto root = QJsonDocument::fromJson(bytes).object();
    auto selection = contentSelection();
    ScoreDropTarget target;
    bool insert = forceInsert || m_entry->mode() != EntryMode::Overwrite;
    if (root["kind"].toString() == "lyrics" && selection.kind == SelectionKind::Lyrics &&
        selection.ids.size() > 1 && !insert)
    {
        QStringList words;
        for (auto value : root["words"].toArray())
            words << (value.toString().isEmpty()
                          ? "@"
                          : FanqieFormat::lyricTokens(value.toString()).join("~"));
        m_lyricEntry->replaceRange(selection.ids, words.join(" "));
        return;
    }
    target.track = m_track;
    if (root["kind"].toString() == "measures")
    {
        target.measure =
            selection.measures.isEmpty() ? m_score.measures.size() : selection.measures.first();
        if (selection.measures.isEmpty())
            for (int i = 0; i < m_score.measures.size(); ++i)
                for (const auto &e : m_score.measures[i].events)
                {
                    bool selected = selection.ids.contains(e.id);
                    for (const auto &n : e.notes)
                        selected |= selection.ids.contains(n.id);
                    if (selected)
                        target.measure = std::min(target.measure, i);
                }
        if (!insert)
            target.measures = selection.measures;
    }
    else if (auto *lyric = selectedLyric())
    {
        target.position = lyric->position;
        target.verse = lyric->verse;
        target.track = lyric->track;
    }
    else
    {
        bool found = false;
        for (const auto &m : m_score.measures)
            for (const auto &e : m.events)
            {
                bool selected = selection.ids.contains(e.id);
                for (const auto &n : e.notes)
                    selected |= selection.ids.contains(n.id);
                if (selected && !found)
                {
                    target.position = e.position;
                    target.track = e.track;
                    found = true;
                }
            }
        if (!found)
            for (const auto &m : m_score.measures)
                for (const auto &e : m.events)
                    if (e.track == m_track && !e.logicalId.isEmpty())
                        target.position = std::max(target.position, e.position + e.length);
    }
    auto before = m_score;
    if (m_document->pasteFragment(bytes, target, insert))
        finishFragmentSelection(bytes, target, false, before);
    else
        emit operationFailed(m_document->error());
}
void ScoreCanvas::pasteInsert() { pasteContent(true); }
} // namespace jianpu
