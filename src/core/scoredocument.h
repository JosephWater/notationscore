// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "scoretypes.h"
#include <QObject>
#include <memory>
namespace mu::engraving
{
class EngravingProject;
}
namespace jianpu
{
class ScoreDocument final : public QObject
{
    Q_OBJECT
  public:
    explicit ScoreDocument(QObject *parent = nullptr);
    ~ScoreDocument() override;
    bool open(const QString &path, const ImportOptions &options = {});
    static QVector<MidiTrackInfo> midiTracks(const QString &path, QString *error = nullptr);
    bool newScore();
    bool save(const QString &path);
    bool exportFile(const QString &path);
    bool edit(const EditRequest &request);
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    bool modified() const;
    ScoreSnapshot snapshot() const;
    QString error() const { return m_error; }
    QString filePath() const { return m_path; }
    QJsonObject viewSettings() const { return m_view; }
    void setViewSettings(const QJsonObject &value);
    QByteArray copy(const QStringList &ids) const;
  signals:
    void changed();

  private:
    std::shared_ptr<mu::engraving::EngravingProject> m_project;
    quint64 m_revision = 0;
    QString m_path;
    QString m_error;
    QStringList m_warnings;
    QJsonObject m_view;
    QJsonObject m_savedView;
    bool m_imported = false;
    QVector<quint64> m_states{0};
    int m_cursor = 0;
    quint64 m_nextState = 1;
    quint64 m_savedState = 0;
    bool fail(const QString &message);
    void notify();
};
} // namespace jianpu
