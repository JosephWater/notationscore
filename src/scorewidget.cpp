#include "scorewidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QUndoCommand>
#include <algorithm>

class PitchCommand final : public QUndoCommand
{
public:
    PitchCommand(ScoreWidget *widget, int index, int before, int after)
        : m_widget(widget), m_index(index), m_before(before), m_after(after)
    {
        setText(QStringLiteral("修改音高"));
    }
    void undo() override { m_widget->applyPitch(m_index, m_before); }
    void redo() override { m_widget->applyPitch(m_index, m_after); }

private:
    ScoreWidget *m_widget;
    int m_index;
    int m_before;
    int m_after;
};

ScoreWidget::ScoreWidget(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("scoreCanvas"));
    setMinimumSize(1000, 380);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("简谱示例画布"));
    setToolTip(QStringLiteral("点击音符，↑↓ 改变音高；Ctrl+Z 撤销，Ctrl+Y 重做"));
}

int ScoreWidget::pitchAt(int index) const
{
    return m_notes.at(static_cast<std::size_t>(index)).pitch;
}

QString ScoreWidget::degreeText(int pitch)
{
    return QString::number((pitch % 7 + 7) % 7 + 1);
}

int ScoreWidget::octaveDots(int pitch)
{
    const int degree = (pitch % 7 + 7) % 7;
    return (pitch - degree) / 7;
}

QRectF ScoreWidget::noteRect(int index) const
{
    const qreal cell = (width() - 144.0) / NoteCount;
    const qreal measureGap = index >= 4 ? 24.0 : 0.0;
    return QRectF(54.0 + index * cell + measureGap, 174.0, cell - 12.0, 112.0);
}

void ScoreWidget::applyPitch(int index, int pitch)
{
    m_notes.at(static_cast<std::size_t>(index)).pitch = pitch;
    update();
}

void ScoreWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#edf2f6"));

    QFont titleFont = font();
    titleFont.setPixelSize(26);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor("#20344a"));
    painter.drawText(QRectF(32, 24, width() - 64, 42), QStringLiteral("简谱实验室"));
    QFont labelFont = font();
    labelFont.setPixelSize(14);
    painter.setFont(labelFont);
    painter.setPen(QColor("#526579"));
    painter.drawText(QRectF(34, 72, width() - 68, 26),
                     QStringLiteral("C++20 · Qt 6.10.2    /    绘制、选择与撤销验证"));

    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(QRectF(28, 118, width() - 56, 204), 12, 12);
    painter.setPen(QColor("#526579"));
    painter.drawText(QRectF(52, 136, width() - 104, 26),
                     QStringLiteral("1 = C     4/4                                          示例 · 两小节"));

    QFont noteFont = font();
    noteFont.setPixelSize(38);
    noteFont.setBold(true);
    for (int i = 0; i < NoteCount; ++i)
    {
        const auto box = noteRect(i);
        if (i == m_selected)
        {
            painter.setPen(QPen(QColor("#2878c8"), 1.5));
            painter.setBrush(QColor("#e6f1ff"));
            painter.drawRoundedRect(box, 8, 8);
        }
        const auto &note = m_notes[static_cast<std::size_t>(i)];
        painter.setFont(noteFont);
        painter.setPen(i == m_selected ? QColor("#125b9e") : QColor("#20344a"));
        painter.setBrush(painter.pen().color());
        const qreal center = box.center().x() - (note.denominator == 2 ? 17.0 : 0.0);
        painter.drawText(QRectF(center - 20, 200, 40, 52), Qt::AlignCenter,
                         degreeText(note.pitch));
        const int dots = octaveDots(note.pitch);
        for (int dot = 0; dot < std::abs(dots); ++dot)
        {
            const qreal y = dots > 0 ? 192.0 - dot * 9.0 : 265.0 + dot * 9.0;
            painter.drawEllipse(QPointF(center, y), 2.8, 2.8);
        }
        if (note.denominator == 8)
        {
            painter.setPen(QPen(painter.pen().color(), 2));
            painter.drawLine(QPointF(center - 13, 255), QPointF(center + 13, 255));
        }
        if (note.denominator == 2)
        {
            painter.setPen(QPen(painter.pen().color(), 2));
            painter.drawLine(QPointF(center + 28, 229), QPointF(center + 49, 229));
        }
    }
    painter.setPen(QPen(QColor("#526579"), 1.5));
    const qreal barX = (noteRect(3).right() + noteRect(4).left()) / 2;
    painter.drawLine(QPointF(barX, 197), QPointF(barX, 269));
    painter.drawLine(QPointF(width() - 52, 197), QPointF(width() - 52, 269));
    painter.drawLine(QPointF(width() - 47, 197), QPointF(width() - 47, 269));
    painter.setFont(labelFont);
    painter.setPen(QColor("#526579"));
    painter.drawText(QRectF(34, 342, width() - 68, 24),
                     QStringLiteral("点击选择音符  ·  ↑ ↓ 修改音高  ·  Ctrl+Z 撤销  ·  Ctrl+Y 重做"));
}

void ScoreWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
    {
        QWidget::mousePressEvent(event);
        return;
    }
    setFocus();
    int selected = -1;
    for (int i = 0; i < NoteCount; ++i)
    {
        if (noteRect(i).contains(event->position()))
        {
            selected = i;
            break;
        }
    }
    if (selected != m_selected)
    {
        m_selected = selected;
        emit selectionChanged(selected);
        update();
    }
    event->accept();
}

void ScoreWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Undo))
        m_undo.undo();
    else if (event->matches(QKeySequence::Redo))
        m_undo.redo();
    else if (m_selected >= 0 && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down))
    {
        const int before = pitchAt(m_selected);
        const int delta = event->key() == Qt::Key_Up ? 1 : -1;
        const int after = std::clamp(before + delta, MinimumPitch, MaximumPitch);
        if (before != after)
            m_undo.push(new PitchCommand(this, m_selected, before, after));
    }
    else
    {
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}
