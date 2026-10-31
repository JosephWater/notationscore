// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "core/scoretypes.h"
namespace jianpu
{
class IScoreEntryView
{
  public:
    virtual ~IScoreEntryView() = default;
    virtual const ScoreSnapshot &score() const = 0;
    virtual QStringList modelSelection() const = 0;
    virtual int activeTrack() const = 0;
    virtual void selectId(const QString &) = 0;
    virtual void selectAppendPosition(int track) = 0;
    virtual bool atAppendPosition() const = 0;
};
} // namespace jianpu
