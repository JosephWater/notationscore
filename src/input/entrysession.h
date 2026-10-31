// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QMetaType>
namespace jianpu
{
enum class EntryMode
{
    Append,
    Overwrite,
    Insert
};
enum class InputSurface
{
    Text,
    Score
};
class EntrySession
{
  public:
    EntryMode mode = EntryMode::Append;
    InputSurface surface = InputSurface::Text;
    void select(bool end) { mode = end ? EntryMode::Append : EntryMode::Overwrite; }
};
} // namespace jianpu
Q_DECLARE_METATYPE(jianpu::EntryMode)
