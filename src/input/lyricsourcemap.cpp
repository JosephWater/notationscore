// SPDX-License-Identifier: GPL-3.0-only
#include "lyricsourcemap.h"
#include <QRegularExpression>
#include <QTextBoundaryFinder>
namespace jianpu
{
QVector<LyricSourceRange> LyricSourceMap::ranges(const TextParseResult &parsed)
{
    QVector<LyricSourceRange> result;
    QMap<int, int> segments, verses;
    int current = 1, segment = 0, offset = 0;
    for (const auto &line : parsed.source.split('\n'))
    {
        auto field = QRegularExpression("^\\s*([QC])(\\d*)\\s*[:：]\\s*(.*)$").match(line);
        if (!field.hasMatch())
        {
            offset += line.size() + 1;
            continue;
        }
        int voice = field.captured(2).isEmpty() ? current : field.captured(2).toInt();
        if (field.captured(1) == "Q")
        {
            current = voice;
            segments[voice] = ++segment;
            verses[voice] = 0;
            offset += line.size() + 1;
            continue;
        }
        int verse = verses[voice]++;
        QVector<const TextNote *> notes;
        for (const auto &n : parsed.notes)
            if (n.voice == voice && n.segment == segments[voice] && n.degree != 0 &&
                n.degree != 8 && !n.tieBack)
                notes << &n;
        QVector<QPair<int, int>> words;
        const auto value = field.captured(3);
        bool join = false;
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, value);
        int begin = 0, end = 0;
        while ((end = finder.toNextBoundary()) >= 0)
        {
            auto ch = value.mid(begin, end - begin);
            if (ch == "~")
            {
                join = true;
                if (!words.isEmpty())
                    words.last().second = end - words.last().first;
            }
            else if (!ch.trimmed().isEmpty())
            {
                bool punctuation =
                    ch != "@" && (ch[0].isPunct() || ch[0].category() == QChar::Symbol_Other);
                if (!words.isEmpty() && (join || punctuation))
                    words.last().second = end - words.last().first;
                else if (!punctuation)
                    words << qMakePair(begin, end - begin);
                join = false;
            }
            begin = end;
        }
        for (int i = 0; i < std::min(notes.size(), words.size()); ++i)
            result << LyricSourceRange{voice, verse,
                                       offset + int(field.capturedStart(3)) + words[i].first,
                                       words[i].second, notes[i]->position};
        if (words.size() < notes.size())
            result << LyricSourceRange{voice, verse, offset + int(line.size()), 0,
                                       notes[words.size()]->position};
        offset += line.size() + 1;
    }
    return result;
}
} // namespace jianpu
