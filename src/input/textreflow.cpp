// SPDX-License-Identifier: GPL-3.0-only
#include "textformat.h"
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <set>

namespace jianpu
{
QString FanqieFormat::replaceMusic(const QString &source, const QString &projection)
{
    if (source.isEmpty())
        return projection;
    static const QRegularExpression musicalField("^\\s*[QC]\\d*\\s*[:：]");
    QStringList music, result;
    for (const auto &line : projection.split('\n'))
        if (musicalField.match(line).hasMatch())
            music << line;
    auto lines = source.split('\n');
    int last = -1, next = 0;
    for (int i = 0; i < lines.size(); ++i)
        if (musicalField.match(lines[i]).hasMatch())
            last = i;
    if (last < 0)
        return source + (source.endsWith('\n') ? "" : "\n") + music.join('\n') + "\n";
    for (int i = 0; i < lines.size(); ++i)
    {
        if (musicalField.match(lines[i]).hasMatch())
        {
            if (next < music.size())
                result << music[next++];
            if (i == last)
                while (next < music.size())
                    result << music[next++];
        }
        else
            result << lines[i];
    }
    return result.join('\n');
}
namespace
{
QString wordToken(const QString &word)
{
    return word.isEmpty() ? "@" : FanqieFormat::lyricTokens(word).join("~");
}
QString decorations(const TextNote &n, bool first, bool last)
{
    QString out;
    for (bool after : {false, true})
    {
        if ((!after && !first) || (after && !last))
            continue;
        QString grace;
        for (const auto &g : n.graces)
            if (g.graceAfter == after)
                grace += FanqieFormat::token(g);
        if (!grace.isEmpty())
            out += "[" + QString(after ? "h" : "") + grace + "]";
    }
    if (first)
    {
        for (const auto &dynamic : n.dynamics)
            out += "&" + dynamic;
        if (!n.annotation.isEmpty())
        {
            auto escaped = n.annotation;
            escaped.replace("\\", "\\\\").replace("\"", "\\\"");
            out += "\"" + escaped + "\"";
        }
    }
    return out;
}
// Choose an exact written duration, splitting unusual sums without rounding.
QVector<TextNote> splitNote(const TextNote &note, const QVector<MusicalTime> &cuts, int fifths)
{
    QVector<TextNote> result;
    MusicalTime pos = note.position, end = pos + note.duration;
    auto scale = note.tuplet ? MusicalTime(note.tuplet, note.normal) : MusicalTime(1);
    while (pos < end)
    {
        auto next = end;
        for (const auto &cut : cuts)
            if (cut > pos && cut < next)
                next = cut;
        while (pos < next)
        {
            auto written = (next - pos) * scale;
            TextNote piece = note;
            piece.extensions = piece.slashes = piece.dots = 0;
            bool exact = false;
            for (int denominator = 1; denominator <= 1024 && !exact; denominator *= 2)
                for (int dots = 0; dots <= 3; ++dots)
                    if (written ==
                        MusicalTime((1LL << (dots + 1)) - 1, denominator * (1LL << dots)))
                    {
                        piece.slashes = std::max(0, int(std::log2(denominator)) - 2);
                        piece.extensions = denominator < 4 ? 4 / denominator - 1 : 0;
                        piece.dots = dots;
                        exact = true;
                        break;
                    }
            if (!exact)
            {
                int denominator = 1;
                while (denominator <= 1024 && MusicalTime(1, denominator) > written)
                    denominator *= 2;
                if (denominator > 1024)
                    return {}; // Keep the original draft if it cannot be represented exactly.
                written = MusicalTime(1, denominator);
                piece.slashes = std::max(0, int(std::log2(denominator)) - 2);
                piece.extensions = denominator < 4 ? 4 / denominator - 1 : 0;
            }
            piece.position = pos;
            piece.written = written;
            piece.duration = written / scale;
            piece.tieBack =
                note.degree != 0 && note.degree != 8 && (note.tieBack || !result.isEmpty());
            piece.tieForward = note.degree != 0 && note.degree != 8 &&
                               (note.tieForward || pos + piece.duration < end);
            if (!result.isEmpty())
            {
                piece.lyrics.clear();
                // A continuation spells the same pitch, even across an accidental reset.
                static const int sharps[]{3, 0, 4, 1, 5, 2, 6}, flats[]{6, 2, 5, 1, 4, 0, 3};
                int keyAlter = 0;
                for (int i = 0; i < std::abs(fifths); ++i)
                    if ((fifths > 0 ? sharps[i] : flats[i]) == piece.step)
                        keyAlter = fifths > 0 ? 1 : -1;
                if (piece.accidental == 99 && piece.alter != keyAlter)
                    piece.accidental = piece.alter == 0 ? 0 : piece.alter - keyAlter;
            }
            result << piece;
            pos += piece.duration;
        }
    }
    return result;
}
} // namespace

QString FanqieFormat::alignedSource(const TextParseResult &p,
                                    const QVector<MusicalTime> &systemStarts) const
{
    // Incomplete structures and surplus lyrics must remain verbatim and editable.
    if (!p.canProject() || p.notes.isEmpty())
        return p.source;
    struct Row
    {
        QString music;
        QVector<TextNote> notes;
        int verses = 0;
        QMap<int, QStringList> trailing;
    };
    using Key = QPair<int, int>; // system, voice: same reading order as the page
    QMap<Key, Row> rows;
    auto systemAt = [&](MusicalTime time)
    {
        int system = 0;
        for (int i = 0; i < systemStarts.size(); ++i)
            if (systemStarts[i] <= time)
                system = i;
        return system;
    };
    QVector<MusicalTime> cuts = systemStarts;
    std::set<MusicalTime> measureEnds;
    for (const auto &m : p.measures)
    {
        cuts << m.start << m.start + m.duration;
        measureEnds.insert(m.start + m.duration);
    }
    QMap<int, std::set<MusicalTime>> explicitEnds;
    for (const auto &b : p.boundaries)
        if (!b.automatic)
            explicitEnds[b.voice].insert(b.position);
    QStringList headers;
    bool changed = p.rewriteLyrics || std::any_of(p.boundaries.begin(), p.boundaries.end(),
                                                  [](const auto &b) { return b.automatic; });
    auto append = [](QString &music, const QString &part)
    {
        auto value = part.trimmed();
        if (value.isEmpty())
            return;
        if (!music.isEmpty() && !music.endsWith(' '))
            music += ' ';
        music += value;
    };
    auto raw = [&](int start, int length)
    {
        auto text = p.source.mid(start, length);
        for (auto it = p.boundaries.crbegin(); it != p.boundaries.crend(); ++it)
            if (it->automatic && it->offset >= start && it->offset < start + length)
                text.remove(it->offset - start, 1);
        return text;
    };
    int offset = 0, voice = 1, segment = 0;
    QMap<int, int> lastSegment, verses;
    QMap<int, QVector<Key>> segmentRows;
    QMap<Key, int> sourceRows;
    QMap<int, bool> numberedMusic, numberedLyrics;
    for (const auto &line : p.source.split('\n'))
    {
        auto field = QRegularExpression("^\\s*([QC])(\\d*)\\s*[:：]\\s*(.*)$").match(line);
        if (!field.hasMatch())
        {
            headers << line;
            offset += line.size() + 1;
            continue;
        }
        int selected = field.captured(2).isEmpty() ? voice : field.captured(2).toInt();
        if (field.captured(1) == "C")
        {
            numberedLyrics[selected] |= !field.captured(2).isEmpty();
            int seg = lastSegment.value(selected);
            int verse = verses[seg]++;
            QStringList expected;
            for (const auto &n : p.notes)
                if (n.segment == seg && n.voice == selected && n.degree != 0 && n.degree != 8 &&
                    !n.tieBack)
                    expected << wordToken(n.lyrics.value(verse));
            // Persist skips after insertions: the text must carry the same binding as the score.
            auto original = lyricTokens(field.captured(3));
            auto rebuilt = lyricTokens(expected.join(" "));
            while (!original.isEmpty() && original.last() == "@")
                original.removeLast();
            while (!rebuilt.isEmpty() && rebuilt.last() == "@")
                rebuilt.removeLast();
            changed |= original != rebuilt;
            for (const auto &key : segmentRows.value(seg))
                rows[key].verses = std::max(rows[key].verses, verse + 1);
            for (const auto &overflow : p.overflow)
                if (overflow.voice == selected && overflow.segment == seg &&
                    overflow.verse == verse && !segmentRows.value(seg).isEmpty())
                    rows[segmentRows[seg].last()].trailing[verse] += overflow.words;
            offset += line.size() + 1;
            continue;
        }
        voice = selected;
        numberedMusic[voice] |= !field.captured(2).isEmpty();
        ++segment;
        lastSegment[voice] = segment;
        int previousEnd = offset + field.capturedStart(3);
        Key previous{-1, voice};
        bool any = false;
        for (const auto &n : p.notes)
        {
            if (n.segment != segment || n.voice != voice)
                continue;
            any = true;
            QString leading = raw(previousEnd, n.offset - previousEnd);
            auto pieces = splitNote(n, cuts, p.fifths);
            if (pieces.isEmpty())
                return p.source;
            bool split = pieces.size() > 1;
            // The written count may grow when a tuplet note crosses a bar.
            // Preserve the original ratio rather than inferring it a second time.
            if (n.tuplet)
                leading.replace(QRegularExpression("\\(y(?![0-9]+:[0-9]+)"),
                                QString("(y%1:%2 ").arg(n.tuplet).arg(n.normal));
            changed |= split;
            for (int pi = 0; pi < pieces.size(); ++pi)
            {
                const auto &piece = pieces[pi];
                Key key{systemAt(piece.position), voice};
                if (previous.first >= 0 && previous != key)
                {
                    int opening = leading.indexOf(QRegularExpression("[(<>]"));
                    append(rows[previous].music, opening >= 0 ? leading.left(opening) : leading);
                    leading = opening >= 0 ? leading.mid(opening) : QString();
                    changed = true;
                }
                if (!segmentRows[segment].contains(key))
                {
                    segmentRows[segment] << key;
                    if (++sourceRows[key] > 1)
                        changed = true;
                }
                auto &row = rows[key];
                row.verses = std::max(row.verses, p.verseRows.value(voice));
                append(row.music, leading);
                append(row.music,
                       split ? token(piece) + decorations(n, pi == 0, pi + 1 == pieces.size())
                             : n.raw);
                leading.clear();
                row.notes << piece;
                auto end = piece.position + piece.duration;
                if (measureEnds.contains(end) && !explicitEnds.value(voice).contains(end))
                {
                    row.music += " | ";
                    changed = true;
                }
                previous = key;
            }
            previousEnd = n.offset + n.length;
        }
        if (any)
            append(rows[previous].music, raw(previousEnd, offset + line.size() - previousEnd));
        else
        {
            bool voiceHasNotes = std::any_of(p.notes.begin(), p.notes.end(),
                                             [&](const auto &n) { return n.voice == voice; });
            if (voiceHasNotes)
            {
                changed = true;
                offset += line.size() + 1;
                continue;
            }
            Key key{0, voice};
            rows[key].music += field.captured(3);
            segmentRows[segment] << key;
        }
        offset += line.size() + 1;
    }
    if (!changed)
        return p.source;
    while (!headers.isEmpty() && headers.last().trimmed().isEmpty())
        headers.removeLast();
    QStringList output = headers;
    for (auto it = rows.begin(); it != rows.end(); ++it)
    {
        auto suffix = p.voiceNames.size() > 1 || numberedMusic.value(it.key().second)
                          ? QString::number(it.key().second)
                          : QString();
        auto lyricSuffix = p.voiceNames.size() > 1 || numberedLyrics.value(it.key().second)
                               ? QString::number(it.key().second)
                               : QString();
        output << "Q" + suffix + ": " + it->music.trimmed();
        for (int verse = 0; verse < it->verses; ++verse)
        {
            QStringList words;
            int keep = 0;
            for (const auto &n : it->notes)
                if (n.degree != 0 && n.degree != 8 && !n.tieBack)
                {
                    words << wordToken(n.lyrics.value(verse));
                    if (!n.lyrics.value(verse).isEmpty() || n.lyricSkips.contains(verse))
                        keep = words.size();
                }
            words = words.mid(0, keep);
            for (const auto &word : it->trailing.value(verse))
                words << wordToken(word);
            output << "C" + lyricSuffix + ": " + words.join(" ");
        }
    }
    return output.join('\n') + (p.source.endsWith('\n') ? "\n" : "");
}
} // namespace jianpu
