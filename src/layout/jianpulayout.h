// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "core/scoretypes.h"
#include "scorelayout.h"
#include <QColor>
#include <QFont>
#include <QHash>
#include <QPainterPath>
#include <QRectF>
#include <memory>

namespace jianpu
{
class VisualElement;
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
    QString fontFamily;
    qreal strokeWidth = 1.2;
    Qt::PenStyle penStyle = Qt::SolidLine;
    Qt::Alignment alignment = Qt::AlignCenter;
    QColor color;
    QString elementId;
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
    struct SystemRange
    {
        MusicalTime start, end;
        QRectF bounds;
        QVector<int> tracks;
    };
    quint64 revision = 0;
    QVector<QRectF> pages;
    QVector<LayoutItem> items;
    QVector<HitRegion> hits;
    QVector<SystemRange> systems;
    QHash<QString, std::shared_ptr<const VisualElement>> elements;
    QSizeF size;
    QStringList warnings;
    const HitRegion *hitTest(QPointF point) const;
};
class JianpuLayout : public IScoreLayout
{
  public:
    LayoutResult layout(const ScoreSnapshot &score,
                        const QJsonObject &settings = {}) const override;
    static QString keyLabel(int fifths, bool minor);
    static QString noteText(const NoteData &note, int fifths, bool natural = false);
    static int octaveDots(const NoteData &note, int fifths);
};
} // namespace jianpu
