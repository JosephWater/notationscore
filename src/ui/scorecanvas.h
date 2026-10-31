// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "core/scoredocument.h"
#include "layout/jianpulayout.h"
#include <QWidget>

namespace jianpu
{
class ScoreCanvas final : public QWidget
{
    Q_OBJECT
  public:
    explicit ScoreCanvas(ScoreDocument *document, QWidget *parent = nullptr);
    const LayoutResult &layoutResult() const { return m_layout; }
    const ScoreSnapshot &score() const { return m_score; }
    QStringList selection() const { return m_selection; }
    int activeTrack() const { return m_track; }
    void setActiveTrack(int track);
    void setZoom(qreal zoom);
    qreal zoom() const { return m_zoom; }
    void selectNext(int direction = 1);
    void selectId(const QString &id);
    bool apply(EditRequest request);
    void setInputDuration(int denominator, int dots = 0);
    void copy();
    void paste();
    void setKeyboardInput(bool enabled)
    {
        m_keyboardInput = enabled;
        update();
    }
    void paintScore(QPainter &painter, const QRectF &exposed) const;
  signals:
    void selectionChanged();
    void operationFailed(const QString &error);
    void zoomChanged(qreal zoom);
    void editLyricsRequested();
    void editMarkRequested();

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void wheelEvent(QWheelEvent *) override;

  private:
    ScoreDocument *m_document;
    ScoreSnapshot m_score;
    LayoutResult m_layout;
    QStringList m_selection;
    int m_track = 0;
    qreal m_zoom = 1;
    int m_denominator = 4;
    int m_dots = 0;
    qint64 m_lastInputTick = -1;
    int m_lastInputTrack = -1;
    QString m_lastInputNoteId;
    bool m_keyboardInput = false;
    bool m_dragging = false;
    QPointF m_dragStart;
    QPointF m_dragEnd;
    void refresh();
    bool editLastSuffix(QChar suffix);
    void advanceInput(qint64 tick, int track);
};
} // namespace jianpu
