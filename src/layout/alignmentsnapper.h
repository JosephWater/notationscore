// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QLineF>
#include <QRectF>
#include <QVector>
namespace jianpu
{
struct SnapResult
{
    QPointF correction;
    QVector<QLineF> guides;
};
class AlignmentSnapper
{
  public:
    static SnapResult snap(const QRectF &moving, const QVector<QRectF> &references,
                           qreal tolerance);
};
} // namespace jianpu
