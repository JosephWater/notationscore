// SPDX-License-Identifier: GPL-3.0-only
#include "scorecanvas.h"
#include "layout/alignmentsnapper.h"
#include "layout/layoutsettings.h"
#include "layout/visualelement.h"
#include <QApplication>
#include <QClipboard>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace jianpu
{
ScoreCanvas::ScoreCanvas(ScoreDocument *document, QWidget *parent)
    : QWidget(parent), m_document(document)
{
    m_entry = new ScoreEntryController(document, this, this);
    m_lyricEntry = new LyricEntryController(document, this);
    setAttribute(Qt::WA_InputMethodEnabled);
    connect(m_lyricEntry, &LyricEntryController::error, this, &ScoreCanvas::operationFailed);
    connect(m_lyricEntry, &LyricEntryController::selectRequested, this,
            [this](const QString &id)
            {
                QScopedValueRollback<bool> selecting(m_selectingLyrics, true);
                if (!id.isEmpty())
                {
                    selectId(id);
                    if (m_entry->mode() != EntryMode::Insert && selectedLyric())
                        m_entry->setMode(selectedLyric()->tail ? EntryMode::Append
                                                               : EntryMode::Overwrite);
                }
            });
    connect(m_entry, &ScoreEntryController::error, this, &ScoreCanvas::operationFailed);
    setObjectName("scoreCanvas");
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("简谱编辑画布"));
    setToolTip(tr("点击修改，末尾空格追加；拖动划选，Shift 连选，Ctrl 多选；空白小节区域选整节。\n"
                  "拖动已选内容搬移，Ctrl 拖动复制；Ctrl+V 按模式粘贴，Ctrl+Shift+V 插入。\n"
                  "方向键导航；Insert 切换插入；Alt+↑↓ 改音高；C 新增一段歌词。"));
    connect(document, &ScoreDocument::changed, this, &ScoreCanvas::refresh);
    refresh();
}
void ScoreCanvas::refresh()
{
    qint64 oldTick = -1;
    int oldTrack = m_track;
    if (!m_selection.isEmpty())
        for (const auto &h : m_layout.hits)
            if (h.id == m_selection.first() && !h.eventId.isEmpty())
            {
                oldTick = h.tick;
                oldTrack = h.track;
                break;
            }
    m_score = m_document->snapshot();
    bool activeExists = false;
    for (const auto &v : m_score.voices)
        if (v.track == m_track)
            activeExists = true;
    if (!activeExists && !m_score.voices.isEmpty())
        m_track = m_score.voices.first().track;
    m_layout = m_document->presentation();
    buildInputSlots();
    QSet<QString> inputEvents;
    for (const auto &measure : m_score.measures)
        for (const auto &event : measure.events)
            if (!event.logicalId.isEmpty())
                inputEvents.insert(event.id);
    for (auto value : m_document->inputState().value("pendingSpans").toArray())
    {
        auto pending = value.toObject();
        int track = pending["track"].toInt();
        qint64 tick = pending["tick"].toVariant().toLongLong();
        QMap<int, QPair<QPointF, QPointF>> rows;
        int count = 0;
        for (const auto &hit : m_layout.hits)
            if (hit.track == track && hit.tick >= tick && !hit.eventId.isEmpty() &&
                inputEvents.contains(hit.eventId))
            {
                auto point = QPointF(hit.rect.center().x(), hit.rect.top() - 14);
                int row = qRound(point.y() / 10);
                if (!rows.contains(row))
                    rows[row] = {point, point};
                else
                    rows[row].second = point;
                ++count;
            }
        for (auto row : rows)
        {
            auto a = row.first, b = row.second;
            if (b.x() < a.x() + 55)
                b.setX(a.x() + 55);
            LayoutItem item;
            item.id = "input-preview";
            item.kind = LayoutItem::Kind::Curve;
            item.penStyle = Qt::DashLine;
            item.color = QColor("#8798ae");
            item.strokeWidth = 1.4;
            item.path.moveTo(a);
            item.path.cubicTo(a + QPointF(15, -15), b + QPointF(-15, -15), b);
            item.bounds = item.path.boundingRect();
            m_layout.items << item;
            if (pending["kind"].toString() == "tuplet")
            {
                LayoutItem label;
                label.kind = LayoutItem::Kind::Text;
                label.text = QString("%1?").arg(count);
                label.bounds = QRectF(a.x(), a.y() - 30, b.x() - a.x(), 20);
                label.fontSize = 12;
                label.color = item.color;
                m_layout.items << label;
            }
        }
    }

    QStringList valid;
    for (const auto &id : m_selection)
        if (id.startsWith("@measure:"))
        {
            for (const auto &m : m_layout.measures)
                if (id == "@measure:" + m.id)
                {
                    valid << id;
                    break;
                }
        }
    for (const auto &id : m_selection)
        for (const auto &hit : m_layout.hits)
            if (hit.id == id)
            {
                valid << id;
                break;
            }
    m_selection = valid;
    if (m_selection.isEmpty() && oldTick >= 0)
        for (const auto &h : m_layout.hits)
            if (h.tick == oldTick && h.track == oldTrack && !h.eventId.isEmpty())
            {
                m_selection = {h.id};
                break;
            }
    resize((m_layout.size * m_zoom).toSize().expandedTo(QSize(1, 1)));
    updateGeometry();
    update();
    emit selectionChanged();
}
void ScoreCanvas::setActiveTrack(int track)
{
    m_track = track;
    m_selection.clear();
    m_entry->selectTarget(false);
    update();
    emit selectionChanged();
}
void ScoreCanvas::setZoom(qreal zoom)
{
    m_zoom = std::clamp(zoom, 0.25, 3.0);
    resize((m_layout.size * m_zoom).toSize());
    updateGeometry();
    update();
    emit zoomChanged(m_zoom);
}
void ScoreCanvas::setInputDuration(int denominator, int dots)
{
    m_entry->setInputDuration(denominator, dots);
}
void ScoreCanvas::paintScore(QPainter &p, const QRectF &exposed, bool interactive) const
{
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(exposed, QColor("#dce1e7"));
    for (const auto &page : m_layout.pages)
    {
        p.fillRect(page.translated(3, 4), QColor("#c2c8d0"));
        p.fillRect(page, Qt::white);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#d7eaff"));
    if (interactive)
        for (const auto &m : m_layout.measures)
            if (m_selection.contains("@measure:" + m.id))
                p.drawRoundedRect(m.rect, 3, 3);
    for (const auto &hit : m_layout.hits)
        if (interactive && m_selection.contains(hit.id))
            p.drawRoundedRect(hit.rect.adjusted(-2, -2, 2, 2), 3, 3);
    for (const auto &item : m_layout.items)
    {
        if (!item.transform.mapRect(item.bounds).adjusted(-20, -20, 20, 20).intersects(exposed))
            continue;
        QColor color =
            item.color.isValid()
                ? item.color
                : (m_selection.contains(item.elementId.isEmpty() ? item.id : item.elementId)
                       ? QColor("#1262bd")
                       : QColor("#20252b"));
        p.save();
        p.setTransform(item.transform, true);
        p.setPen(QPen(color, item.strokeWidth, item.penStyle));
        p.setBrush(color);
        switch (item.kind)
        {
        case LayoutItem::Kind::Text:
        {
            QFont font(item.fontFamily);
            font.setPixelSize(qRound(item.fontSize));
            font.setBold(item.bold);
            p.setFont(font);
            p.drawText(item.bounds, item.alignment, item.text);
            break;
        }
        case LayoutItem::Kind::Line:
        case LayoutItem::Kind::Curve:
            p.setBrush(Qt::NoBrush);
            p.drawPath(item.path);
            break;
        case LayoutItem::Kind::Dot:
            p.drawEllipse(item.bounds);
            break;
        }
        p.restore();
    }
    if (interactive && hasFocus())
        for (auto it = m_inputSlots.begin(); it != m_inputSlots.end(); ++it)
        {
            p.setPen(QPen(QColor("#8798ae"), 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(it.value(), 3, 3);
        }
    if (interactive && m_keyboardInput && !m_selection.isEmpty())
    {
        for (const auto &hit : m_layout.hits)
            if (m_selection.contains(hit.id) && (!hit.eventId.isEmpty() || atAppendPosition()))
            {
                p.setPen(QPen(QColor("#2572c3"), 1.5));
                p.drawLine(QPointF(hit.rect.right() + 7, hit.rect.top()),
                           QPointF(hit.rect.right() + 7, hit.rect.bottom()));
                break;
            }
    }
    if (interactive && m_selection.size() == 1)
    {
        auto element = m_layout.elements.value(m_selection.first());
        if (element && element->identity().movable)
        {
            auto rect = element->bounds(m_layout);
            p.setPen(QPen(QColor("#2878c8"), 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(rect);
            QPointF rotate(rect.center().x(), rect.top() - 18);
            p.drawLine(QPointF(rect.center().x(), rect.top()), rotate);
            p.setBrush(Qt::white);
            p.drawEllipse(rotate, 4, 4);
            p.drawRect(QRectF(rect.bottomRight() - QPointF(4, 4), QSizeF(8, 8)));
        }
        p.setPen(QPen(QColor("#c54b98"), 1, Qt::DashLine));
        for (const auto &guide : m_snapGuides)
            p.drawLine(guide);
    }
    if (interactive && hasFocus() && selectedLyric())
    {
        auto rect = selectedLyric()->rect;
        p.setPen(QPen(QColor("#2572c3"), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rect.adjusted(-2, -1, 2, 1), 2, 2);
        if (!m_preedit.isEmpty())
        {
            p.fillRect(rect, Qt::white);
            p.drawText(rect, Qt::AlignCenter, m_preedit);
        }
    }
    if (interactive && m_contentGesture == 2 && m_gestureMoved && m_dropValid)
    {
        p.setPen(QPen(QColor("#1b7acf"), 2));
        p.drawLine(m_dropBounds.topLeft(), m_dropBounds.bottomLeft());
        p.setBrush(QColor(30, 120, 220, 35));
        p.drawRect(m_dropBounds);
    }
    if (m_dragging)
    {
        p.setBrush(QColor(30, 120, 220, 25));
        p.setPen(QPen(QColor("#2878c8"), 1, Qt::DashLine));
        p.drawRect(QRectF(m_dragStart, m_dragEnd).normalized());
    }
}
void ScoreCanvas::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.scale(m_zoom, m_zoom);
    paintScore(painter,
               QRectF(event->rect().x() / m_zoom, event->rect().y() / m_zoom,
                      event->rect().width() / m_zoom, event->rect().height() / m_zoom),
               true);
}
void ScoreCanvas::selectId(const QString &id)
{
    m_selection = {id};
    m_rangeAnchor = id;
    m_preedit.clear();
    if (auto *slot = selectedLyric(); slot && !m_selectingLyrics)
        m_lyricEntry->select(*slot);
    for (const auto &hit : m_layout.hits)
        if (hit.id == id && hit.track >= 0)
            m_track = hit.track;
    for (auto *parent = parentWidget(); parent; parent = parent->parentWidget())
        if (auto *area = qobject_cast<QScrollArea *>(parent))
        {
            for (const auto &h : m_layout.hits)
                if (h.id == id)
                    area->ensureVisible(qRound(h.rect.center().x() * m_zoom),
                                        qRound(h.rect.center().y() * m_zoom), 40, 40);
            break;
        }
    QApplication::inputMethod()->update(Qt::ImQueryAll);
    update();
    emit selectionChanged();
}
QStringList ScoreCanvas::modelSelection() const
{
    QStringList result;
    for (const auto &id : m_selection)
    {
        if (id.startsWith("@lyric"))
            for (const auto &slot : m_layout.lyrics)
                if (slot.id == id && !slot.eventId.isEmpty())
                {
                    result << slot.eventId;
                    break;
                }
        auto element = m_layout.elements.value(id);
        auto model = element ? element->identity().modelId : id;
        if (!model.startsWith("@") && !model.isEmpty() && !result.contains(model))
            result << model;
    }
    return result;
}
void ScoreCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    setFocus();
    auto point = event->position() / m_zoom;
    m_contentGesture = 0;
    m_dropValid = false;
    m_transformMode = 0;
    if (m_selection.size() == 1)
    {
        auto element = m_layout.elements.value(m_selection.first());
        if (element && element->identity().movable)
        {
            auto rect = element->bounds(m_layout);
            if (QLineF(point, QPointF(rect.center().x(), rect.top() - 18)).length() <= 8 / m_zoom)
                m_transformMode = 1;
            else if (QLineF(point, rect.bottomRight()).length() <= 8 / m_zoom)
                m_transformMode = 2;
            if (m_transformMode)
            {
                m_dragElement = m_selection.first();
                m_dragLayout = m_layout;
                m_dragStart = point;
                m_transformOrigin = element->transformOrigin(m_layout);
                auto saved = m_document->viewSettings()["elementOffsets"]
                                 .toObject()[element->identity().key]
                                 .toObject();
                m_rotation = saved["rotation"].toDouble();
                m_scale = saved["scale"].toDouble(1);
                m_previewRotation = m_rotation;
                m_previewScale = m_scale;
                m_document->sealInputGroup();
                return;
            }
        }
    }
    for (auto it = m_inputSlots.begin(); it != m_inputSlots.end(); ++it)
        if (it.value().adjusted(-3, -3, 3, 3).contains(point))
        {
            selectAppendPosition(it.key());
            m_entry->selectTarget(true);
            return;
        }
    if (beginContentSelection(point, event->modifiers()))
        return;
    const auto *hit = m_layout.hitTest(point);
    if (hit)
    {
        for (const auto &slot : m_layout.lyrics)
            if (slot.id == hit->id)
            {
                selectId(slot.id);
                m_entry->selectTarget(slot.tail);
                m_keyboardInput = true;
                return;
            }
        auto element = m_layout.elements.value(hit->id);
        if (element && element->identity().movable)
        {
            selectId(hit->id);
            m_keyboardInput = false;
            m_document->sealInputGroup();
            m_dragElement = hit->id;
            m_dragLayout = m_layout;
            m_dragStart = point;
            m_elementDelta = {};
            return;
        }
        m_entry->selectTarget(false);
        m_keyboardInput = !hit->eventId.isEmpty();
        if (event->modifiers() & Qt::ControlModifier)
        {
            if (m_selection.contains(hit->id))
                m_selection.removeAll(hit->id);
            else
                m_selection << hit->id;
            m_track = hit->track;
            update();
            emit selectionChanged();
        }
        else
            selectId(hit->id);
    }
    else
    {
        m_selection.clear();
        m_dragStart = m_dragEnd = point;
        m_dragging = true;
        update();
        emit selectionChanged();
    }
}
void ScoreCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_contentGesture)
    {
        moveContentSelection(event);
        return;
    }
    if (!m_dragElement.isEmpty())
    {
        auto point = event->position() / m_zoom;
        m_layout = m_dragLayout;
        auto element = m_layout.elements.value(m_dragElement);
        if (m_transformMode == 1)
        {
            auto angle = [](QPointF p)
            { return std::atan2(p.y(), p.x()) * 180.0 / 3.141592653589793; };
            double delta =
                angle(point - m_transformOrigin) - angle(m_dragStart - m_transformOrigin);
            m_previewRotation = std::clamp(m_rotation + delta, -360.0, 360.0);
            if (!(event->modifiers() & Qt::AltModifier) &&
                std::abs(m_previewRotation - std::round(m_previewRotation / 15) * 15) < 3)
                m_previewRotation = std::round(m_previewRotation / 15) * 15;
            element->transform(m_layout, m_previewRotation - m_rotation, 1);
        }
        else if (m_transformMode == 2)
        {
            m_previewScale =
                std::clamp(m_scale * QLineF(point, m_transformOrigin).length() /
                               std::max(1.0, QLineF(m_dragStart, m_transformOrigin).length()),
                           0.25, 4.0);
            element->transform(m_layout, 0, m_previewScale / m_scale);
        }
        else
        {
            m_elementDelta = point - m_dragStart;
            m_snapGuides.clear();
            if (!(event->modifiers() & Qt::AltModifier))
            {
                auto moving = element->bounds(m_layout).translated(m_elementDelta);
                QVector<QRectF> references;
                for (const auto &hit : m_layout.hits)
                    if (hit.id != m_dragElement &&
                        std::abs(hit.rect.center().y() - moving.center().y()) < 800)
                        references << hit.rect;
                auto settings = LayoutSettings::fromJson(m_document->viewSettings());
                const qreal mm = 96.0 / 25.4;
                for (const auto &page : m_layout.pages)
                    if (page.intersects(moving))
                        references
                            << page.adjusted(settings.leftMm * mm, settings.topMm * mm,
                                             -settings.rightMm * mm, -settings.bottomMm * mm);
                auto snap = AlignmentSnapper::snap(moving, references, 6 / m_zoom);
                m_elementDelta += snap.correction;
                m_snapGuides = snap.guides;
            }
            element->translate(m_layout, m_elementDelta);
        }
        update();
        return;
    }
    if (m_dragging)
    {
        m_dragEnd = event->position() / m_zoom;
        update();
    }
}
void ScoreCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (releaseContentSelection(event))
        return;
    if (!m_dragElement.isEmpty())
    {
        auto id = m_dragElement;
        m_dragElement.clear();
        m_snapGuides.clear();
        if (m_transformMode)
            m_document->transformElement(id, m_previewRotation, m_previewScale);
        else if (!m_elementDelta.isNull())
            m_document->moveElement(id, m_elementDelta);
        m_transformMode = 0;
        m_dragLayout = {};
        return;
    }
    if (!m_dragging)
        return;
    m_dragging = false;
    auto rect = QRectF(m_dragStart, m_dragEnd).normalized();
    for (const auto &hit : m_layout.hits)
        if (!hit.eventId.isEmpty() && rect.contains(hit.rect.center()) &&
            !m_selection.contains(hit.id))
            m_selection << hit.id;
    update();
    emit selectionChanged();
}
void ScoreCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (auto *hit = m_layout.hitTest(event->position() / m_zoom))
    {
        auto element = m_layout.elements.value(hit->id);
        if (element && !element->identity().headerField.isEmpty())
        {
            emit editHeaderRequested(element->identity().headerField);
            return;
        }
        if (lyricSelected())
            return;
        if (hit->eventId.isEmpty())
            emit editMarkRequested();
        else
            emit editLyricsRequested();
    }
}
void ScoreCanvas::buildInputSlots()
{
    m_inputSlots.clear();
    for (const auto &voice : m_score.voices)
    {
        if (voice.percussion)
            continue;
        const EventData *last = nullptr;
        for (const auto &measure : m_score.measures)
            for (const auto &e : measure.events)
                if (e.track == voice.track && !e.grace && !e.logicalId.isEmpty() &&
                    (!last || e.position > last->position))
                    last = &e;
        const HitRegion *anchor = nullptr;
        for (const auto &hit : m_layout.hits)
            if (!hit.eventId.isEmpty() && hit.track == voice.track &&
                (!last || hit.eventId == last->id))
            {
                anchor = &hit;
                break;
            }
        if (!anchor)
            continue;
        QRectF rect(anchor->rect.right() + 15, anchor->rect.top(), 22, anchor->rect.height());
        if (last)
        {
            for (const auto &hit : m_layout.hits)
                if (!hit.eventId.isEmpty() && hit.track == voice.track &&
                    hit.tick == (last->position + last->length).ticks())
                {
                    rect.moveCenter(hit.rect.center());
                    break;
                }
        }
        m_inputSlots[voice.track] = rect;
        m_layout.hits << HitRegion{QString("@input:%1").arg(voice.track), {}, rect, voice.track};
    }
}
void ScoreCanvas::selectAppendPosition(int track)
{
    m_track = track;
    selectId(QString("@input:%1").arg(track));
    m_keyboardInput = true;
}
bool ScoreCanvas::atAppendPosition() const
{
    return m_selection.contains(QString("@input:%1").arg(m_track));
}
const LyricSlot *ScoreCanvas::selectedLyric() const
{
    for (const auto &slot : m_layout.lyrics)
        if (m_selection.contains(slot.id))
            return &slot;
    return nullptr;
}
void ScoreCanvas::startLyrics()
{
    int verse = m_lyricEntry->newVerse(m_track);
    if (verse < 0)
        return;
    for (const auto &slot : m_layout.lyrics)
        if (slot.track == m_track && slot.verse == verse)
        {
            selectId(slot.id);
            m_entry->selectTarget(true);
            setFocus();
            m_keyboardInput = true;
            return;
        }
}
void ScoreCanvas::selectNext(int direction)
{
    auto selected = contentSelection();
    if (selected.kind == SelectionKind::Measures && !selected.measures.isEmpty())
    {
        int index = direction < 0 ? selected.measures.first() : selected.measures.last();
        if (m_selection.size() == 1)
            index = std::clamp(index + direction, 0, int(m_score.measures.size()) - 1);
        m_selection = {"@measure:" + m_score.measures[index].id};
        m_rangeAnchor = m_selection.first();
        m_entry->selectTarget(false);
        update();
        emit selectionChanged();
        return;
    }
    if (m_selection.size() > 1)
    {
        QVector<SelectionTarget> targets;
        for (const auto &t : selectionTargets())
            if (m_selection.contains(t.id))
                targets << t;
        if (!targets.isEmpty())
        {
            std::stable_sort(targets.begin(), targets.end(),
                             [](const auto &a, const auto &b) { return a.order < b.order; });
            selectId(direction < 0 ? targets.first().id : targets.last().id);
            m_entry->selectTarget(selectedLyric() ? selectedLyric()->tail : atAppendPosition());
            return;
        }
    }
    if (m_selection.size() == 1)
    {
        auto element = m_layout.elements.value(m_selection.first());
        if (element && element->identity().movable)
        {
            selectSpatial(direction, 0);
            return;
        }
    }
    if (auto *lyric = selectedLyric())
    {
        auto current = *lyric;
        QVector<LyricSlot> lyricTargets;
        for (const auto &s : m_layout.lyrics)
            if (s.track == current.track && s.verse == current.verse && !s.eventId.isEmpty())
                lyricTargets << s;
        std::sort(lyricTargets.begin(), lyricTargets.end(),
                  [](const auto &a, const auto &b) { return a.tick < b.tick; });
        int index = lyricTargets.size();
        for (int i = 0; i < lyricTargets.size(); ++i)
            if (lyricTargets[i].id == current.id)
                index = i;
        int next = std::max(0, index + direction);
        if (next < lyricTargets.size())
        {
            selectId(lyricTargets[next].id);
            m_entry->selectTarget(lyricTargets[next].tail);
        }
        else
            for (const auto &s : m_layout.lyrics)
                if (s.track == current.track && s.verse == current.verse && s.eventId.isEmpty() &&
                    s.system == lyricTargets.last().system)
                {
                    selectId(s.id);
                    m_entry->selectTarget(true);
                    break;
                }
        return;
    }
    QVector<const HitRegion *> hits;
    QSet<QString> seen;
    for (const auto &h : m_layout.hits)
        if (h.track == m_track && !h.eventId.isEmpty() && !seen.contains(h.eventId))
        {
            bool written = false;
            for (const auto &m : m_score.measures)
                for (const auto &e : m.events)
                    if (e.id == h.eventId)
                        written = !e.logicalId.isEmpty() ||
                                  m_document->inputState()["protected"].toBool();
            if (!written)
                continue;
            seen.insert(h.eventId);
            hits << &h;
        }
    std::sort(hits.begin(), hits.end(), [](auto a, auto b) { return a->tick < b->tick; });
    int current = atAppendPosition() ? hits.size() : -1;
    for (int i = 0; i < hits.size(); ++i)
        if (m_selection.contains(hits[i]->id))
            current = i;
    int next = current < 0 ? 0 : std::max(0, current + direction);
    if (next >= hits.size())
        selectAppendPosition(m_track);
    else
        selectId(hits[next]->id);
    m_entry->selectTarget(atAppendPosition());
    m_keyboardInput = true;
}
void ScoreCanvas::selectVertical(int direction) { selectSpatial(0, direction); }
void ScoreCanvas::selectSpatial(int dx, int dy)
{
    const HitRegion *current = nullptr;
    for (const auto &h : m_layout.hits)
        if (m_selection.contains(h.id))
        {
            current = &h;
            break;
        }
    if (!current)
        return;
    const HitRegion *next = nullptr;
    qreal best = 1e12;
    for (const auto &h : m_layout.hits)
    {
        if (h.id == current->id || h.id.startsWith("@decoration:") ||
            h.id.startsWith("@lyric-number:"))
            continue;
        auto delta = h.rect.center() - current->rect.center();
        qreal forward = dx ? delta.x() * dx : delta.y() * dy;
        if (forward < 12)
            continue;
        qreal cross = std::abs(dx ? delta.y() : delta.x());
        qreal cost = forward + cross * 0.2;
        if (cost < best)
        {
            next = &h;
            best = cost;
        }
    }
    if (next)
    {
        selectId(next->id);
        m_keyboardInput = true;
        m_entry->selectTarget(selectedLyric() ? selectedLyric()->tail : atAppendPosition());
    }
}
void ScoreCanvas::selectEnd()
{
    if (auto *lyric = selectedLyric())
    {
        auto current = *lyric;
        for (const auto &s : m_layout.lyrics)
            if (s.track == current.track && s.verse == current.verse &&
                s.system == current.system && s.tail)
            {
                selectId(s.id);
                m_entry->selectTarget(true);
                return;
            }
    }
    else
    {
        selectAppendPosition(m_track);
        m_entry->selectTarget(true);
    }
}
bool ScoreCanvas::apply(EditRequest request)
{
    if (request.ids.isEmpty())
        request.ids = modelSelection();
    if (!m_document->edit(request))
    {
        emit operationFailed(m_document->error());
        return false;
    }
    return true;
}
void ScoreCanvas::copy()
{
    auto selected = contentSelection();
    auto bytes = m_document->copyFragment(selected);
    if (bytes.isEmpty())
    {
        emit operationFailed(
            tr("请在同一声部选取音符或歌词；连音组请完整选择，也可直接选择整小节"));
        return;
    }
    auto *mime = new QMimeData;
    mime->setData("application/x-jianpu-fragment", bytes);
    if (selected.kind == SelectionKind::Lyrics)
    {
        QString text;
        for (auto value : QJsonDocument::fromJson(bytes).object()["words"].toArray())
            text += value.toString();
        mime->setText(text);
    }
    QApplication::clipboard()->setMimeData(mime);
}
void ScoreCanvas::paste()
{
    if (QApplication::clipboard()->mimeData()->hasFormat("application/x-jianpu-fragment"))
    {
        pasteContent(false);
        return;
    }
    if (lyricSelected())
    {
        if (m_selection.size() > 1)
            m_lyricEntry->replaceRange(contentSelection().ids, QApplication::clipboard()->text());
        else
            m_lyricEntry->input(QApplication::clipboard()->text(), m_entry->mode());
        return;
    }
    auto bytes = QApplication::clipboard()->mimeData()->data("application/x-jianpu-selection");
    if (!bytes.isEmpty())
        apply({EditRequest::Kind::Paste, {}, 0, 0, QString::fromUtf8(bytes)});
}
void ScoreCanvas::deleteSelection()
{
    auto selected = contentSelection();
    if (selected.kind == SelectionKind::Measures ||
        (selected.kind == SelectionKind::Notes && m_selection.size() > 1))
    {
        if (!m_document->deleteFragment(selected))
            emit operationFailed(m_document->error());
    }
    else if (lyricSelected())
    {
        if (m_selection.size() > 1)
            m_lyricEntry->replaceRange(selected.ids, {});
        else
            m_lyricEntry->erase(false, m_entry->mode());
    }
    else
        apply({EditRequest::Kind::Delete});
}
void ScoreCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && !m_preedit.isEmpty())
    {
        m_preedit.clear();
        QApplication::inputMethod()->reset();
        update();
        event->accept();
        return;
    }
    if (event->key() >= Qt::Key_Left && event->key() <= Qt::Key_Down && m_selection.size() == 1 &&
        (event->modifiers() & Qt::AltModifier))
    {
        auto element = m_layout.elements.value(m_selection.first());
        if (element && element->identity().movable)
        {
            qreal step = event->modifiers() & Qt::ShiftModifier     ? 10.0
                         : event->modifiers() & Qt::ControlModifier ? 0.2
                                                                    : 1.0;
            QPointF delta(event->key() == Qt::Key_Left    ? -step
                          : event->key() == Qt::Key_Right ? step
                                                          : 0,
                          event->key() == Qt::Key_Up     ? -step
                          : event->key() == Qt::Key_Down ? step
                                                         : 0);
            m_document->moveElement(m_selection.first(), delta, "move:" + element->identity().key);
            event->accept();
            return;
        }
    }
    if (lyricSelected() && (event->modifiers() & Qt::AltModifier) && event->key() >= Qt::Key_Left &&
        event->key() <= Qt::Key_Down)
    {
        if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
            selectNext(event->key() == Qt::Key_Left ? -1 : 1);
        else
            selectVertical(event->key() == Qt::Key_Up ? -1 : 1);
        event->accept();
        return;
    }
    if (lyricSelected() && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)))
    {
        if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete)
        {
            if (m_selection.size() > 1)
                m_lyricEntry->replaceRange(contentSelection().ids, {});
            else
                m_lyricEntry->erase(event->key() == Qt::Key_Backspace, m_entry->mode());
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_End)
        {
            selectEnd();
            event->accept();
            return;
        }
        if (!event->text().isEmpty() && event->text().front().isPrint())
        {
            if (m_selection.size() > 1)
                m_lyricEntry->replaceRange(contentSelection().ids, event->text());
            else
                m_lyricEntry->input(event->text(), m_entry->mode());
            event->accept();
            return;
        }
    }
    using K = EditRequest::Kind;
    if (event->matches(QKeySequence::Undo))
        m_document->undo();
    else if (event->matches(QKeySequence::Redo))
        m_document->redo();
    else if (event->matches(QKeySequence::Copy))
        copy();
    else if (event->matches(QKeySequence::Cut) && lyricSelected())
    {
        copy();
        deleteSelection();
    }
    else if (event->key() == Qt::Key_V &&
             event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier))
        pasteInsert();
    else if (event->matches(QKeySequence::Paste))
        paste();
    else if (event->key() == Qt::Key_Escape)
    {
        if (!m_dragElement.isEmpty())
        {
            m_layout = m_dragLayout;
            m_dragElement.clear();
            m_dragLayout = {};
        }
        m_dragging = false;
        m_contentGesture = 0;
        m_dropValid = false;
        m_rangeAnchor.clear();
        setCursor(Qt::ArrowCursor);
        m_snapGuides.clear();
        m_transformMode = 0;
        m_selection.clear();
        m_entry->selectTarget(false);
        m_keyboardInput = false;
        update();
        emit selectionChanged();
    }
    else if (event->key() == Qt::Key_End)
    {
        selectEnd();
    }
    else if (event->key() == Qt::Key_Insert)
    {
        m_entry->setMode(m_entry->mode() == EntryMode::Insert ? EntryMode::Overwrite
                                                              : EntryMode::Insert);
    }
    else if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
        selectNext(event->key() == Qt::Key_Left ? -1 : 1);
    else if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)
    {
        bool up = event->key() == Qt::Key_Up;
        if (event->modifiers() & Qt::AltModifier)
            apply({event->modifiers() & Qt::ControlModifier
                       ? (up ? K::OctaveUp : K::OctaveDown)
                       : (up ? K::DiatonicUp : K::DiatonicDown)});
        else
            selectVertical(up ? -1 : 1);
    }
    else if (event->key() == Qt::Key_Backspace)
    {
        if (contentSelection().kind == SelectionKind::Measures || m_selection.size() > 1)
            deleteSelection();
        else
            m_entry->backspace();
    }
    else if (event->key() == Qt::Key_Delete)
        deleteSelection();
    else if (event->key() == Qt::Key_L)
        emit editLyricsRequested();
    else if (!event->text().isEmpty() && event->text()[0].isPrint())
    {
        if (m_selection.isEmpty())
        {
            selectAppendPosition(m_track);
            m_entry->selectTarget(true);
        }
        m_keyboardInput = true;
        m_entry->input(event->text());
    }
    else
    {
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}
void ScoreCanvas::inputMethodEvent(QInputMethodEvent *event)
{
    if (!lyricSelected())
    {
        event->ignore();
        return;
    }
    m_preedit = event->preeditString();
    if (!event->commitString().isEmpty())
        if (m_selection.size() > 1)
            m_lyricEntry->replaceRange(contentSelection().ids, event->commitString());
        else
            m_lyricEntry->input(event->commitString(), m_entry->mode());
    update();
    event->accept();
}
QVariant ScoreCanvas::inputMethodQuery(Qt::InputMethodQuery query) const
{
    auto *slot = selectedLyric();
    if (query == Qt::ImEnabled)
        return slot != nullptr;
    if (slot && query == Qt::ImCursorRectangle)
        return QRectF(slot->rect.topLeft() * m_zoom, slot->rect.size() * m_zoom);
    if (slot && query == Qt::ImSurroundingText)
        return slot->text;
    if (slot && query == Qt::ImCursorPosition)
        return slot->text.size();
    return QWidget::inputMethodQuery(query);
}
void ScoreCanvas::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier)
    {
        setZoom(m_zoom * std::pow(1.15, event->angleDelta().y() / 120.0));
        event->accept();
    }
    else
        QWidget::wheelEvent(event);
}
} // namespace jianpu
