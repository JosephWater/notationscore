// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "core/scoretypes.h"
#include <QRectF>
#include <QStringList>
#include <QVector>
namespace jianpu
{
struct SelectionTarget
{
    QString id, group;
    SelectionKind kind = SelectionKind::None;
    MusicalTime order;
    QRectF rect;
};
class SelectionPolicy
{
  public:
    static QStringList click(const QStringList &current, const QString &anchor,
                             const SelectionTarget &target, const QVector<SelectionTarget> &all,
                             bool control, bool shift);
    static QStringList range(const QString &anchor, const QString &end,
                             const QVector<SelectionTarget> &all);
    static QStringList rectangle(const QRectF &, const SelectionTarget &anchor,
                                 const QVector<SelectionTarget> &all);
};
} // namespace jianpu
