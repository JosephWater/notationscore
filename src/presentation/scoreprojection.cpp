// SPDX-License-Identifier: GPL-3.0-only
#include "scoreprojection.h"
namespace jianpu
{
ScoreProjection::ScoreProjection(std::unique_ptr<TextFormatAdapter> text,
                                 std::unique_ptr<IScoreLayout> layout)
    : m_text(text ? std::move(text) : TextFormatAdapter::create()),
      m_layout(layout ? std::move(layout) : std::make_unique<JianpuLayout>())
{
}
LayoutResult ScoreProjection::layout(const ScoreSnapshot &score, const QJsonObject &settings) const
{
    return m_layout->layout(score, settings);
}
QString ScoreProjection::align(const TextParseResult &text, const LayoutResult &layout) const
{
    QVector<MusicalTime> starts;
    for (const auto &system : layout.systems)
        starts << system.start;
    return m_text->alignedSource(text, starts);
}
} // namespace jianpu
