// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "entrysession.h"
#include "musictextediting.h"
#include "scoreentryview.h"
#include <QObject>
namespace jianpu
{
class ScoreDocument;
class ScoreEntryController : public QObject
{
    Q_OBJECT
  public:
    ScoreEntryController(ScoreDocument *, IScoreEntryView *, QObject *parent = nullptr);
    EntryMode mode() const { return m_session.mode; }
    InputSurface surface() const { return m_session.surface; }
    void setMode(EntryMode);
    void focus(InputSurface);
    void selectTarget(bool atEnd);
    void setInputDuration(int denominator, int dots);
    bool input(const QString &);
    bool backspace();
    bool wantsLiteralLetters() const;
  signals:
    void modeChanged(jianpu::EntryMode);
    void error(const QString &);
    void sourceCursorChanged(int);

  private:
    ScoreDocument *m_document;
    IScoreEntryView *m_view;
    EntrySession m_session;
    int m_denominator = 4, m_dots = 0;
    qint64 m_lastInputTick = -1;
    int m_lastInputTrack = -1;
    QString m_lastInputNoteId;
    int m_lastCursor = -1;
    quint64 m_group = 0;
    bool m_applying = false;
    SourceChanges m_draftChanges;
    QString m_draftSource;
    bool inGrace() const;
    bool overwrite(int);
    bool suffix(QChar);
    void advance(qint64, int);
    int voiceNumber() const;
    bool applySource(MusicTextEdit);
    bool insertText(const QString &);
};
} // namespace jianpu
