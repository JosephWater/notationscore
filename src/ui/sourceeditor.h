// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QPlainTextEdit>
class QCompleter;
namespace jianpu
{
class SourceEditor final : public QPlainTextEdit
{
    Q_OBJECT
  public:
    explicit SourceEditor(QWidget *parent = nullptr);
    int gutterWidth() const;
    void paintGutter(QPaintEvent *);
    void showDiagnostics(const QVector<QPair<int, int>> &ranges);

  protected:
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool event(QEvent *) override;

  private:
    QWidget *m_gutter;
    QCompleter *m_completer;
};
} // namespace jianpu
