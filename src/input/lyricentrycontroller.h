// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "entrysession.h"
#include "layout/jianpulayout.h"
#include <QObject>
namespace jianpu
{
class ScoreDocument;
class LyricEntryController : public QObject
{
    Q_OBJECT
  public:
    explicit LyricEntryController(ScoreDocument *, QObject *parent = nullptr);
    void select(const LyricSlot &slot)
    {
        m_target = slot;
        m_inserted = false;
    }
    bool input(const QString &, EntryMode);
    bool erase(bool backward, EntryMode);
    bool replaceRange(const QStringList &ids, const QString &text);
    int newVerse(int track);
  signals:
    void selectRequested(const QString &id);
    void error(const QString &message);

  private:
    ScoreDocument *m_document;
    LyricSlot m_target;
    bool m_inserted = false;
    QVector<EventData> events() const;
    bool commit(const QVector<EventData> &, const QStringList &, int selectIndex,
                const QStringList &extra = {});
};
} // namespace jianpu
