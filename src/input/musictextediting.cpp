// SPDX-License-Identifier: GPL-3.0-only
#include "musictextediting.h"
#include <QRegularExpression>
#include <algorithm>
#include <functional>
namespace jianpu
{
int MusicTextEditing::appendPosition(const QString &source, int voice)
{
    int current = 1, offset = 0, result = -1;
    for (const auto &line : source.split('\n'))
    {
        auto field = QRegularExpression("^\\s*Q(\\d*)\\s*[:：]").match(line);
        if (field.hasMatch())
        {
            if (!field.captured(1).isEmpty())
                current = field.captured(1).toInt();
            if (current == voice)
                result = offset + line.size();
        }
        offset += line.size() + 1;
    }
    return result;
}
MusicTextEdit MusicTextEditing::insert(const QString &source, int position, const QString &text,
                                       const QVector<int> &discardedSeparators)
{
    MusicTextEdit edit;
    edit.source = source;
    position = std::clamp(position, 0, int(source.size()));
    edit.changes.before = source;
    auto separators = discardedSeparators;
    std::sort(separators.begin(), separators.end(), std::greater<int>());
    for (int offset : separators)
        if (offset >= position && offset < edit.source.size() && edit.source[offset] == '|')
        {
            edit.source.remove(offset, 1);
            edit.changes.edits << SourceChange{offset, 1, 0};
        }
    edit.source.insert(position, text);
    edit.changes.edits << SourceChange{position, 0, int(text.size())};
    edit.cursor = position + text.size();
    edit.changed = !text.isEmpty();
    return edit;
}
MusicTextEdit MusicTextEditing::eraseBefore(const TextParseResult &parsed, int voice, int position)
{
    MusicTextEdit edit;
    edit.source = parsed.source;
    edit.changes.before = parsed.source;
    int target = -1;
    for (int i = 0; i < parsed.notes.size(); ++i)
        if (parsed.notes[i].voice == voice && parsed.notes[i].offset < position)
            target = i;
    if (target < 0)
        return edit;
    // An automatically split sustained note is one input unit, even over Q rows.
    int first = target;
    while (parsed.notes[first].tieBack)
    {
        int before = first - 1;
        while (before >= 0 && parsed.notes[before].voice != voice)
            --before;
        if (before < 0)
            break;
        first = before;
    }
    QVector<QPair<int, int>> removals;
    for (int i = first; i <= target; ++i)
        if (parsed.notes[i].voice == voice)
            removals << qMakePair(parsed.notes[i].offset, parsed.notes[i].length);
    // Automatic separators cannot trap Backspace. Keep authored repeats and endings.
    for (const auto &bar : parsed.boundaries)
        if (bar.automatic && bar.voice == voice && bar.offset >= parsed.notes[first].offset &&
            bar.offset < position)
        {
            int length = parsed.source.mid(bar.offset).startsWith(":|:") ? 3
                         : parsed.source.mid(bar.offset).startsWith("||") ||
                                 parsed.source.mid(bar.offset).startsWith("|:") ||
                                 parsed.source.mid(bar.offset).startsWith(":|")
                             ? 2
                             : 1;
            removals << qMakePair(bar.offset, length);
        }
    std::sort(removals.begin(), removals.end(), [](auto a, auto b) { return a.first > b.first; });
    for (auto [offset, length] : removals)
    {
        edit.source.remove(offset, length);
        edit.changes.edits << SourceChange{offset, length, 0};
    }
    edit.cursor = parsed.notes[first].offset;
    edit.onset = parsed.notes[first].position;
    edit.changed = true;
    return edit;
}
} // namespace jianpu
