// SPDX-License-Identifier: GPL-3.0-only
#include "jianpulayout.h"
#include "layoutsettings.h"
#include <QFontMetricsF>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace jianpu
{
namespace
{
constexpr qreal PageWidth = 794;
int positiveMod(int a, int b) { return (a % b + b) % b; }
int tonicStep(int fifths) { return positiveMod(fifths * 4, 7); }
int keyAlter(int step, int fifths)
{
    static constexpr int sharps[] = {3, 0, 4, 1, 5, 2, 6};
    static constexpr int flats[] = {6, 2, 5, 1, 4, 0, 3};
    for (int i = 0; i < std::min(7, std::abs(fifths)); ++i)
        if ((fifths >= 0 ? sharps[i] : flats[i]) == step)
            return fifths >= 0 ? 1 : -1;
    return 0;
}
qreal restTicks(const EventData &event)
{
    return event.denominator == 0 ? event.duration
                                  : 1920.0 / event.denominator * (2.0 - std::pow(0.5, event.dots));
}
} // namespace
const HitRegion *LayoutResult::hitTest(QPointF p) const
{
    for (auto it = hits.crbegin(); it != hits.crend(); ++it)
        if (it->rect.contains(p))
            return &*it;
    return nullptr;
}
QString JianpuLayout::keyLabel(int fifths, bool minor)
{
    static const QStringList major{"C♭", "G♭", "D♭", "A♭", "E♭", "B♭", "F", "C",
                                   "G",  "D",  "A",  "E",  "B",  "F♯", "C♯"};
    static const QStringList minors{"A♭", "E♭", "B♭", "F",  "C",  "G",  "D", "A",
                                    "E",  "B",  "F♯", "C♯", "G♯", "D♯", "A♯"};
    int index = std::clamp(fifths, -7, 7) + 7;
    return QStringLiteral("%1 = %2").arg(minor ? "6" : "1", (minor ? minors : major).at(index));
}
QString JianpuLayout::noteText(const NoteData &n, int fifths, bool natural)
{
    int delta = n.alter - keyAlter(n.step, fifths);
    QString acc;
    if (delta)
    {
        if (delta > 0)
            acc = QString(delta, QChar(0x266f));
        else
            acc = QString(-delta, QChar(0x266d));
    }
    else if (natural)
        acc = QStringLiteral("♮");
    return acc + QString::number(positiveMod(n.step - tonicStep(fifths), 7) + 1);
}
int JianpuLayout::octaveDots(const NoteData &n, int fifths)
{
    // Middle-octave tonic starts at C4..B4 according to the key's spelling.
    return int(std::floor((n.octave * 7 + n.step - (4 * 7 + tonicStep(fifths))) / 7.0));
}

LayoutResult JianpuLayout::layout(const ScoreSnapshot &inputScore,
                                  const QJsonObject &settings) const
{
    ScoreSnapshot score = inputScore;
    auto timeOf = [](const EventData &e)
    { return e.length == MusicalTime() ? MusicalTime(e.tick, 1920) : e.position; };
    const auto style = LayoutSettings::fromJson(settings);
    const qreal mm = 96.0 / 25.4;
    qreal pageWidth = (style.landscape ? style.heightMm : style.widthMm) * mm;
    const qreal pageHeight = (style.landscape ? style.widthMm : style.heightMm) * mm;
    const qreal Margin = style.leftMm * mm;
    const qreal contentWidth = std::max(80.0, pageWidth - Margin - style.rightMm * mm);
    const qreal noteSize = style.notePt * 4 / 3, lyricSize = style.lyricPt * 4 / 3;
    auto lyricFont = style.font(lyricSize, style.lyricBold);
    if (!style.lyricFont.isEmpty())
        lyricFont.setFamily(style.lyricFont);
    const QFontMetricsF lyricMetrics(lyricFont);
    const qreal lyricHeight = std::max(lyricSize * 1.5, lyricMetrics.height());
    const qreal lyricAdvance = lyricHeight * style.verseGap;
    const qreal scale = noteSize / 26;
    auto textWidth = [&](const QString &t, qreal sz)
    {
        auto f = style.font(sz, sz == lyricSize ? style.lyricBold : false);
        if (sz == lyricSize && !style.lyricFont.isEmpty())
            f.setFamily(style.lyricFont);
        return QFontMetricsF(f).horizontalAdvance(t);
    };
    LayoutResult out;
    out.revision = score.revision;
    // A very dense measure can be continued at beat boundaries without changing music.
    QVector<MeasureData> displayMeasures;
    for (const auto &measure : score.measures)
    {
        QMap<qint64, int> onsets;
        for (const auto &event : measure.events)
            if (!event.grace)
                onsets[event.tick] = 1;
        const int capacity = std::max(3, int(contentWidth / (noteSize * 2.15 * style.noteGap)));
        QVector<qint64> cuts{measure.tick};
        int count = 0;
        qint64 beat =
            (measure.denominator == 8 && measure.numerator >= 6 && measure.numerator % 3 == 0)
                ? 720
                : 1920 / measure.denominator;
        auto keys = onsets.keys();
        for (int i = 0; i < keys.size(); ++i)
        {
            if (count >= capacity)
            {
                qint64 cut = keys[i] - ((keys[i] - measure.tick) % std::max<qint64>(1, beat));
                if (cut <= cuts.last())
                    cut = keys[i];
                cuts << cut;
                count = 0;
                for (int j = i - 1; j >= 0 && keys[j] >= cut; --j)
                    ++count;
            }
            ++count;
        }
        cuts << measure.tick + measure.duration;
        for (int i = 0; i + 1 < cuts.size(); ++i)
        {
            MeasureData fragment = measure;
            fragment.tick = cuts[i];
            fragment.duration = cuts[i + 1] - cuts[i];
            fragment.events.clear();
            if (i > 0)
                fragment.repeatStart = false;
            if (i + 2 < cuts.size())
            {
                fragment.repeatEnd = false;
                fragment.barline = 1;
            }
            for (const auto &event : measure.events)
                if (event.tick >= cuts[i] && event.tick < cuts[i + 1])
                    fragment.events << event;
            displayMeasures << fragment;
        }
    }
    score.measures = displayMeasures;
    QSet<int> hidden;
    for (const auto &v : settings.value("hiddenTracks").toArray())
        hidden.insert(v.toInt());
    QVector<VoiceData> voices;
    for (const auto &v : score.voices)
        if (!v.percussion && !hidden.contains(v.track))
            voices.push_back(v);
    QMap<int, int> order;
    auto orderValues = settings.value("voiceOrder").toArray();
    for (int i = 0; i < orderValues.size(); ++i)
        order[orderValues[i].toInt()] = i;
    std::stable_sort(voices.begin(), voices.end(),
                     [&](const auto &a, const auto &b)
                     {
                         return order.value(a.track, 100000 + a.track) <
                                order.value(b.track, 100000 + b.track);
                     });
    if (voices.isEmpty() && !score.voices.isEmpty())
    {
        out.pages.push_back(QRectF(24, 24, pageWidth, pageHeight));
        out.items.push_back({LayoutItem::Kind::Text,
                             {},
                             QStringLiteral("请在声部面板中显示有音高声部"),
                             QRectF(70, 130, 650, 35),
                             {},
                             20});
        out.size = QSizeF(pageWidth + 48, pageHeight + 48);
        return out;
    }
    QMap<int, qreal> rowHeights;
    QMap<int, int> maxChord;
    QMap<int, int> maxVerse;
    QMap<int, int> maxDots;
    QMap<int, qreal> noteSpacing;
    for (const auto &v : voices)
    {
        maxChord[v.track] = 1;
        maxVerse[v.track] = score.lyricRows.value(v.track);
        maxDots[v.track] = 0;
    }
    for (const auto &m : score.measures)
        for (const auto &e : m.events)
        {
            if (!maxChord.contains(e.track))
                continue;
            maxChord[e.track] = std::max(maxChord[e.track], int(e.notes.size()));
            for (const auto &n : e.notes)
                maxDots[e.track] = std::max(maxDots[e.track], std::abs(octaveDots(n, e.keyFifths)));
            for (const auto &l : e.lyrics)
                maxVerse[e.track] = std::max(maxVerse[e.track], l.verse + 1);
        }
    qreal systemHeight = style.preset == "classic" ? 24 : 36;
    for (const auto &v : voices)
    {
        noteSpacing[v.track] = 40 * scale + maxDots[v.track] * 8;
        rowHeights[v.track] = 10 * scale + maxChord[v.track] * noteSpacing[v.track] +
                              maxVerse[v.track] * lyricAdvance + style.voiceGap + style.lyricGap;
        bool hasHairpin = false, hasDynamic = false;
        for (const auto &mark : score.marks)
            if (mark.track == v.track)
            {
                hasHairpin |= mark.kind == "hairpin";
                hasDynamic |= mark.kind == "dynamic";
            }
        if (hasHairpin)
            rowHeights[v.track] += 54;
        else if (hasDynamic)
            rowHeights[v.track] += 26;
        if (voices.size() > 1 && style.showVoices)
        {
            rowHeights[v.track] += 48;
            qreal content = maxChord[v.track] * noteSpacing[v.track] +
                            maxVerse[v.track] * lyricAdvance + style.lyricGap;
            rowHeights[v.track] = std::max(rowHeights[v.track], content + (hasHairpin   ? 110
                                                                           : hasDynamic ? 96
                                                                                        : 60));
        }
        systemHeight += rowHeights[v.track];
    }
    if (systemHeight > pageHeight - (style.topMm + style.bottomMm) * mm - 110)
        out.warnings << QStringLiteral("当前声部组超出页面高度，请减小字号或调整纸张。");
    int page = -1;
    qreal y = 0;
    QSet<QString> headerMarks;
    QVector<HitRegion> headerHits;
    auto text =
        [&](QString id, QString value, QRectF rect, qreal size, int track = -1, bool bold = false)
    {
        out.items.push_back(
            {LayoutItem::Kind::Text, std::move(id), std::move(value), rect, {}, size, track, bold});
        auto &item = out.items.last();
        item.fontFamily = style.chineseFont;
        item.strokeWidth = style.lineWidth;
    };
    auto line = [&](QString id, QPointF a, QPointF b, int track = -1)
    {
        LayoutItem item;
        item.kind = LayoutItem::Kind::Line;
        item.id = std::move(id);
        item.track = track;
        item.strokeWidth = style.lineWidth;
        item.path.moveTo(a);
        item.path.lineTo(b);
        item.bounds = item.path.boundingRect();
        out.items.push_back(item);
    };
    auto dot = [&](QString id, QPointF p, int track)
    {
        out.items.push_back({LayoutItem::Kind::Dot,
                             std::move(id),
                             {},
                             QRectF(p.x() - 1.8, p.y() - 1.8, 3.6, 3.6),
                             {},
                             0,
                             track});
    };
    auto newPage = [&](int firstMeasure)
    {
        ++page;
        qreal top = 24 + page * (pageHeight + 24);
        out.pages.push_back(QRectF(24, top, pageWidth, pageHeight));
        qreal headerY = top + style.topMm * mm;
        if (page == 0)
        {
            text({}, score.title, QRectF(24 + Margin, headerY, contentWidth, style.titlePt * 1.8),
                 style.titlePt * 4 / 3, -1, style.titleBold);
            if (!style.titleFont.isEmpty())
                out.items.last().fontFamily = style.titleFont;
            out.items.last().alignment = (style.titleAlign == "left"    ? Qt::AlignLeft
                                          : style.titleAlign == "right" ? Qt::AlignRight
                                                                        : Qt::AlignHCenter) |
                                         Qt::AlignVCenter;
            headerY += style.titlePt * 1.8 + style.headerGap;
            for (const auto &subtitle : score.subtitles)
            {
                text({}, subtitle,
                     QRectF(24 + Margin, headerY, contentWidth, style.subtitlePt * 1.8),
                     style.subtitlePt * 4 / 3, -1, style.subtitleBold);
                if (!style.subtitleFont.isEmpty())
                    out.items.last().fontFamily = style.subtitleFont;
                headerY += style.subtitlePt * 1.8 + 3;
            }
            for (const auto &mark : score.marks)
                if (mark.kind == "tempo" && mark.tick == 0 && !hidden.contains(mark.track))
                {
                    QRectF box(24 + Margin, top + style.topMm * mm + 8, 150, 26);
                    text(mark.id, mark.text, box, 16, mark.track);
                    out.items.last().alignment = Qt::AlignLeft | Qt::AlignVCenter;
                    headerHits.push_back({mark.id, {}, box, mark.track, 0, mark.tick, false});
                    headerMarks.insert(mark.id);
                    break;
                }
        }
        else if (style.continuationTitle)
        {
            text({}, score.title, QRectF(24 + Margin, headerY, contentWidth, 24), 14);
            headerY += 30;
        }
        int fifths = score.keyFifths;
        bool minor = score.minor;
        qint64 keyTick = -1;
        if (!score.measures.isEmpty())
            for (const auto &mark : score.marks)
            {
                if ((mark.kind == "key" || mark.kind == "minor-key") && mark.track == 0 &&
                    mark.tick <= score.measures[firstMeasure].tick && mark.tick > keyTick)
                {
                    keyTick = mark.tick;
                    fifths = mark.text.toInt();
                    minor = mark.kind == "minor-key";
                }
            }
        text({}, keyLabel(fifths, minor), QRectF(24 + Margin, headerY, 95, 30), 16);
        out.items.last().alignment = Qt::AlignLeft | Qt::AlignVCenter;
        if (!score.measures.isEmpty())
        {
            qreal tx = 24 + Margin + 88;
            text({}, QString::number(score.measures[firstMeasure].numerator),
                 QRectF(tx, headerY - 1, 25, 16), 14);
            line({}, QPointF(tx + 3, headerY + 15), QPointF(tx + 22, headerY + 15));
            text({}, QString::number(score.measures[firstMeasure].denominator),
                 QRectF(tx, headerY + 15, 25, 16), 14);
        }
        qreal authorBottom = headerY + (30 - style.authorPt * 1.8) / 2;
        if (page == 0)
            for (const auto &author : score.authors)
            {
                text({}, author,
                     QRectF(24 + Margin + 150, authorBottom, contentWidth - 150,
                            style.authorPt * 1.8),
                     style.authorPt * 4 / 3, -1, style.authorBold);
                if (!style.authorFont.isEmpty())
                    out.items.last().fontFamily = style.authorFont;
                out.items.last().alignment = (style.authorAlign == "left"     ? Qt::AlignLeft
                                              : style.authorAlign == "center" ? Qt::AlignHCenter
                                                                              : Qt::AlignRight) |
                                             Qt::AlignVCenter;
                authorBottom += style.authorPt * 1.8 + 2;
            }
        if (style.showPages)
            text({}, QString::number(page + 1),
                 QRectF(24 + Margin, top + pageHeight - 35, contentWidth, 20), 12);
        y = std::max(headerY + 45, authorBottom + 20);
    };
    struct MeasureMetrics
    {
        QMap<MusicalTime, qreal> x;
        QMap<MusicalTime, QPair<qreal, qreal>> ink;
        qreal width = 0;
    };
    QVector<MeasureMetrics> metrics;
    QHash<QString, QString> noteLabels;
    QMap<int, QMap<qint64, int>> graceCounts, afterGraceCounts;
    for (const auto &measure : score.measures)
        for (const auto &event : measure.events)
            if (event.grace)
                if (event.graceAfter)
                    ++afterGraceCounts[event.track][event.tick];
                else
                    ++graceCounts[event.track][event.tick];
    for (const auto &m : score.measures)
    {
        QMap<int, QMap<int, int>> accidentalState;
        auto events = m.events;
        std::stable_sort(events.begin(), events.end(),
                         [](const auto &a, const auto &b)
                         {
                             return a.tick != b.tick ? a.tick < b.tick
                                                     : (a.grace ? (a.graceAfter ? 2 : 0) : 1) <
                                                           (b.grace ? (b.graceAfter ? 2 : 0) : 1);
                         });
        for (const auto &event : events)
            for (const auto &note : event.notes)
            {
                int degree = note.octave * 7 + note.step;
                int delta = note.alter - keyAlter(note.step, event.keyFifths);
                bool natural = delta == 0 && accidentalState[event.track].value(degree) != 0;
                noteLabels[note.id] = noteText(note, event.keyFifths, natural);
                accidentalState[event.track][degree] = delta;
            }
        MeasureMetrics mm;
        QMap<MusicalTime, qreal> widths;
        widths[MusicalTime(m.tick, 1920)] = 34;
        for (const auto &e : m.events)
        {
            if (!maxChord.contains(e.track))
                continue;
            qreal w = 36 * scale * style.noteGap;
            qreal inkLeft = 0, inkRight = 0;
            auto glyphExtent = [&](const QString &value, qreal px)
            {
                QFontMetricsF fm(style.font(px, style.noteBold, true));
                auto ink = fm.tightBoundingRect(value);
                qreal advance = fm.horizontalAdvance(value);
                inkLeft = std::max(inkLeft, advance / 2 - ink.left());
                inkRight = std::max(inkRight, ink.right() - advance / 2);
            };
            for (const auto &n : e.notes)
            {
                glyphExtent(e.rhythm ? QStringLiteral("X") : noteLabels.value(n.id), noteSize);
                w = std::max(w, QFontMetricsF(style.font(noteSize, style.noteBold, true))
                                        .horizontalAdvance(noteLabels.value(n.id)) +
                                    14 * scale);
            }
            for (const auto &l : e.lyrics)
            {
                w = std::max(w, textWidth(l.text, lyricSize) + 14);
            }
            if (e.rest)
            {
                glyphExtent("0", noteSize);
                if (e.denominator == 0 || e.denominator <= 2)
                    inkRight += (std::max(1, int(std::ceil(restTicks(e) / 480.0))) - 1) * 26;
            }
            if (e.rest && (e.denominator == 0 || e.denominator <= 2))
                w = std::max(w, std::ceil(restTicks(e) / 480.0) * 26 + 18);
            if (e.denominator <= 2 && !e.rest)
            {
                w = std::max(w, (e.denominator == 1 ? 80.0 : 50.0) * scale);
                inkRight = std::max(inkRight, e.denominator == 1 ? 62.0 : 32.0);
            }
            if (e.dots)
                inkRight = std::max(inkRight, 16 + (e.dots - 1) * 6 + 1.8);
            if (!e.grace)
            {
                w += afterGraceCounts[e.track].value(e.tick) * 46;
                inkLeft += graceCounts[e.track].value(e.tick) * 46;
                inkRight += afterGraceCounts[e.track].value(e.tick) * 46;
                auto &ink = mm.ink[timeOf(e)];
                ink.first = std::max(ink.first, inkLeft);
                ink.second = std::max(ink.second, inkRight);
            }
            widths[timeOf(e)] = std::max(widths.value(timeOf(e)), w);
        }
        qreal x = 16;
        MusicalTime lastTick(m.tick, 1920);
        qreal lastWidth = 0;
        for (auto it = widths.cbegin(); it != widths.cend(); ++it)
        {
            x += std::max(lastWidth,
                          30 * std::sqrt(std::max(0.0, (it.key() - lastTick).value() * 4)));
            int graceCount = 0;
            for (const auto &voice : voices)
                graceCount = std::max(graceCount, graceCounts[voice.track].value(it.key().ticks()));
            x += graceCount * 46;
            mm.x[it.key()] = x;
            lastTick = it.key();
            lastWidth = it.value();
        }
        mm.width = std::max(90.0, x + lastWidth + 12 * style.measureGap);
        metrics.push_back(mm);
    }
    for (const auto &metric : metrics)
        if (metric.width > contentWidth)
        {
            out.warnings << QStringLiteral("小节内容超出可用宽度，请增大纸张或减小字号。");
            break;
        }
    newPage(0);
    struct EventPosition
    {
        QPointF p;
        qreal systemLeft;
        qreal systemRight;
        int system;
        qreal rowTop;
    };
    QHash<QString, EventPosition> positions;
    QMap<int, QMap<qint64, qreal>> tickPositions;
    struct System
    {
        int first;
        int last;
        qreal top;
        QMap<int, qreal> rowTop;
        qreal left;
        qreal right;
    };
    QVector<System> systems;
    for (int begin = 0; begin < score.measures.size();)
    {
        if (y + systemHeight > out.pages.last().bottom() - style.bottomMm * mm)
            newPage(begin);
        int end = begin;
        qreal natural = 0;
        while (end < score.measures.size() &&
               (end == begin || natural + metrics[end].width <= contentWidth) &&
               end - begin < style.measuresPerLine)
        {
            bool forced = false;
            if (style.sourceBreaks && end > begin)
                for (const auto &event : score.measures[end].events)
                    if (event.sourceBreak)
                        forced = true;
            if (forced)
                break;
            natural += metrics[end++].width;
        }
        if (style.sourceBreaks && end - begin >= 3)
        {
            int segmentEnd = score.measures.size();
            for (int i = end; i < score.measures.size(); ++i)
            {
                bool forced = false;
                for (const auto &event : score.measures[i].events)
                    forced |= event.sourceBreak;
                if (forced)
                {
                    segmentEnd = i;
                    break;
                }
            }
            if (segmentEnd - end == 1)
            {
                --end;
                natural -= metrics[end].width;
            }
        }
        qreal factor = (end == score.measures.size() && !style.stretchLast)
                           ? std::min(1.0, contentWidth / natural)
                           : contentWidth / natural;
        // Regular systems share an even measure grid. Dense measures keep their
        // measured widths rather than squeezing lyrics or accidentals together.
        qreal columnWidth = contentWidth / std::max(1, end - begin);
        int referenceColumns =
            systems.isEmpty() ? style.measuresPerLine : systems.last().last - systems.last().first;
        if (end == score.measures.size() && !style.stretchLast && end - begin < referenceColumns)
            columnWidth = contentWidth / referenceColumns;
        bool equalColumns = true;
        for (int i = begin; i < end; ++i)
            equalColumns &= metrics[i].width <= columnWidth;
        System system{begin, end, y, {}, 24 + Margin, 24 + Margin + contentWidth};
        qreal rowY = y + 28;
        for (const auto &v : voices)
        {
            system.rowTop[v.track] = rowY;
            if (style.showVoices && voices.size() > 1)
                text({}, v.name, QRectF(system.right - 200, rowY - 48, 200, 18), 11, v.track);
            rowY += rowHeights[v.track];
        }
        qreal measureX = system.left;
        const int systemIndex = systems.size();
        for (int mi = begin; mi < end; ++mi)
        {
            QMap<int, QMap<qint64, int>> graceIndex, afterGraceIndex;
            const auto &m = score.measures[mi];
            qreal width = equalColumns ? columnWidth : metrics[mi].width * factor;
            qreal measureScale = width / metrics[mi].width;
            qreal inkLeft = width, inkRight = 0;
            for (auto it = metrics[mi].ink.begin(); it != metrics[mi].ink.end(); ++it)
            {
                qreal center = metrics[mi].x.value(it.key()) * measureScale;
                inkLeft = std::min(inkLeft, center - it->first);
                inkRight = std::max(inkRight, center + it->second);
            }
            // Center the musical glyphs, including duration dashes,
            // rather than scaling an asymmetric empty tail after the last note.
            qreal centerShift = metrics[mi].ink.isEmpty() ? 0 : (width - inkLeft - inkRight) / 2;
            if (style.showMeasures)
                text(m.id, QString::number(m.number), QRectF(measureX, y, 40, 20), 11);
            for (const auto &v : voices)
            {
                qreal row = system.rowTop[v.track];
                QFontMetricsF fm(style.font(noteSize, style.noteBold, true));
                qreal boxHeight = std::max(34.0, noteSize * 1.35);
                qreal digitCenter = (boxHeight - fm.height()) / 2 + fm.ascent() +
                                    fm.tightBoundingRect("0").center().y();
                qreal top = row + digitCenter - noteSize * 0.7;
                qreal bottom = row + (maxChord[v.track] - 1) * noteSpacing[v.track] + digitCenter +
                               noteSize * 0.7;
                qreal middle = (top + bottom) / 2;
                if (m.repeatStart)
                {
                    line(m.id, QPointF(measureX, top), QPointF(measureX, bottom), v.track);
                    out.items.last().strokeWidth = style.lineWidth * 2.5;
                }
                line(m.id, QPointF(measureX + width, top), QPointF(measureX + width, bottom),
                     v.track);
                if (m.repeatEnd)
                    out.items.last().strokeWidth = style.lineWidth * 2.5;
                if (m.barline == 32)
                {
                    out.items.last().strokeWidth = style.lineWidth * 2.5;
                    line(m.id, QPointF(measureX + width - 5, top),
                         QPointF(measureX + width - 5, bottom), v.track);
                }
                if (m.repeatStart)
                {
                    line(m.id, QPointF(measureX + 4, top), QPointF(measureX + 4, bottom), v.track);
                    dot(m.id, QPointF(measureX + 10, middle - 5), v.track);
                    dot(m.id, QPointF(measureX + 10, middle + 5), v.track);
                }
                if (m.repeatEnd)
                {
                    line(m.id, QPointF(measureX + width - 4, top),
                         QPointF(measureX + width - 4, bottom), v.track);
                    dot(m.id, QPointF(measureX + width - 10, middle - 5), v.track);
                    dot(m.id, QPointF(measureX + width - 10, middle + 5), v.track);
                }
            }
            for (const auto &e : m.events)
            {
                if (!system.rowTop.contains(e.track))
                    continue;
                qreal row = system.rowTop[e.track];
                qreal x =
                    measureX + metrics[mi].x.value(timeOf(e), 24) * measureScale + centerShift;
                if (e.grace && e.graceAfter)
                    x += 46 * (++afterGraceIndex[e.track][e.tick]);
                else if (e.grace)
                    x -= 46 * (graceCounts[e.track].value(e.tick) - graceIndex[e.track][e.tick]++);
                qreal baseline = row + (maxChord[e.track] - 1) * noteSpacing[e.track];
                if (e.grace)
                    baseline -= 12;
                EventPosition pos{QPointF(x, baseline), system.left, system.right, systemIndex,
                                  row};
                positions.insert(e.id, pos);
                if (!e.grace)
                    tickPositions[e.track][e.tick] = x;
                auto notes = e.notes;
                std::sort(notes.begin(), notes.end(),
                          [](const auto &a, const auto &b) { return a.midi < b.midi; });
                if (e.rest && e.visible)
                {
                    int count = (e.denominator == 0 || e.denominator <= 2)
                                    ? std::max(1, int(std::ceil(restTicks(e) / 480.0)))
                                    : 1;
                    for (int r = 0; r < count; ++r)
                    {
                        text(e.id, "0",
                             QRectF(x - 15 + r * 26, baseline, 30, std::max(34.0, noteSize * 1.35)),
                             noteSize, e.track, style.noteBold);
                        out.items.last().fontFamily = style.numberFont;
                        qreal remaining = restTicks(e) - r * 480;
                        if (e.denominator == 0 && remaining < 480)
                        {
                            for (int denominator = 8; denominator <= 128; denominator *= 2)
                                for (int dots = 0; dots <= 2; ++dots)
                                {
                                    if (std::abs(1920.0 / denominator * (2 - std::pow(0.5, dots)) -
                                                 remaining) > 0.01)
                                        continue;
                                    int beams = int(std::log2(denominator)) - 2;
                                    for (int b = 0; b < beams; ++b)
                                        line(e.id, QPointF(x - 10 + r * 26, baseline + 36 + b * 4),
                                             QPointF(x + 10 + r * 26, baseline + 36 + b * 4),
                                             e.track);
                                    for (int d = 0; d < dots; ++d)
                                        dot(e.id, QPointF(x + 16 + r * 26 + d * 6, baseline + 18),
                                            e.track);
                                }
                        }
                    }
                    QRectF box(x - 15, baseline, 30 + (count - 1) * 26, 34);
                    out.hits.push_back(
                        {e.id, e.id, box.adjusted(-4, -7, 4, 9), e.track, mi, e.tick, false});
                }
                if (e.rest && !e.visible)
                    out.hits.push_back({e.id, e.id, QRectF(x - 15, baseline - 7, 30, 50), e.track,
                                        mi, e.tick, false});
                int ni = 0;
                if (e.visible)
                    for (const auto &n : notes)
                    {
                        qreal ny = baseline - ni++ * noteSpacing[e.track];
                        qreal fontSize = e.grace ? noteSize * 0.65 : noteSize;
                        qreal digitWidth =
                            QFontMetricsF(style.font(fontSize, style.noteBold, true))
                                .horizontalAdvance(e.rhythm ? QStringLiteral("X")
                                                            : noteLabels.value(n.id)) +
                            6;
                        QRectF box(x - digitWidth / 2, ny, digitWidth,
                                   std::max(34.0, fontSize * 1.35));
                        text(n.id, e.rhythm ? QStringLiteral("X") : noteLabels.value(n.id), box,
                             e.grace ? noteSize * 0.65 : noteSize, e.track, style.noteBold);
                        out.items.last().fontFamily = style.numberFont;
                        int dots = e.rhythm ? 0 : octaveDots(n, e.keyFifths);
                        int beamLines = e.denominator >= 8 ? int(std::log2(e.denominator)) - 2 : 0;
                        for (int d = 0; d < std::abs(dots); ++d)
                            dot(n.id,
                                QPointF(x, dots > 0 ? ny - 5 - d * 5
                                                    : ny + 40 + beamLines * 4 + d * 5),
                                e.track);
                        out.hits.push_back(
                            {n.id, e.id, box.adjusted(-2, -7, 2, 7), e.track, mi, e.tick, true});
                        auto np = pos;
                        np.p.setY(ny);
                        positions.insert(n.id, np);
                    }

                if (e.visible && (!e.rest || e.denominator >= 4))
                    for (int d = 0; d < e.dots; ++d)
                        dot(e.id, QPointF(x + 16 + d * 6, baseline + 18), e.track);
                if (e.visible && !e.rest && (e.denominator == 1 || e.denominator == 2))
                {
                    int count = e.denominator == 1 ? 3 : 1;
                    for (int k = 0; k < count; ++k)
                        line(e.id, QPointF(x + 22 + k * 15, baseline + 18),
                             QPointF(x + 32 + k * 15, baseline + 18), e.track);
                }
                for (const auto &lyric : e.lyrics)
                {
                    qreal ly = row + maxChord[e.track] * noteSpacing[e.track] + style.lyricGap +
                               lyric.verse * lyricAdvance;
                    QString body = lyric.text;
                    while (!body.isEmpty() && body.back().isPunct())
                        body.chop(1);
                    // Punctuation hangs to the right; it must not move the sung
                    // character away from the note's horizontal anchor.
                    qreal bodyWidth = lyricMetrics.horizontalAdvance(body);
                    auto value = lyric.text + (lyric.extend ? "＿" : "");
                    qreal w = std::max(bodyWidth, lyricMetrics.horizontalAdvance(value)) + 6;
                    text(e.id, lyric.text + (lyric.extend ? "＿" : ""),
                         QRectF(x - bodyWidth / 2, ly, w, lyricHeight), lyricSize, e.track,
                         style.lyricBold);
                    out.items.last().alignment = Qt::AlignLeft | Qt::AlignVCenter;
                    if (!style.lyricFont.isEmpty())
                        out.items.last().fontFamily = style.lyricFont;
                }
            }
            measureX += width;
        }
        out.systems.push_back(
            {MusicalTime(score.measures[begin].tick, 1920),
             MusicalTime(score.measures[end - 1].tick + score.measures[end - 1].duration, 1920),
             QRectF(system.left, y, measureX - system.left, systemHeight)});
        systems.push_back(system);
        y += systemHeight + style.systemGap;
        begin = end;
    }
    for (const auto &measure : score.measures)
        for (const auto &voice : voices)
        {
            QVector<const EventData *> group;
            auto paintGroup = [&]()
            {
                if (group.isEmpty())
                    return;
                for (int i = 0; i < group.size(); ++i)
                {
                    const auto &event = *group[i];
                    const auto p = positions.value(event.id).p;
                    int levels = std::max(0, int(std::log2(std::max(4, event.denominator))) - 2);
                    for (int level = 0; level < levels; ++level)
                    {
                        bool left = i > 0 && group[i - 1]->denominator >= (8 << level);
                        bool right =
                            i + 1 < group.size() && group[i + 1]->denominator >= (8 << level);
                        if (right)
                            line(event.id, QPointF(p.x(), p.y() + 36 + level * 4),
                                 QPointF(positions.value(group[i + 1]->id).p.x(),
                                         p.y() + 36 + level * 4),
                                 event.track);
                        else if (!left)
                        {
                            double a = -10, b = 10;
                            if (group.size() > 1 && level > 0)
                            {
                                if (i == group.size() - 1)
                                    b = 0;
                                else
                                    a = 0;
                            }
                            line(event.id, QPointF(p.x() + a, p.y() + 36 + level * 4),
                                 QPointF(p.x() + b, p.y() + 36 + level * 4), event.track);
                        }
                    }
                }
                group.clear();
            };
            qint64 beat =
                (measure.denominator == 8 && measure.numerator >= 6 && measure.numerator % 3 == 0)
                    ? 720
                    : 1920 / measure.denominator;
            QString previousBeam;
            qint64 previousBeat = -1;
            bool previousGrace = false, previousAfter = false;
            MusicalTime lastEnd;
            for (const auto &event : measure.events)
            {
                if (event.track != voice.track)
                    continue;
                qint64 bucket = (event.tick - measure.tick) / std::max<qint64>(1, beat);
                bool joins = !group.isEmpty() && event.grace == previousGrace &&
                             event.graceAfter == previousAfter &&
                             (event.grace ||
                              ((event.position == lastEnd || event.tick == lastEnd.ticks()) &&
                               ((!event.beamGroup.isEmpty() && event.beamGroup == previousBeam) ||
                                (event.beamGroup.isEmpty() && previousBeam.isEmpty() &&
                                 bucket == previousBeat))));
                if (!joins || event.rest || !event.visible || event.denominator < 8)
                    paintGroup();
                if (event.visible && !event.rest && event.denominator >= 8 &&
                    positions.contains(event.id))
                    group << &event;
                previousBeam = event.beamGroup;
                previousBeat = bucket;
                previousGrace = event.grace;
                previousAfter = event.graceAfter;
                lastEnd = event.length == MusicalTime()
                              ? MusicalTime(event.tick + event.duration, 1920)
                              : event.position + event.length;
            }
            paintGroup();
        }
    out.hits += headerHits;
    QVector<QRectF> markLabels;
    for (const auto &mark : score.marks)
    {
        if (headerMarks.contains(mark.id))
            continue;
        if (hidden.contains(mark.track))
            continue;
        int firstSystem = -1, lastSystem = -1;
        for (int si = 0; si < systems.size(); ++si)
        {
            const auto &sys = systems[si];
            auto start = score.measures[sys.first].tick;
            const auto &lastMeasure = score.measures[sys.last - 1];
            auto end = lastMeasure.tick + lastMeasure.duration;
            if (mark.tick >= start && mark.tick < end)
                firstSystem = si;
            if (((mark.kind == "tuplet" || mark.kind == "volta" || mark.kind == "hairpin")
                     ? mark.endTick > start
                     : mark.endTick >= start) &&
                mark.endTick <= end)
                lastSystem = si;
        }
        if (firstSystem < 0)
            continue;
        auto pointAt = [&](const System &sys, qint64 tick)
        {
            const auto &finalMeasure = score.measures[sys.last - 1];
            if (tick >= score.measures[sys.first].tick &&
                tick < finalMeasure.tick + finalMeasure.duration &&
                tickPositions[mark.track].contains(tick))
                return tickPositions[mark.track][tick];
            qreal x = sys.left;
            qreal total = 0;
            for (int i = sys.first; i < sys.last; ++i)
                total += metrics[i].width;
            for (int i = sys.first; i < sys.last; ++i)
            {
                const auto &m = score.measures[i];
                qreal w = metrics[i].width * contentWidth / total;
                if (tick < m.tick + m.duration)
                    return x +
                           std::clamp((tick - m.tick) / qreal(std::max(qint64(1), m.duration)), 0.0,
                                      1.0) *
                               (w - 30) +
                           20;
                x += w;
            }
            return sys.right - 10;
        };
        if (mark.endTick > mark.tick &&
            (mark.kind == "slur" || mark.kind == "tie" || mark.kind == "hairpin" ||
             mark.kind == "volta" || mark.kind == "tuplet"))
        {
            lastSystem = std::max(firstSystem, lastSystem);
            for (int si = firstSystem; si <= lastSystem; ++si)
            {
                const auto &sys = systems[si];
                if (!sys.rowTop.contains(mark.track))
                    continue;
                qreal row = sys.rowTop[mark.track];
                qreal a = si == firstSystem ? pointAt(sys, mark.tick) : sys.left + 5;
                qreal b = si == lastSystem ? pointAt(sys, mark.endTick) : sys.right - 5;
                if (si == firstSystem && positions.contains(mark.startId) &&
                    positions[mark.startId].system == si)
                    a = positions[mark.startId].p.x();
                if (si == lastSystem && positions.contains(mark.endId) &&
                    positions[mark.endId].system == si)
                    b = positions[mark.endId].p.x();
                qreal cy = row - (mark.kind == "volta" ? 20 : 5);
                if (mark.kind == "tie" && positions.contains(mark.startId) &&
                    positions[mark.startId].system == si)
                    cy = positions[mark.startId].p.y() - 5;
                if (mark.kind == "tuplet")
                    cy = row - 34;
                QPainterPath path;
                if (mark.kind == "hairpin")
                {
                    cy = row + maxChord[mark.track] * noteSpacing[mark.track] + style.lyricGap +
                         maxVerse[mark.track] * lyricAdvance + 46;
                    bool close = mark.text == ">";
                    path.moveTo(a, cy + (close ? -5 : 0));
                    path.lineTo(b, cy + (close ? 0 : -5));
                    path.moveTo(a, cy + (close ? 5 : 0));
                    path.lineTo(b, cy + (close ? 0 : 5));
                }
                else if (mark.kind == "volta" || mark.kind == "tuplet")
                {
                    path.moveTo(a, cy + 9);
                    path.lineTo(a, cy);
                    path.lineTo(b, cy);
                    path.lineTo(b, cy + 9);
                    text(mark.id, mark.text,
                         QRectF(mark.kind == "tuplet" ? (a + b) / 2 - 15 : a + 4, cy + 2,
                                mark.kind == "tuplet" ? 30 : 65, 16),
                         11, mark.track);
                }
                else
                {
                    path.moveTo(a, cy);
                    path.cubicTo(a + (b - a) * 0.25, cy - 18, a + (b - a) * 0.75, cy - 18, b, cy);
                }
                out.items.push_back({LayoutItem::Kind::Curve,
                                     mark.id,
                                     {},
                                     path.boundingRect(),
                                     path,
                                     0,
                                     mark.track});
                out.hits.push_back({mark.id,
                                    {},
                                    path.boundingRect().adjusted(-4, -5, 4, 5),
                                    mark.track,
                                    sys.first,
                                    mark.tick,
                                    false});
            }
        }
        else
        {
            const auto &sys = systems[firstSystem];
            if (!sys.rowTop.contains(mark.track))
                continue;
            qreal x = pointAt(sys, mark.tick);
            qreal my = sys.rowTop[mark.track] - 35;
            if (mark.kind == "dynamic")
                my = sys.rowTop[mark.track] + maxChord[mark.track] * noteSpacing[mark.track] +
                     style.lyricGap + maxVerse[mark.track] * lyricAdvance + 16;
            if (mark.kind == "articulation" || mark.kind == "fermata")
                my = sys.rowTop[mark.track] - 22;
            QString label = (mark.kind == "key" || mark.kind == "minor-key")
                                ? keyLabel(mark.text.toInt(), mark.kind == "minor-key")
                                : mark.text;
            QRectF box(x - 20, my, std::max(50.0, textWidth(label, 13) + 6), 22);
            for (int pass = 0; pass < score.marks.size(); ++pass)
            {
                bool collision = false;
                for (const auto &occupied : markLabels)
                    if (box.intersects(occupied))
                    {
                        box.moveLeft(occupied.right() + 8);
                        collision = true;
                        break;
                    }
                if (!collision)
                    break;
            }
            markLabels << box;
            text(mark.id, label, box, 13, mark.track);
            out.hits.push_back({mark.id, {}, box, mark.track, sys.first, mark.tick, false});
        }
    }
    for (auto &item : out.items)
        if (item.kind == LayoutItem::Kind::Curve)
            item.strokeWidth = style.lineWidth;
    out.size = QSizeF(pageWidth + 48, out.pages.last().bottom() + 24);
    return out;
}
} // namespace jianpu
