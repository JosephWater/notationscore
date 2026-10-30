// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "core/scoredocument.h"
#include <QMainWindow>
class QListWidget;
class QTreeWidget;
class QComboBox;
class QSpinBox;
class QLineEdit;
class QLabel;
class QPlainTextEdit;
class QAction;
class QCloseEvent;
namespace jianpu
{
class ScoreCanvas;
class InputController;
class MainWindow final : public QMainWindow
{
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr);
    bool openPath(const QString &path, bool interactive = true);
    ScoreDocument *document() { return &m_document; }
    ScoreCanvas *canvas() { return m_canvas; }
    void setConfirmClose(bool confirm) { m_confirmClose = confirm; }

  protected:
    void closeEvent(QCloseEvent *) override;

  private:
    ScoreDocument m_document;
    ScoreCanvas *m_canvas;
    InputController *m_input = nullptr;
    QListWidget *m_voices;
    QListWidget *m_marks;
    QPlainTextEdit *m_report;
    QComboBox *m_duration;
    QSpinBox *m_dots;
    QSpinBox *m_verse;
    QLineEdit *m_lyrics;
    QLabel *m_selectionInfo;
    QAction *m_undo;
    QAction *m_redo;
    bool m_refreshing = false;
    bool m_confirmClose = true;
    void buildMenus();
    void buildPanels();
    void refresh();
    void selectionChanged();
    void editLyrics();
    void editMark(const QString &kind);
    void editSelectedMark();
    bool save(bool saveAs = false);
    bool confirmDiscard();
    void showError(const QString &error);
    void exportFormat(const QString &extension);
};
} // namespace jianpu
