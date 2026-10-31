// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonObject>
namespace jianpu
{
struct ScoreSnapshot;
struct LayoutResult;
class IScoreLayout
{
  public:
    virtual ~IScoreLayout() = default;
    virtual LayoutResult layout(const ScoreSnapshot &, const QJsonObject &) const = 0;
};
} // namespace jianpu
