// SPDX-License-Identifier: GPL-3.0-only
#include "scorecanvas.h"
#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
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
    setObjectName("scoreCanvas");
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("简谱编辑画布"));
    setToolTip(tr("点击选择；Shift 扩展范围；1–7 改音高，0 休止；N 连续输入；方向键移动／改音高"));
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
    m_layout = JianpuLayout().layout(m_score, m_document->viewSettings());
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
                m_anchor = h.id;
                break;
            }
    if (m_selection.isEmpty())
        m_anchor.clear();
    resize((m_layout.size * m_zoom).toSize().expandedTo(QSize(1, 1)));
    updateGeometry();
    update();
    emit selectionChanged();
}
void ScoreCanvas::setActiveTrack(int track)
{
    m_track = track;
    m_selection.clear();
    m_anchor.clear();
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
    m_denominator = denominator;
    m_dots = dots;
}
void ScoreCanvas::paintScore(QPainter &p, const QRectF &exposed) const
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
    for (const auto &hit : m_layout.hits)
        if (m_selection.contains(hit.id))
            p.drawRoundedRect(hit.rect.adjusted(-2, -2, 2, 2), 3, 3);
    for (const auto &item : m_layout.items)
    {
        if (!item.bounds.adjusted(-20, -20, 20, 20).intersects(exposed))
            continue;
        QColor color = item.color.isValid() ? item.color
                                            : (m_selection.contains(item.id) ? QColor("#1262bd")
                                                                             : QColor("#20252b"));
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
    }
    if (m_keyboardInput && !m_selection.isEmpty())
    {
        for (const auto &hit : m_layout.hits)
            if (m_selection.contains(hit.id))
            {
                p.setPen(QPen(QColor("#2572c3"), 1.5));
                p.drawLine(QPointF(hit.rect.right() + 7, hit.rect.top()),
                           QPointF(hit.rect.right() + 7, hit.rect.bottom()));
                break;
            }
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
    paintScore(painter, QRectF(event->rect().x() / m_zoom, event->rect().y() / m_zoom,
                               event->rect().width() / m_zoom, event->rect().height() / m_zoom));
}
void ScoreCanvas::selectId(const QString &id)
{
    m_selection = {id};
    m_anchor = id;
    for (const auto &hit : m_layout.hits)
        if (hit.id == id)
            m_track = hit.track;
    update();
    emit selectionChanged();
}
void ScoreCanvas::rangeSelect(const HitRegion &hit)
{
    const HitRegion *anchor = nullptr;
    for (const auto &h : m_layout.hits)
        if (h.id == m_anchor)
        {
            anchor = &h;
            break;
        }
    if (!anchor)
    {
        selectId(hit.id);
        return;
    }
    qint64 start = std::min(anchor->tick, hit.tick), end = std::max(anchor->tick, hit.tick);
    int lowTrack = std::min(anchor->track, hit.track),
        highTrack = std::max(anchor->track, hit.track);
    m_selection.clear();
    for (const auto &h : m_layout.hits)
        if (!h.eventId.isEmpty() && h.tick >= start && h.tick <= end && h.track >= lowTrack &&
            h.track <= highTrack && !m_selection.contains(h.id))
            m_selection << h.id;
    std::stable_sort(m_selection.begin(), m_selection.end(),
                     [&](const auto &a, const auto &b)
                     {
                         auto tickFor = [&](const QString &id)
                         {
                             for (const auto &h : m_layout.hits)
                                 if (h.id == id)
                                     return h.tick;
                             return qint64(0);
                         };
                         return tickFor(a) < tickFor(b);
                     });
    update();
    emit selectionChanged();
}
void ScoreCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    setFocus();
    auto point = event->position() / m_zoom;
    const auto *hit = m_layout.hitTest(point);
    if (hit)
    {
        if (event->modifiers() & Qt::ShiftModifier)
            rangeSelect(*hit);
        else if (event->modifiers() & Qt::ControlModifier)
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
        m_anchor.clear();
        m_dragStart = m_dragEnd = point;
        m_dragging = true;
        update();
        emit selectionChanged();
    }
}
void ScoreCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging)
    {
        m_dragEnd = event->position() / m_zoom;
        update();
    }
}
void ScoreCanvas::mouseReleaseEvent(QMouseEvent *)
{
    if (!m_dragging)
        return;
    m_dragging = false;
    auto rect = QRectF(m_dragStart, m_dragEnd).normalized();
    for (const auto &hit : m_layout.hits)
        if (!hit.eventId.isEmpty() && rect.contains(hit.rect.center()) &&
            !m_selection.contains(hit.id))
            m_selection << hit.id;
    if (!m_selection.isEmpty())
        m_anchor = m_selection.first();
    update();
    emit selectionChanged();
}
void ScoreCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (auto *hit = m_layout.hitTest(event->position() / m_zoom))
    {
        if (hit->eventId.isEmpty())
            emit editMarkRequested();
        else
            emit editLyricsRequested();
    }
}
void ScoreCanvas::selectNext(int direction)
{
    QVector<const HitRegion *> hits;
    QSet<QString> seen;
    for (const auto &h : m_layout.hits)
        if (h.track == m_track && !h.eventId.isEmpty() && !seen.contains(h.eventId))
        {
            seen.insert(h.eventId);
            hits << &h;
        }
    std::sort(hits.begin(), hits.end(), [](auto a, auto b) { return a->tick < b->tick; });
    if (hits.isEmpty())
        return;
    int current = -1;
    for (int i = 0; i < hits.size(); ++i)
        if (m_selection.contains(hits[i]->id))
            current = i;
    int next = current < 0 ? 0 : std::clamp(current + direction, 0, int(hits.size()) - 1);
    selectId(hits[next]->id);
}
bool ScoreCanvas::apply(EditRequest request)
{
    if (request.ids.isEmpty())
        request.ids = m_selection;
    if (!m_document->edit(request))
    {
        emit operationFailed(m_document->error());
        return false;
    }
    return true;
}
void ScoreCanvas::copy()
{
    const auto bytes = m_document->copy(m_selection);
    if (bytes.isEmpty())
        return;
    auto *mime = new QMimeData;
    mime->setData("application/x-jianpu-selection", bytes);
    QApplication::clipboard()->setMimeData(mime);
}
void ScoreCanvas::paste()
{
    auto bytes = QApplication::clipboard()->mimeData()->data("application/x-jianpu-selection");
    if (!bytes.isEmpty())
        apply({EditRequest::Kind::Paste, {}, 0, 0, QString::fromUtf8(bytes)});
}
void ScoreCanvas::keyPressEvent(QKeyEvent *event)
{
    using K = EditRequest::Kind;
    if (event->matches(QKeySequence::Undo))
        m_document->undo();
    else if (event->matches(QKeySequence::Redo))
        m_document->redo();
    else if (event->matches(QKeySequence::Copy))
        copy();
    else if (event->matches(QKeySequence::Paste))
        paste();
    else if (event->key() == Qt::Key_N)
    {
        m_input = !m_input;
        setCursor(m_input ? Qt::CrossCursor : Qt::ArrowCursor);
    }
    else if (event->key() == Qt::Key_Escape)
    {
        m_input = false;
        m_selection.clear();
        setCursor(Qt::ArrowCursor);
        update();
        emit selectionChanged();
    }
    else if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
        selectNext(event->key() == Qt::Key_Left ? -1 : 1);
    else if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)
    {
        bool up = event->key() == Qt::Key_Up;
        bool octave = event->modifiers() & Qt::ControlModifier;
        apply(
            {octave ? (up ? K::OctaveUp : K::OctaveDown) : (up ? K::DiatonicUp : K::DiatonicDown)});
    }
    else if (event->key() >= Qt::Key_0 && event->key() <= Qt::Key_7)
    {
        if (m_selection.isEmpty())
            selectNext();
        qint64 tick = -1;
        for (const auto &hit : m_layout.hits)
            if (m_selection.contains(hit.id))
            {
                tick = hit.tick;
                break;
            }
        if (apply({event->key() == Qt::Key_0 ? K::InsertRest : K::SetDegree,
                   {},
                   event->key() - Qt::Key_0,
                   m_denominator,
                   {},
                   m_dots}) &&
            m_input)
        {
            for (const auto &hit : m_layout.hits)
                if (hit.track == m_track && hit.tick > tick && !hit.eventId.isEmpty())
                {
                    selectId(hit.id);
                    break;
                }
        }
    }
    else if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
        apply({K::Delete});
    else if (event->key() == Qt::Key_L)
        emit editLyricsRequested();
    else
    {
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
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
