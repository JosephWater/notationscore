#pragma once

#include <QUndoStack>
#include <QWidget>
#include <array>

class PitchCommand;

// A disposable toolchain/interaction sample, not the future score file model.
class ScoreWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit ScoreWidget(QWidget *parent = nullptr);
    static constexpr int NoteCount = 8;
    static constexpr int MinimumPitch = -14;
    static constexpr int MaximumPitch = 20;
    int pitchAt(int index) const;
    int selectedIndex() const { return m_selected; }
    QRectF noteRect(int index) const;
    QUndoStack *undoStack() { return &m_undo; }
    static QString degreeText(int pitch);
    static int octaveDots(int pitch);

signals:
    void selectionChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    friend class PitchCommand;
    struct Note
    {
        int pitch; // Diatonic steps relative to middle C, with 1=C in this sample.
        int denominator;
    };
    void applyPitch(int index, int pitch);
    std::array<Note, NoteCount> m_notes{{{0, 4}, {1, 8}, {2, 8}, {4, 2},
                                      {7, 4}, {6, 8}, {-1, 8}, {0, 2}}};
    int m_selected = -1;
    QUndoStack m_undo;
};
