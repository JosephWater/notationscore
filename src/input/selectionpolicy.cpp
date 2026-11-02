// SPDX-License-Identifier: GPL-3.0-only
#include "selectionpolicy.h"
#include <QSet>
#include <algorithm>
namespace jianpu
{
QStringList SelectionPolicy::range(const QString &anchor, const QString &end,
                                   const QVector<SelectionTarget> &all)
{
    const SelectionTarget *a = nullptr, *b = nullptr;
    for (const auto &t : all)
    {
        if (t.id == anchor)
            a = &t;
        if (t.id == end)
            b = &t;
    }
    if (!a || !b || a->kind != b->kind || a->group != b->group)
        return {end};
    QVector<SelectionTarget> ordered;
    for (const auto &t : all)
        if (t.kind == a->kind && t.group == a->group)
            ordered << t;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto &x, const auto &y) { return x.order < y.order; });
    QStringList result;
    for (const auto &t : ordered)
        if (t.order >= std::min(a->order, b->order) && t.order <= std::max(a->order, b->order) &&
            !result.contains(t.id))
            result << t.id;
    return result;
}
QStringList SelectionPolicy::click(const QStringList &current, const QString &anchor,
                                   const SelectionTarget &target,
                                   const QVector<SelectionTarget> &all, bool control, bool shift)
{
    QStringList result = shift ? range(anchor, target.id, all) : QStringList{target.id};
    if (control)
    {
        result = current;
        if (shift)
        {
            for (const auto &id : range(anchor, target.id, all))
                if (!result.contains(id))
                    result << id;
        }
        else if (result.contains(target.id))
            result.removeAll(target.id);
        else
            result << target.id;
    }
    return result;
}
QStringList SelectionPolicy::rectangle(const QRectF &rect, const SelectionTarget &anchor,
                                       const QVector<SelectionTarget> &all)
{
    QStringList result;
    for (const auto &t : all)
        if (t.kind == anchor.kind && t.group == anchor.group && rect.intersects(t.rect) &&
            !result.contains(t.id))
            result << t.id;
    return result;
}
} // namespace jianpu
