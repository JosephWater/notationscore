// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QMap>
#include <QWidget>
class QComboBox;
namespace jianpu
{
class ScoreDocument;
class LayoutPanel final : public QWidget
{
    Q_OBJECT
  public:
    explicit LayoutPanel(ScoreDocument *, QWidget *parent = nullptr);

  private:
    ScoreDocument *m_document;
    QComboBox *m_presets;
    QMap<QString, QWidget *> m_fields;
    bool m_refreshing = false;
    void apply();
    void refresh();
};
} // namespace jianpu
