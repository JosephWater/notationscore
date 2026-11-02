// SPDX-License-Identifier: GPL-3.0-only
#include "alignmentsnapper.h"
#include <algorithm>
#include <cmath>
namespace jianpu
{
SnapResult AlignmentSnapper::snap(const QRectF &moving, const QVector<QRectF> &references,
                                  qreal tolerance)
{
    SnapResult result;
    qreal bestX = tolerance + 1, bestY = tolerance + 1;
    QLineF gx, gy;
    for (const auto &reference : references)
    {
        for (qreal x : {moving.left(), moving.center().x(), moving.right()})
            for (qreal target : {reference.left(), reference.center().x(), reference.right()})
                if (std::abs(target - x) < bestX && std::abs(target - x) <= tolerance)
                {
                    bestX = std::abs(target - x);
                    result.correction.setX(target - x);
                    gx = QLineF(target, std::min(moving.top(), reference.top()) - 8, target,
                                std::max(moving.bottom(), reference.bottom()) + 8);
                }
        for (qreal y : {moving.top(), moving.center().y(), moving.bottom()})
            for (qreal target : {reference.top(), reference.center().y(), reference.bottom()})
                if (std::abs(target - y) < bestY && std::abs(target - y) <= tolerance)
                {
                    bestY = std::abs(target - y);
                    result.correction.setY(target - y);
                    gy = QLineF(std::min(moving.left(), reference.left()) - 8, target,
                                std::max(moving.right(), reference.right()) + 8, target);
                }
    }
    if (bestX <= tolerance)
        result.guides << gx;
    if (bestY <= tolerance)
        result.guides << gy;
    return result;
}
} // namespace jianpu
