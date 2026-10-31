// SPDX-License-Identifier: GPL-3.0-only
#include "scorecanvas.h"
#include "layout/visualelement.h"
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
    m_entry = new ScoreEntryController(document, this, this);
    connect(m_entry, &ScoreEntryController::error, this, &ScoreCanvas::operationFailed);
    setObjectName("scoreCanvas");
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("简谱编辑画布"));
    setToolTip(tr("点击音符后依次覆盖；Ctrl+点击或框选多选；End 到末尾追加；方向键选择；Insert "
                  "切换插入；Alt+↑↓ 改音高"));
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
    for (const auto &hit : m_layout.hits)
        if (interactive && m_selection.contains(hit.id))
            p.drawRoundedRect(hit.rect.adjusted(-2, -2, 2, 2), 3, 3);
    for (const auto &item : m_layout.items)
    {
        if (!item.bounds.adjusted(-20, -20, 20, 20).intersects(exposed))
            continue;
        QColor color =
            item.color.isValid()
                ? item.color
                : (m_selection.contains(item.elementId.isEmpty() ? item.id : item.elementId)
                       ? QColor("#1262bd")
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
    for (const auto &hit : m_layout.hits)
        if (hit.id == id)
            m_track = hit.track;
    update();
    emit selectionChanged();
}
QStringList ScoreCanvas::modelSelection() const
{
    QStringList result;
    for (const auto &id : m_selection)
    {
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
    for (auto it = m_inputSlots.begin(); it != m_inputSlots.end(); ++it)
        if (it.value().adjusted(-3, -3, 3, 3).contains(point))
        {
            selectAppendPosition(it.key());
            m_entry->selectTarget(true);
            return;
        }
    const auto *hit = m_layout.hitTest(point);
    if (hit)
    {
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
    if (!m_dragElement.isEmpty())
    {
        m_elementDelta = event->position() / m_zoom - m_dragStart;
        m_layout = m_dragLayout;
        m_layout.elements.value(m_dragElement)->translate(m_layout, m_elementDelta);
        update();
        return;
    }
    if (m_dragging)
    {
        m_dragEnd = event->position() / m_zoom;
        update();
    }
}
void ScoreCanvas::mouseReleaseEvent(QMouseEvent *)
{
    if (!m_dragElement.isEmpty())
    {
        auto id = m_dragElement;
        m_dragElement.clear();
        if (!m_elementDelta.isNull())
            m_document->moveElement(id, m_elementDelta);
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
void ScoreCanvas::selectNext(int direction)
{
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
void ScoreCanvas::selectVertical(int direction)
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
    qreal nearestRow = 1e9, nearestX = 1e9;
    for (const auto &h : m_layout.hits)
    {
        if (h.eventId.isEmpty() && !h.id.startsWith("@input:"))
            continue;
        qreal dy = (h.rect.center().y() - current->rect.center().y()) * direction;
        if (dy < 12)
            continue;
        qreal dx = std::abs(h.rect.center().x() - current->rect.center().x());
        if (dy < nearestRow - 5 || (std::abs(dy - nearestRow) <= 5 && dx < nearestX))
        {
            next = &h;
            nearestRow = dy;
            nearestX = dx;
        }
    }
    if (next)
    {
        selectId(next->id);
        m_keyboardInput = true;
        m_entry->selectTarget(atAppendPosition());
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
    if (event->key() >= Qt::Key_Left && event->key() <= Qt::Key_Down && m_selection.size() == 1)
    {
        auto element = m_layout.elements.value(m_selection.first());
        if (element && element->identity().movable)
        {
            qreal step = event->modifiers() & Qt::ShiftModifier ? 10.0
                         : event->modifiers() & Qt::AltModifier ? 0.2
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
    using K = EditRequest::Kind;
    if (event->matches(QKeySequence::Undo))
        m_document->undo();
    else if (event->matches(QKeySequence::Redo))
        m_document->redo();
    else if (event->matches(QKeySequence::Copy))
        copy();
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
        m_selection.clear();
        m_entry->selectTarget(false);
        m_keyboardInput = false;
        update();
        emit selectionChanged();
    }
    else if (event->key() == Qt::Key_End)
    {
        selectAppendPosition(m_track);
        m_entry->selectTarget(true);
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
        m_entry->backspace();
    else if (event->key() == Qt::Key_Delete)
        apply({K::Delete});
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
