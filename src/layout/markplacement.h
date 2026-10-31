// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "core/scoretypes.h"
namespace jianpu
{
class MarkPlacement
{
  public:
    static qreal labelY(const QString &kind, qreal noteTop, qreal lyricBottom);
    static qreal belowReserve(const QVector<MarkData> &, int track, qint64 start, qint64 end);
};
} // namespace jianpu
