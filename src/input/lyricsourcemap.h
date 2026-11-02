// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "textformat.h"
namespace jianpu
{
struct LyricSourceRange
{
    int voice = 1, verse = 0, offset = 0, length = 0;
    MusicalTime position;
};
class LyricSourceMap
{
  public:
    static QVector<LyricSourceRange> ranges(const TextParseResult &);
};
} // namespace jianpu
