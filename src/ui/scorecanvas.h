// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "core/scoredocument.h"
#include "input/lyricentrycontroller.h"
#include "input/scoreentrycontroller.h"
#include "input/scoreentryview.h"
#include "input/selectionpolicy.h"
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
    const LyricSlot *selectedLyric() const;
    bool lyricSelected() const { return selectedLyric() != nullptr; }
    void startLyrics();
    void selectEnd();
    bool composingLyrics() const { return !m_preedit.isEmpty(); }
    bool apply(EditRequest request);
    void setInputDuration(int denominator, int dots = 0);
    void copy();
    void deleteSelection();
    ScoreSelection contentSelection() const;
    void pasteInsert();
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
    void inputMethodEvent(QInputMethodEvent *) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery) const override;

  private:
    ScoreDocument *m_document;
    ScoreSnapshot m_score;
    LayoutResult m_layout;
    QStringList m_selection;
    int m_track = 0;
    qreal m_zoom = 1;
    ScoreEntryController *m_entry;
    LyricEntryController *m_lyricEntry;
    QString m_preedit;
    bool m_selectingLyrics = false;
    QString m_rangeAnchor;
    SelectionTarget m_pressedTarget;
    QStringList m_selectionBeforeDrag;
    int m_contentGesture = 0;
    bool m_gestureMoved = false, m_pendingToggle = false;
    Qt::KeyboardModifiers m_pressModifiers;
    QByteArray m_dragFragment;
    ScoreDropTarget m_dropTarget;
    QString m_dropId;
    QRectF m_dropBounds;
    bool m_dropValid = false;
    QVector<SelectionTarget> selectionTargets() const;
    SelectionTarget targetAt(QPointF, SelectionKind forced = SelectionKind::None) const;
    void moveContentSelection(QMouseEvent *);
    bool releaseContentSelection(QMouseEvent *);
    bool beginContentSelection(QPointF, Qt::KeyboardModifiers);
    bool dropTargetAt(QPointF, const QByteArray &, ScoreDropTarget &, QRectF &) const;
    void finishFragmentSelection(const QByteArray &, const ScoreDropTarget &, bool move,
                                 const ScoreSnapshot &before);
    void pasteContent(bool forceInsert);
    QMap<int, QRectF> m_inputSlots;
    bool m_keyboardInput = false;
    bool m_dragging = false;
    QPointF m_dragStart;
    QPointF m_dragEnd;
    QString m_dragElement;
    LayoutResult m_dragLayout;
    QPointF m_elementDelta;
    QVector<QLineF> m_snapGuides;
    int m_transformMode = 0;
    QPointF m_transformOrigin;
    double m_rotation = 0, m_scale = 1, m_previewRotation = 0, m_previewScale = 1;
    void refresh();
    void buildInputSlots();
    void selectVertical(int direction);
    void selectSpatial(int dx, int dy);
};
} // namespace jianpu
