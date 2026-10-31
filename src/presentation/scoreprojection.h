// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/textformat.h"
#include "layout/jianpulayout.h"
#include <memory>
namespace jianpu
{
// The document coordinates transactions; this service coordinates their read-only
// text and page projections. Neither adapter knows about widgets or undo stacks.
class ScoreProjection
{
  public:
    ScoreProjection(std::unique_ptr<TextFormatAdapter> text = {},
                    std::unique_ptr<IScoreLayout> layout = {});
    const TextFormatAdapter &format() const { return *m_text; }
    LayoutResult layout(const ScoreSnapshot &, const QJsonObject &) const;
    QString align(const TextParseResult &, const LayoutResult &) const;

  private:
    std::unique_ptr<TextFormatAdapter> m_text;
    std::unique_ptr<IScoreLayout> m_layout;
};
} // namespace jianpu
