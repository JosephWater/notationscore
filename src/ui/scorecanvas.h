// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "core/scoredocument.h"
#include "input/scoreentrycontroller.h"
#include "input/scoreentryview.h"
#include "layout/jianpulayout.h"
#include <QWidget>

namespace jianpu
{
class ScoreCanvas final : public QWidget, public IScoreEntryView
{
    Q_OBJECT
  public:
    explicit ScoreCanvas(ScoreDocument *document, QWidget *parent = nullptr);
    const LayoutResult &layoutResult() const { return m_layout; }
    const ScoreSnapshot &score() const override { return m_score; }
    QStringList selection() const { return m_selection; }
    QStringList modelSelection() const override;
    int activeTrack() const override { return m_track; }
    void setActiveTrack(int track);
    void setZoom(qreal zoom);
    qreal zoom() const { return m_zoom; }
    void selectNext(int direction = 1);
    void selectId(const QString &id) override;
    void selectAppendPosition(int track) override;
    bool atAppendPosition() const override;
    ScoreEntryController *entryController() const { return m_entry; }
    bool apply(EditRequest request);
    void setInputDuration(int denominator, int dots = 0);
    void copy();
    void paste();
    void setKeyboardInput(bool enabled)
    {
        m_keyboardInput = enabled;
        update();
    }
    void paintScore(QPainter &painter, const QRectF &exposed, bool interactive = false) const;
  signals:
    void selectionChanged();
    void operationFailed(const QString &error);
    void zoomChanged(qreal zoom);
    void editLyricsRequested();
    void editMarkRequested();
    void editHeaderRequested(const QString &);

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
    ScoreEntryController *m_entry;
    QMap<int, QRectF> m_inputSlots;
    bool m_keyboardInput = false;
    bool m_dragging = false;
    QPointF m_dragStart;
    QPointF m_dragEnd;
    QString m_dragElement;
    LayoutResult m_dragLayout;
    QPointF m_elementDelta;
    void refresh();
    void buildInputSlots();
    void selectVertical(int direction);
};
} // namespace jianpu
