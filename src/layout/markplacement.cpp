// SPDX-License-Identifier: GPL-3.0-only
#include "markplacement.h"
#include <algorithm>
namespace jianpu
{
qreal MarkPlacement::labelY(const QString &kind, qreal top, qreal bottom)
{
    if (kind == "dynamic")
        return bottom + 16;
    if (kind == "articulation" || kind == "fermata")
        return top - 23;
    if (kind == "marker" || kind == "jump")
        return top - 58;
    return top - 42;
}
qreal MarkPlacement::belowReserve(const QVector<MarkData> &marks, int track, qint64 start,
                                  qint64 end)
{
    qreal reserve = 0;
    for (const auto &mark : marks)
        if (mark.track == track && mark.tick < end && (mark.endTick > start || mark.tick >= start))
        {
            if (mark.kind == "hairpin")
                reserve = std::max(reserve, 60.0);
            else if (mark.kind == "dynamic")
                reserve = std::max(reserve, 30.0);
        }
    return reserve;
}
} // namespace jianpu
