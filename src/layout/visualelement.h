// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "jianpulayout.h"
namespace jianpu
{
struct ElementIdentity
{
    QString id, key, label, modelId;
    QString headerField;
    int track = -1;
    qint64 tick = 0;
    bool movable = true;
};
// One semantic element may own several drawing primitives (a time signature,
// repeat bar, or a spanner continued on several systems). Movement is atomic.
class VisualElement
{
  public:
    VisualElement(ElementIdentity identity, QVector<int> items);
    virtual ~VisualElement() = default;
    const ElementIdentity &identity() const { return m_identity; }
    virtual QPainterPath hitShape(const LayoutResult &) const = 0;
    QRectF bounds(const LayoutResult &) const;
    void translate(LayoutResult &, QPointF) const;
    QPointF transformOrigin(const LayoutResult &) const;
    void transform(LayoutResult &, qreal rotation, qreal scale) const;

  protected:
    ElementIdentity m_identity;
    QVector<int> m_items;
};
class TextVisualElement final : public VisualElement
{
  public:
    using VisualElement::VisualElement;
    QPainterPath hitShape(const LayoutResult &) const override;
};
class SymbolVisualElement final : public VisualElement
{
  public:
    using VisualElement::VisualElement;
    QPainterPath hitShape(const LayoutResult &) const override;
};
class VisualElementRegistry
{
  public:
    static void build(LayoutResult &, const ScoreSnapshot &, const QJsonObject &settings);
};
} // namespace jianpu
