// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "core/scoretypes.h"
#include <QFont>
#include <QHash>
#include <QPainterPath>
#include <QRectF>

namespace jianpu
{
struct LayoutItem
{
    enum class Kind
    {
        Text,
        Line,
        Curve,
        Dot
    };
    Kind kind = Kind::Text;
    QString id;
    QString text;
    QRectF bounds;
    QPainterPath path;
    qreal fontSize = 24;
    int track = -1;
    bool bold = false;
};
struct HitRegion
{
    QString id;
    QString eventId;
    QRectF rect;
    int track = 0;
    int measure = 0;
    qint64 tick = 0;
    bool note = false;
};
struct LayoutResult
{
    quint64 revision = 0;
    QVector<QRectF> pages;
    QVector<LayoutItem> items;
    QVector<HitRegion> hits;
    QSizeF size;
    const HitRegion *hitTest(QPointF point) const;
};
class JianpuLayout
{
  public:
    LayoutResult layout(const ScoreSnapshot &score, const QJsonObject &settings = {}) const;
    static QString keyLabel(int fifths, bool minor);
    static QString noteText(const NoteData &note, int fifths, bool natural = false);
    static int octaveDots(const NoteData &note, int fifths);
};
} // namespace jianpu
