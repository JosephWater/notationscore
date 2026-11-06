// SPDX-License-Identifier: GPL-3.0-only
#include "markplacement.h"
#include <algorithm>
namespace jianpu
{
qreal MarkPlacement::labelY(const QString &kind, qreal top, qreal bottom)
{
    if (kind == "dynamic")
        return bottom + 3;
    return top - 23;
}
qreal MarkPlacement::belowReserve(const QVector<MarkData> &marks, int track, qint64 start,
                                  qint64 end)
{
    bool dynamic = false, hairpin = false, pedal = false;
    for (const auto &mark : marks)
        if (mark.track == track && mark.tick < end && (mark.endTick > start || mark.tick >= start))
        {
            dynamic |= mark.kind == "dynamic";
            hairpin |= mark.kind == "hairpin";
            pedal |= mark.kind == "pedal";
        }
    return (dynamic ? 22.0 : 0.0) + (hairpin ? 16.0 : 0.0) + (pedal ? 26.0 : 0.0);
}
} // namespace jianpu
