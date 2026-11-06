// SPDX-License-Identifier: GPL-3.0-only
#include "sourceeditor.h"
#include <QAbstractItemView>
#include <QCompleter>
#include <QKeyEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextDocument>
namespace jianpu
{
namespace
{
class Gutter : public QWidget
{
    SourceEditor *editor;

  public:
    explicit Gutter(SourceEditor *e) : QWidget(e), editor(e) {}
    void paintEvent(QPaintEvent *e) override { editor->paintGutter(e); }
};
class Highlighter : public QSyntaxHighlighter
{
  public:
    explicit Highlighter(QTextDocument *d) : QSyntaxHighlighter(d) {}
    void highlightBlock(const QString &text) override
    {
        QTextCharFormat field, note, comment, symbol;
        field.setForeground(QColor("#315d9a"));
        field.setFontWeight(QFont::DemiBold);
        note.setForeground(QColor("#924324"));
        symbol.setForeground(QColor("#7051a4"));
        comment.setForeground(QColor("#718078"));
        comment.setFontItalic(true);
        if (text.trimmed().startsWith('#'))
        {
            setFormat(0, text.size(), comment);
            return;
        }
        auto prefix = QRegularExpression("^\\s*[A-Z]\\d*\\s*[:：]").match(text);
        if (prefix.hasMatch())
            setFormat(0, prefix.capturedLength(), field);
        if (text.trimmed().startsWith('Q'))
        {
            auto matches = QRegularExpression("[0-9][/.,'#\\$=\\-]*")
                               .globalMatch(text.mid(prefix.capturedLength()));
            while (matches.hasNext())
            {
                auto m = matches.next();
                setFormat(prefix.capturedLength() + m.capturedStart(), m.capturedLength(), note);
            }
            auto marks = QRegularExpression("[|:()<>!\\[\\]]").globalMatch(text);
            while (marks.hasNext())
            {
                auto m = marks.next();
                setFormat(m.capturedStart(), m.capturedLength(), symbol);
            }
        }
    }
};
} // namespace
SourceEditor::SourceEditor(QWidget *parent) : QPlainTextEdit(parent), m_gutter(new Gutter(this))
{
    setObjectName("sourceEditor");
    setUndoRedoEnabled(false);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont f("DejaVu Sans Mono");
    f.setPointSize(11);
    setFont(f);
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
    new Highlighter(document());
    m_completer =
        new QCompleter(QStringList{"V: 1.0", "V: 1.1", "M: ", "B: ", "Z: ", "D: C", "P: 4/4",
                                   "Q: ", "C: ", "Q1: ", "Q2: ", "C1: ", "C2: "},
                       this);
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    connect(m_completer, qOverload<const QString &>(&QCompleter::activated), this,
            [this](const QString &value)
            {
                auto c = textCursor();
                c.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
                c.insertText(value);
            });
    connect(this, &QPlainTextEdit::blockCountChanged, this,
            [this]
            {
                setViewportMargins(gutterWidth(), 0, 0, 0);
                m_gutter->update();
            });
    connect(this, &QPlainTextEdit::updateRequest, this,
            [this](const QRect &r, int dy)
            {
                if (dy)
                    m_gutter->scroll(0, dy);
                else
                    m_gutter->update(0, r.y(), m_gutter->width(), r.height());
            });
    setViewportMargins(gutterWidth(), 0, 0, 0);
}
int SourceEditor::gutterWidth() const
{
    return 16 + fontMetrics().horizontalAdvance('9') * QString::number(blockCount()).size();
}
void SourceEditor::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);
    auto r = contentsRect();
    m_gutter->setGeometry(r.left(), r.top(), gutterWidth(), r.height());
}
void SourceEditor::paintGutter(QPaintEvent *)
{
    QPainter p(m_gutter);
    p.fillRect(m_gutter->rect(), QColor("#f0f3f5"));
    p.setPen(QColor("#8994a2"));
    auto block = firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    while (block.isValid() && top < height())
    {
        int h = qRound(blockBoundingRect(block).height());
        if (block.isVisible())
            p.drawText(0, top, gutterWidth() - 7, h, Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(block.blockNumber() + 1));
        top += h;
        block = block.next();
    }
}
void SourceEditor::showDiagnostics(const QVector<QPair<int, int>> &ranges)
{
    QList<QTextEdit::ExtraSelection> selections;
    for (auto range : ranges)
    {
        QTextEdit::ExtraSelection s;
        s.cursor = QTextCursor(document());
        s.cursor.setPosition(std::clamp(range.first, 0, int(document()->characterCount() - 1)));
        s.cursor.setPosition(std::min(range.first + std::max(1, range.second),
                                      int(document()->characterCount() - 1)),
                             QTextCursor::KeepAnchor);
        s.format.setUnderlineColor(QColor("#c84d40"));
        s.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        selections << s;
    }
    setExtraSelections(selections);
}
bool SourceEditor::event(QEvent *e)
{
    if (e->type() == QEvent::ShortcutOverride)
    {
        auto *k = static_cast<QKeyEvent *>(e);
        if (k->matches(QKeySequence::Copy) || k->matches(QKeySequence::Paste) ||
            k->matches(QKeySequence::Cut) || k->matches(QKeySequence::SelectAll) ||
            k->matches(QKeySequence::Undo) || k->matches(QKeySequence::Redo) ||
            k->key() == Qt::Key_Delete)
        {
            e->accept();
            return true;
        }
    }
    return QPlainTextEdit::event(e);
}
void SourceEditor::keyPressEvent(QKeyEvent *e)
{
    if (m_completer->popup()->isVisible() &&
        (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Escape ||
         e->key() == Qt::Key_Tab))
    {
        e->ignore();
        return;
    }
    if (e->key() == Qt::Key_Space && e->modifiers() == Qt::ControlModifier)
    {
        auto c = textCursor();
        c.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
        m_completer->setCompletionPrefix(c.selectedText());
        m_completer->complete(cursorRect());
        return;
    }
    QPlainTextEdit::keyPressEvent(e);
}
} // namespace jianpu
