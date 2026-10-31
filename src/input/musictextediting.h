// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "textformat.h"
namespace jianpu
{
struct MusicTextEdit
{
    QString source;
    SourceChanges changes;
    int cursor = 0;
    MusicalTime onset;
    bool changed = false;
};
// Format-specific source surgery. The input controller deals in musical targets,
// never in Q/C prefixes or automatic barline characters.
class MusicTextEditing
{
  public:
    static int appendPosition(const QString &, int voice);
    static MusicTextEdit insert(const QString &, int position, const QString &,
                                const QVector<int> &discardedSeparators = {});
    static MusicTextEdit eraseBefore(const TextParseResult &, int voice, int position);
};
} // namespace jianpu
