// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "input/textformat.h"
#include <QObject>
#include <QTimer>
class QDockWidget;
class QLineEdit;
class QLabel;
class QMainWindow;
namespace jianpu
{
class ScoreDocument;
class ScoreCanvas;
class SourceEditor;
class InputController final : public QObject
{
    Q_OBJECT
  public:
    InputController(ScoreDocument *, ScoreCanvas *, QMainWindow *);
    bool flush();
    SourceEditor *editor() const { return m_editor; }
    void sourceSelection();
    void beginExternalSource();
    void updateExternalSource(const QString &);
    bool endExternalSource(const QString &, bool commit, const QString &message = {});
    bool externalSourceActive() const { return m_external; }
  signals:
    void error(const QString &);

  protected:
    bool eventFilter(QObject *, QEvent *) override;

  private:
    ScoreDocument *m_document;
    ScoreCanvas *m_canvas;
    SourceEditor *m_editor;
    QDockWidget *m_dock;
    QLineEdit *m_inline;
    QLabel *m_context;
    QTimer m_timer;
    bool m_refreshing = false, m_seenQ = false, m_composing = false;
    bool m_external = false;
    bool m_annotation = false;
    int m_voice = 1, m_inlineStart = -1;
    QString m_inlinePrefix;
    QString m_observedSource;
    SourceChanges m_sourceChanges;
    void refresh();
    void commit();
    void command(int key);
    void appendMusic(const QString &);
    void editHeader(const QString &field);
    void startLyrics();
    void updateInline();
    int voiceNumber() const;
};
} // namespace jianpu
