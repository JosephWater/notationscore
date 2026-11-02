// SPDX-License-Identifier: GPL-3.0-only
#include "visualelement.h"
#include <QFontMetricsF>
#include <QJsonObject>
#include <QPainterPathStroker>
#include <QSet>
#include <algorithm>
namespace jianpu
{
namespace
{
QRectF textInk(const LayoutItem &item)
{
    QFont font(item.fontFamily);
    font.setPixelSize(qRound(item.fontSize));
    font.setBold(item.bold);
    QFontMetricsF metrics(font);
    auto rect = metrics.tightBoundingRect(item.text);
    double x = item.bounds.left();
    auto advance = metrics.horizontalAdvance(item.text);
    if (item.alignment & Qt::AlignRight)
        x += item.bounds.width() - advance;
    else if (item.alignment & Qt::AlignHCenter)
        x += (item.bounds.width() - advance) / 2;
    double baseline = item.bounds.top() + metrics.ascent();
    if (item.alignment & Qt::AlignVCenter)
        baseline += (item.bounds.height() - metrics.height()) / 2;
    else if (item.alignment & Qt::AlignBottom)
        baseline += item.bounds.height() - metrics.height();
    return rect.translated(x, baseline).adjusted(-3, -3, 3, 3);
}
QPainterPath primitiveShape(const LayoutItem &item)
{
    QPainterPath path;
    if (item.kind == LayoutItem::Kind::Text)
        path.addRect(textInk(item));
    else if (item.kind == LayoutItem::Kind::Dot)
        path.addEllipse(item.bounds.adjusted(-2, -2, 2, 2));
    else
    {
        QPainterPathStroker stroke;
        stroke.setWidth(std::max(7.0, item.strokeWidth + 5));
        path = stroke.createStroke(item.path);
    }
    return path;
}
} // namespace
VisualElement::VisualElement(ElementIdentity identity, QVector<int> items)
    : m_identity(std::move(identity)), m_items(std::move(items))
{
}
QRectF VisualElement::bounds(const LayoutResult &layout) const
{
    return hitShape(layout).boundingRect();
}
void VisualElement::translate(LayoutResult &layout, QPointF delta) const
{
    for (int index : m_items)
    {
        auto &item = layout.items[index];
        item.bounds.translate(delta);
        item.path.translate(delta);
        QTransform before, after;
        before.translate(-delta.x(), -delta.y());
        after.translate(delta.x(), delta.y());
        item.transform = before * item.transform * after;
    }
    for (auto &hit : layout.hits)
        if (hit.id == m_identity.id)
            hit.rect.translate(delta);
}
QPointF VisualElement::transformOrigin(const LayoutResult &layout) const
{
    QRectF bounds;
    for (int index : m_items)
        bounds = bounds.united(primitiveShape(layout.items[index]).boundingRect());
    return bounds.center();
}
void VisualElement::transform(LayoutResult &layout, qreal rotation, qreal scale) const
{
    auto origin = transformOrigin(layout);
    QTransform t;
    t.translate(origin.x(), origin.y());
    t.rotate(rotation);
    t.scale(scale, scale);
    t.translate(-origin.x(), -origin.y());
    for (int index : m_items)
        layout.items[index].transform *= t;
    for (auto &hit : layout.hits)
        if (hit.id == m_identity.id)
            hit.rect = bounds(layout);
}
QPainterPath TextVisualElement::hitShape(const LayoutResult &layout) const
{
    QPainterPath shape;
    for (int index : m_items)
        shape =
            shape.united(layout.items[index].transform.map(primitiveShape(layout.items[index])));
    return shape;
}
QPainterPath SymbolVisualElement::hitShape(const LayoutResult &layout) const
{
    QPainterPath shape;
    for (int index : m_items)
        shape.addPath(layout.items[index].transform.map(primitiveShape(layout.items[index])));
    return shape;
}
void VisualElementRegistry::build(LayoutResult &layout, const ScoreSnapshot &score,
                                  const QJsonObject &settings)
{
    QHash<QString, ElementIdentity> identities;
    QHash<QString, QVector<int>> groups;
    QHash<QString, QString> anchors, keys;
    QHash<QString, const EventData *> events;
    QSet<QString> notes, measures, marks;
    for (const auto &measure : score.measures)
    {
        measures.insert(measure.id);
        for (const auto &event : measure.events)
        {
            events[event.id] = &event;
            QString anchor = event.logicalId.isEmpty() ? event.id : event.logicalId;
            anchors[event.id] = anchor;
            for (const auto &note : event.notes)
            {
                notes.insert(note.id);
                anchors[note.id] = event.notes.size() == 1 ? anchor : anchor + ":" + note.id;
            }
        }
    }
    QHash<QString, int> markOrdinals;
    for (const auto &mark : score.marks)
    {
        marks.insert(mark.id);
        QString anchor = mark.startId;
        if (anchor.isEmpty())
            for (const auto &measure : score.measures)
                for (const auto &event : measure.events)
                    if (!event.grace && event.track == mark.track && event.tick == mark.tick)
                        anchor = event.id;
        QString base = "mark:" + mark.kind + ":" + anchors.value(anchor, anchor);
        keys[mark.id] = base + ":" + QString::number(markOrdinals[base]++);
    }
    QHash<QString, int> ordinals;
    for (int index = 0; index < layout.items.size(); ++index)
    {
        auto &item = layout.items[index];
        QString id = item.elementId.isEmpty() ? item.id : item.elementId;
        ElementIdentity info;
        info.id = id;
        info.modelId = item.id;
        info.track = item.track;
        info.key = keys.value(id, id);
        info.label = item.text;
        if (id.startsWith("@"))
        {
            if (id.startsWith("@lyric"))
                info.movable = false;
            if (id.startsWith("@title"))
                info.headerField = "B";
            else if (id.startsWith("@author"))
                info.headerField = "Z";
            else if (id.startsWith("@key"))
                info.headerField = "D";
            else if (id.startsWith("@time"))
                info.headerField = "P";
        }
        else if (notes.contains(id) && item.kind == LayoutItem::Kind::Text)
            info.movable = false;
        else if (events.contains(id) && item.kind == LayoutItem::Kind::Text)
            info.movable = false;
        else if (measures.contains(id))
        {
            info.id = "@bar:" + id + ":" + QString::number(item.track);
            QString number;
            for (const auto &m : score.measures)
                if (m.id == id)
                    number = QString::number(m.number);
            info.key = "bar:" + number + ":" + QString::number(item.track);
            info.label = QStringLiteral("小节线 / 小节号");
        }
        else if (!marks.contains(id))
        {
            QString anchor = anchors.value(item.id, item.id);
            QString base = "@decoration:" + anchor + ":" + QString::number(int(item.kind));
            info.id = info.key = base + ":" + QString::number(ordinals[base]++);
        }
        if (info.id.isEmpty())
            info.id = info.key = "@aux:" + QString::number(index);
        item.elementId = info.id;
        if (events.contains(item.id))
            info.tick = events[item.id]->tick;
        for (const auto &mark : score.marks)
            if (mark.id == item.id)
                info.tick = mark.tick;
        identities[info.id] = info;
        groups[info.id] << index;
    }
    layout.hits.erase(std::remove_if(layout.hits.begin(), layout.hits.end(),
                                     [](const auto &hit) { return hit.eventId.isEmpty(); }),
                      layout.hits.end());
    auto ordered = groups.keys();
    std::sort(ordered.begin(), ordered.end(),
              [&](const auto &a, const auto &b) { return groups[a].last() < groups[b].last(); });
    for (const auto &id : ordered)
    {
        const auto &indices = groups[id];
        auto info = identities[id];
        bool textOnly = std::all_of(indices.begin(), indices.end(), [&](int i)
                                    { return layout.items[i].kind == LayoutItem::Kind::Text; });
        std::shared_ptr<const VisualElement> element;
        if (textOnly)
            element = std::make_shared<TextVisualElement>(info, indices);
        else
            element = std::make_shared<SymbolVisualElement>(info, indices);
        layout.elements[info.id] = element;
        if (info.movable)
            layout.hits << HitRegion{info.id,   {},   element->bounds(layout), info.track, 0,
                                     info.tick, false};
    }
    auto offsets = settings.value("elementOffsets").toObject();
    for (const auto &element : layout.elements)
    {
        auto offset = offsets.value(element->identity().key).toObject();
        if (element->identity().movable)
        {
            element->translate(layout, QPointF(offset["x"].toDouble(), offset["y"].toDouble()));
            element->transform(layout, offset["rotation"].toDouble(),
                               offset["scale"].toDouble(1.0));
        }
    }
}
} // namespace jianpu
