// SPDX-License-Identifier: GPL-3.0-only
#include "inputcontroller.h"
#include "core/scoredocument.h"
#include "ui/scorecanvas.h"
#include "ui/sourceeditor.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QInputDialog>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QTextBlock>
#include <QTextBoundaryFinder>
#include <QTextCursor>
#include <QVBoxLayout>
namespace jianpu
{
namespace
{
struct LyricLine
{
    int end, voice, segment, verse;
    QString text;
};
QVector<LyricLine> lyricLines(const QString &source)
{
    QVector<LyricLine> result;
    int current = 1, segment = 0, offset = 0;
    QMap<int, int> segments, verses;
    for (const auto &line : source.split('\n'))
    {
        auto field = QRegularExpression("^\\s*([QC])(\\d*)\\s*[:：]\\s*(.*)$").match(line);
        if (field.hasMatch())
        {
            int voice = field.captured(2).isEmpty() ? current : field.captured(2).toInt();
            if (field.captured(1) == "Q")
            {
                current = voice;
                segments[voice] = ++segment;
                verses[voice] = 0;
            }
            else
                result << LyricLine{offset + int(line.size()), voice, segments.value(voice),
                                    verses[voice]++, field.captured(3)};
        }
        offset += line.size() + 1;
    }
    return result;
}
int lyricEndCursor(const QString &oldSource, const QString &source, const TextParseResult &old,
                   const TextParseResult &now, int cursor)
{
    for (const auto &line : lyricLines(oldSource))
        if (cursor == line.end)
        {
            int last = std::max(0, int(FanqieFormat::lyricTokens(line.text).size()) - 1);
            const TextNote *target = nullptr;
            for (const auto &n : old.notes)
                if (n.voice == line.voice && n.segment == line.segment && n.degree != 0 &&
                    n.degree != 8 && !n.tieBack)
                {
                    target = &n;
                    if (last-- == 0)
                        break;
                }
            if (target)
                for (const auto &n : now.notes)
                    if (n.voice == target->voice && n.position == target->position)
                        for (const auto &newLine : lyricLines(source))
                            if (newLine.voice == line.voice && newLine.segment == n.segment &&
                                newLine.verse == line.verse)
                                return newLine.end;
        }
    return -1;
}
} // namespace
InputController::InputController(ScoreDocument *d, ScoreCanvas *c, QMainWindow *window)
    : QObject(window), m_document(d), m_canvas(c)
{
    m_dock = new QDockWidget(tr("文字稿 · 番茄式 1.0"), window);
    m_dock->setObjectName("sourceDock");
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    auto *hint = new QLabel(tr(
        "点击文字稿编辑文字；点击谱面编辑音符\nInsert 切换插入／修改；End 末尾追加；Esc 返回命令"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    m_context = new QLabel(tr("C1 的 1 表示声部；同一曲行的多个 C1 依次为第 1、2… 段歌词。"));
    m_context->setObjectName("lyricContext");
    m_context->setWordWrap(true);
    layout->addWidget(m_context);
    m_editor = new SourceEditor;
    connect(m_editor->document(), &QTextDocument::contentsChange, this,
            [this](int position, int removed, int added)
            {
                auto source = m_editor->toPlainText();
                if (!m_refreshing)
                {
                    if (m_sourceChanges.edits.isEmpty())
                        m_sourceChanges.before = m_document->sourceText();
                    m_sourceChanges.edits << SourceChange{
                        position,
                        std::min(removed, std::max(0, int(m_observedSource.size()) - position)),
                        std::min(added, std::max(0, int(source.size()) - position))};
                }
                m_observedSource = source;
            });
    layout->addWidget(m_editor);
    m_editor->setMinimumWidth(330);
    m_inline = new QLineEdit;
    m_inline->setObjectName("keyboardTextInput");
    m_inline->hide();
    layout->addWidget(m_inline);
    m_dock->setWidget(panel);
    window->addDockWidget(Qt::LeftDockWidgetArea, m_dock);
    m_editor->installEventFilter(this);
    m_canvas->installEventFilter(this);
    m_inline->installEventFilter(this);
    m_timer.setSingleShot(true);
    m_timer.setInterval(160);
    connect(&m_timer, &QTimer::timeout, this, &InputController::commit);
    connect(m_editor, &QPlainTextEdit::textChanged, this,
            [this]
            {
                if (!m_refreshing)
                {
                    m_timer.start();
                }
            });
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this,
            &InputController::sourceSelection);
    connect(m_document, &ScoreDocument::changed, this, &InputController::refresh);
    connect(m_document, &ScoreDocument::loaded, this,
            [this]
            {
                m_timer.stop();
                m_sourceChanges = {};
                m_seenQ = false;
                m_voice = 1;
                m_inline->hide();
                refresh();
                auto cursor = m_editor->textCursor();
                cursor.setPosition(std::clamp(m_document->inputState().value("cursor").toInt(), 0,
                                              int(m_editor->toPlainText().size())));
                m_editor->setTextCursor(cursor);
            });
    connect(m_canvas, &ScoreCanvas::selectionChanged, this,
            [this]
            {
                if (m_refreshing || m_editor->hasFocus() || m_timer.isActive())
                    return;
                for (const auto &measure : m_canvas->score().measures)
                    for (const auto &event : measure.events)
                    {
                        bool selected = m_canvas->modelSelection().contains(event.id);
                        for (const auto &n : event.notes)
                            selected |= m_canvas->modelSelection().contains(n.id);
                        if (selected && event.sourceStart >= 0)
                        {
                            m_refreshing = true;
                            auto c = m_editor->textCursor();
                            c.setPosition(event.sourceStart);
                            c.setPosition(std::min(event.sourceStart + event.sourceLength,
                                                   int(m_editor->toPlainText().size())),
                                          QTextCursor::KeepAnchor);
                            m_editor->setTextCursor(c);
                            m_editor->ensureCursorVisible();
                            m_refreshing = false;
                            return;
                        }
                    }
            });
    connect(m_canvas, &ScoreCanvas::editHeaderRequested, this, &InputController::editHeader);
    connect(m_inline, &QLineEdit::textEdited, this, [this] { updateInline(); });
    connect(m_inline, &QLineEdit::returnPressed, this,
            [this]
            {
                flush();
                m_inline->hide();
                m_annotation = false;
                m_canvas->setFocus();
            });
    connect(m_canvas->entryController(), &ScoreEntryController::sourceCursorChanged, this,
            [this](int position)
            {
                QScopedValueRollback<bool> guard(m_refreshing, true);
                auto cursor = m_editor->textCursor();
                cursor.setPosition(std::clamp(position, 0, int(m_editor->toPlainText().size())));
                m_editor->setTextCursor(cursor);
                m_editor->ensureCursorVisible();
            });
    refresh();
}
void InputController::refresh()
{
    if (m_refreshing || m_timer.isActive())
        return;
    auto source = m_document->sourceText();
    if (source != m_editor->toPlainText())
    {
        m_refreshing = true;
        int pos = m_editor->textCursor().position();
        // Keep the caret on the same musical token when automatic bars or Q/C
        // rows are inserted before it. This also keeps suffix entry before |.
        const auto oldParsed = FanqieFormat().parse(m_editor->toPlainText());
        const auto newParsed = FanqieFormat().parse(source);
        int lyricCursor =
            lyricEndCursor(m_editor->toPlainText(), source, oldParsed, newParsed, pos);
        if (lyricCursor >= 0)
            pos = lyricCursor;
        else
            for (const auto &old : oldParsed.notes)
                if (pos >= old.offset && pos <= old.offset + old.length)
                {
                    for (const auto &now : newParsed.notes)
                        if (now.voice == old.voice && now.position == old.position)
                        {
                            pos = now.offset + std::min(pos - old.offset, now.length);
                            break;
                        }
                    break;
                }
        m_editor->setPlainText(source);
        auto c = m_editor->textCursor();
        c.setPosition(std::min(pos, int(source.size())));
        m_editor->setTextCursor(c);
        m_refreshing = false;
    }
    auto parsed =
        FanqieFormat().parse(source, m_document->viewSettings().value("inputHabits").toObject());
    QVector<QPair<int, int>> ranges;
    for (auto d : parsed.diagnostics)
        if (d.blocking)
            ranges << qMakePair(d.offset, d.length);
    m_editor->showDiagnostics(ranges);
}
void InputController::commit()
{
    if (m_composing)
    {
        m_timer.start();
        return;
    }
    auto source = m_editor->toPlainText();
    if (source == m_document->sourceText())
    {
        m_sourceChanges = {};
        return;
    }
    QString mergeKey;
    // Coalesce a directly entered note and its suffixes; paste and edits in the middle are
    // independent.
    {
        auto parsed = FanqieFormat().parse(source);
        int cursor = m_editor->textCursor().position();
        for (const auto &n : parsed.notes)
            if (cursor > n.offset && cursor <= n.offset + n.length)
                mergeKey = QString("note:%1:%2").arg(n.voice).arg(n.offset);
    }
    m_refreshing = true;
    auto changes = m_sourceChanges;
    m_sourceChanges = {};
    bool ok = m_document->applyText(source, mergeKey, changes);
    m_refreshing = false;
    if (!ok)
    {
        auto message = m_document->error();
        m_refreshing = true;
        m_document->storeInputDraft(source, message);
        m_refreshing = false;
        emit error(message);
        return;
    }
    refresh();
    m_canvas->setKeyboardInput(m_canvas->hasFocus());
    if (m_editor->hasFocus())
        sourceSelection();
}
bool InputController::flush()
{
    if (m_composing)
        return false;
    m_timer.stop();
    commit();
    return m_editor->toPlainText() == m_document->sourceText();
}
int InputController::voiceNumber() const
{
    auto voices = m_canvas->score().voices;
    auto parsed = FanqieFormat().parse(m_document->sourceText());
    auto ids = parsed.voiceNames.keys();
    int index = 0;
    for (const auto &v : voices)
    {
        if (v.percussion)
            continue;
        if (v.track == m_canvas->activeTrack())
            return index < ids.size() ? ids[index] : index + 1;
        ++index;
    }
    return ids.isEmpty() ? 1 : ids.first();
}
void InputController::sourceSelection()
{
    if (m_refreshing)
        return;
    int pos = m_editor->textCursor().position();
    auto source = m_editor->toPlainText();
    bool lyricContext = false;
    for (const auto &line : lyricLines(source))
    {
        int start = source.lastIndexOf('\n', std::max(0, line.end - 1)) + 1;
        if (pos >= start && pos <= line.end)
        {
            m_context->setText(
                tr("当前：第 %1 声部 · 第 %2 段歌词。C%1 表示声部，重复字段按段次排列。")
                    .arg(line.voice)
                    .arg(line.verse + 1));
            lyricContext = true;
            break;
        }
    }
    if (!lyricContext)
        m_context->setText(
            tr("点击谱面后依次覆盖；End 在末尾追加。C1 表示声部，多行 C1 依次对应多段歌词。"));
    auto state = m_document->inputState();
    state["cursor"] = pos;
    m_document->setInputState(state);
    if (!m_editor->hasFocus())
        return;
    QScopedValueRollback<bool> selectingFromSource(m_refreshing, true);
    for (const auto &m : m_canvas->score().measures)
        for (const auto &e : m.events)
            if (e.sourceStart >= 0 && pos >= e.sourceStart && pos <= e.sourceStart + e.sourceLength)
            {
                m_canvas->selectId(e.notes.isEmpty() ? e.id : e.notes.first().id);
                for (const auto &hit : m_canvas->layoutResult().hits)
                    if (hit.eventId == e.id)
                    {
                        auto *area =
                            qobject_cast<QScrollArea *>(m_canvas->parentWidget()->parentWidget());
                        if (area)
                            area->ensureVisible(qRound(hit.rect.center().x() * m_canvas->zoom()),
                                                qRound(hit.rect.center().y() * m_canvas->zoom()),
                                                80, 80);
                        break;
                    }
                return;
            }
}
void InputController::appendMusic(const QString &text)
{
    flush();
    if (text == "\b")
        m_canvas->entryController()->backspace();
    else
        m_canvas->entryController()->input(text);
}
void InputController::startLyrics()
{
    m_dock->show();
    m_dock->raise();
    m_annotation = false;
    flush();
    m_voice = voiceNumber();
    m_lyricBase = m_editor->toPlainText();
    m_globalLyrics = true;
    auto parsed = FanqieFormat().parse(m_lyricBase);
    m_lyricVerse = parsed.verseRows.value(m_voice);
    for (const auto &note : parsed.notes)
        if (note.voice == m_voice)
            for (auto it = note.lyrics.begin(); it != note.lyrics.end(); ++it)
                m_lyricVerse = std::max(m_lyricVerse, it.key() + 1);
    m_inline->clear();
    m_inline->setPlaceholderText(
        tr("第 %1 段，从曲首填词；@ 跳音，~ 合字，Enter 完成").arg(m_lyricVerse + 1));
    updateInline();
    flush();
    m_inline->show();
    m_inline->setFocus();
    m_canvas->setKeyboardInput(false);
}
void InputController::updateInline()
{
    if (m_globalLyrics && !m_annotation)
    {
        auto parsed = FanqieFormat().parse(m_lyricBase);
        auto words = FanqieFormat::lyricTokens(m_inline->text());
        QMap<int, int> counts, ends, verses;
        QMap<int, int> currentSegment;
        int segment = 0, current = 1, offset = 0;
        for (const auto &n : parsed.notes)
            if (n.voice == m_voice && n.degree != 0 && n.degree != 8 && !n.tieBack)
                ++counts[n.segment];
        for (const auto &line : m_lyricBase.split('\n'))
        {
            auto field = QRegularExpression("^\\s*([QC])(\\d*)\\s*[:：]").match(line);
            if (field.hasMatch())
            {
                int voice = field.captured(2).isEmpty() ? current : field.captured(2).toInt();
                if (field.captured(1) == "Q")
                {
                    ++segment;
                    current = voice;
                    currentSegment[voice] = segment;
                    if (voice == m_voice)
                    {
                        ends[segment] = offset + line.size();
                        verses[segment] = 0;
                    }
                }
                else if (voice == m_voice && currentSegment.contains(voice))
                {
                    int s = currentSegment[voice];
                    ends[s] = offset + line.size();
                    ++verses[s];
                }
            }
            offset += line.size() + 1;
        }
        struct Insert
        {
            int pos;
            QString text;
        };
        QVector<Insert> inserts;
        int used = 0;
        for (auto it = ends.begin(); it != ends.end(); ++it)
        {
            QString value;
            int take = std::min(counts.value(it.key()), int(words.size()) - used);
            if (it.key() == ends.lastKey())
                take = words.size() - used;
            QStringList chunk;
            for (int i = 0; i < take; ++i)
            {
                auto word = words[used++];
                QStringList chars;
                QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, word);
                int begin = 0, end = 0;
                while ((end = finder.toNextBoundary()) >= 0)
                {
                    chars << word.mid(begin, end - begin);
                    begin = end;
                }
                chunk << chars.join("~");
            }
            while (verses[it.key()] < m_lyricVerse)
            {
                value += QString("\nC%1: ").arg(m_voice);
                ++verses[it.key()];
            }
            value += QString("\nC%1: %2").arg(m_voice).arg(chunk.join(" "));
            inserts << Insert{it.value(), value};
        }
        QString source = m_lyricBase;
        std::sort(inserts.begin(), inserts.end(), [](auto a, auto b) { return a.pos > b.pos; });
        for (auto i : inserts)
            source.insert(i.pos, i.text);
        m_refreshing = true;
        m_editor->setPlainText(source);
        m_refreshing = false;
        m_timer.start();
        return;
    }
    auto source = m_editor->toPlainText();
    int end = source.indexOf('\n', m_inlineStart);
    if (end < 0)
        end = source.size();
    if (m_annotation)
        --end;
    auto value = m_inline->text();
    if (m_annotation)
        value.replace("\\", "\\\\").replace("\"", "\\\"");
    source.replace(m_inlineStart, end - m_inlineStart, value);
    m_refreshing = true;
    m_editor->setPlainText(source);
    m_refreshing = false;
    m_timer.start();
}
void InputController::editHeader(const QString &field)
{
    flush();
    auto source = m_editor->toPlainText();
    auto regex = QRegularExpression("^[ \t]*" + field + "\\s*[:：]\\s*(.*)$",
                                    QRegularExpression::MultilineOption);
    auto match = regex.match(source);
    QString value = match.captured(1);
    bool ok = false;
    if (field == "Z")
    {
        QStringList poets, composers, others;
        auto authors = regex.globalMatch(source);
        while (authors.hasNext())
        {
            auto author = authors.next().captured(1).trimmed();
            auto role = QRegularExpression("\\s*(词曲|作词|作曲|词|曲)$").match(author);
            if (role.hasMatch())
            {
                auto name = author.left(role.capturedStart()).trimmed();
                if (role.captured(1).contains(QStringLiteral("词")))
                    poets << name;
                if (role.captured(1).contains(QStringLiteral("曲")))
                    composers << name;
            }
            else
                others << author;
        }
        QDialog dialog(qobject_cast<QWidget *>(parent()));
        dialog.setWindowTitle(tr("词曲作者"));
        auto *form = new QFormLayout(&dialog);
        QLineEdit poet(poets.join("、")), composer(composers.join("、")), other(others.join("；"));
        form->addRow(tr("作词"), &poet);
        form->addRow(tr("作曲"), &composer);
        form->addRow(tr("其他署名（；分隔）"), &other);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form->addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        poet.setFocus();
        ok = dialog.exec() == QDialog::Accepted;
        QStringList lines;
        if (!poet.text().trimmed().isEmpty() && poet.text().trimmed() == composer.text().trimmed())
            lines << poet.text().trimmed() + QStringLiteral(" 词曲");
        else
        {
            if (!poet.text().trimmed().isEmpty())
                lines << poet.text().trimmed() + QStringLiteral(" 词");
            if (!composer.text().trimmed().isEmpty())
                lines << composer.text().trimmed() + QStringLiteral(" 曲");
        }
        for (auto line : other.text().split(QRegularExpression("[;；]")))
            if (!line.trimmed().isEmpty())
                lines << line.trimmed();
        value = lines.join('\n');
    }
    else
        value = QInputDialog::getText(qobject_cast<QWidget *>(parent()),
                                      field == "B"   ? tr("题目")
                                      : field == "D" ? tr("调号")
                                                     : tr("拍号"),
                                      field == "D"   ? tr("例如 D、F#、B$、Am")
                                      : field == "P" ? tr("例如 4/4、6/8")
                                                     : tr("题目"),
                                      QLineEdit::Normal, value, &ok);
    if (!ok)
        return;
    if (field == "Z")
    {
        QStringList lines;
        for (auto v : value.split('\n'))
            if (!v.trimmed().isEmpty())
                lines << "Z: " + v;
        value = lines.join('\n');
    }
    else
        value = field + ": " + value;
    if (field == "Z" && match.hasMatch())
    {
        QVector<QPair<int, int>> positions;
        auto matches = regex.globalMatch(source);
        while (matches.hasNext())
        {
            auto m = matches.next();
            positions << qMakePair(int(m.capturedStart()), int(m.capturedLength()));
        }
        for (int i = positions.size() - 1; i >= 0; --i)
            source.replace(positions[i].first, positions[i].second, i == 0 ? value : QString());
    }
    else if (match.hasMatch())
        source.replace(match.capturedStart(), match.capturedLength(), value);
    else if (!value.isEmpty())
        source.prepend(value + "\n");
    m_refreshing = true;
    m_editor->setPlainText(source);
    m_refreshing = false;
    commit();
    m_canvas->setFocus();
}
void InputController::command(int key)
{
    m_canvas->entryController()->focus(InputSurface::Score);
    m_voice = voiceNumber();
    if (key == Qt::Key_B)
        editHeader("B");
    else if (key == Qt::Key_Z)
        editHeader("Z");
    else if (key == Qt::Key_D)
        editHeader("D");
    else if (key == Qt::Key_P)
        editHeader("P");
    else if (key == Qt::Key_C)
        startLyrics();
    else if (key == Qt::Key_Q)
    {
        flush();
        auto parsed = FanqieFormat().parse(m_document->sourceText());
        if (!parsed.notes.isEmpty() || m_seenQ)
        {
            int maxVoice = 1;
            for (auto it = parsed.voiceNames.begin(); it != parsed.voiceNames.end(); ++it)
                maxVoice = std::max(maxVoice, it.key());
            m_voice = maxVoice + 1;
            auto source = m_editor->toPlainText() + QString("\nQ%1: ").arg(m_voice);
            m_refreshing = true;
            m_editor->setPlainText(source);
            m_refreshing = false;
            commit();
        }
        m_seenQ = true;
        auto voices = m_canvas->score().voices;
        auto ids = FanqieFormat().parse(m_document->sourceText()).voiceNames.keys();
        int index = ids.indexOf(m_voice);
        if (index >= 0 && index < voices.size())
            m_canvas->setActiveTrack(voices[index].track);
        m_canvas->selectAppendPosition(m_canvas->activeTrack());
        m_canvas->entryController()->selectTarget(true);
        m_canvas->setFocus();
    }
    m_canvas->setKeyboardInput(m_canvas->hasFocus());
}
bool InputController::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::FocusIn)
    {
        if (obj == m_editor)
        {
            m_canvas->entryController()->focus(InputSurface::Text);
            m_canvas->setKeyboardInput(false);
        }
        else if (obj == m_canvas)
        {
            flush();
            bool fromText = m_canvas->entryController()->surface() == InputSurface::Text;
            m_canvas->entryController()->focus(InputSurface::Score);
            if (m_canvas->selection().isEmpty())
            {
                m_canvas->selectAppendPosition(m_canvas->activeTrack());
                m_canvas->entryController()->selectTarget(true);
            }
            else if (fromText)
                m_canvas->entryController()->selectTarget(m_canvas->atAppendPosition());
            m_canvas->setKeyboardInput(true);
        }
    }
    if (obj == m_canvas && event->type() == QEvent::MouseButtonPress)
    {
        flush();
        m_document->sealInputGroup();
    }
    if ((obj == m_editor || obj == m_inline) && event->type() == QEvent::InputMethod)
    {
        auto *e = static_cast<QInputMethodEvent *>(event);
        m_composing = !e->preeditString().isEmpty();
        if (!e->commitString().isEmpty())
        {
            QTimer::singleShot(0, this,
                               [this]
                               {
                                   if (!m_composing)
                                       flush();
                               });
        }
    }
    if (event->type() != QEvent::KeyPress)
        return QObject::eventFilter(obj, event);
    auto *e = static_cast<QKeyEvent *>(event);
    if (obj == m_editor)
    {
        if (e->matches(QKeySequence::Paste))
            flush();
        if (e->key() >= Qt::Key_0 && e->key() <= Qt::Key_9 &&
            QRegularExpression("^\\s*Q\\d*\\s*[:：]")
                .match(m_editor->textCursor().block().text())
                .hasMatch())
            flush();
        if (e->matches(QKeySequence::Undo))
        {
            m_timer.stop();
            commit();
            m_document->undo();
            return true;
        }
        if (e->matches(QKeySequence::Redo))
        {
            m_document->redo();
            return true;
        }
        if (e->key() == Qt::Key_Escape && !m_composing)
        {
            flush();
            m_canvas->setFocus();
            return true;
        }
        return false;
    }
    if (obj == m_inline)
    {
        if (e->key() == Qt::Key_Escape && !m_composing)
        {
            flush();
            m_inline->hide();
            m_canvas->setFocus();
            return true;
        }
        return false;
    }
    if (e->key() == Qt::Key_Escape)
    {
        flush();
        m_document->sealInputGroup();
        m_canvas->setKeyboardInput(false);
        return false;
    }
    if (e->modifiers() & Qt::ControlModifier)
    {
        if (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)
        {
            flush();
            auto voices = m_canvas->score().voices;
            int index = 0;
            for (int i = 0; i < voices.size(); ++i)
                if (voices[i].track == m_canvas->activeTrack())
                    index = i;
            index =
                std::clamp(index + (e->key() == Qt::Key_Up ? -1 : 1), 0, int(voices.size()) - 1);
            if (index >= 0 && index < voices.size())
            {
                m_canvas->setActiveTrack(voices[index].track);
                m_voice = voiceNumber();
            }
            return true;
        }
        return false;
    }
    if (e->modifiers() & Qt::AltModifier)
        return false;
    if (e->key() == Qt::Key_N)
        return true;
    if (!e->text().isEmpty() && e->text()[0].isLetter() &&
        m_canvas->entryController()->wantsLiteralLetters())
    {
        appendMusic(e->text());
        return true;
    }
    if (e->key() == Qt::Key_B || e->key() == Qt::Key_Z || e->key() == Qt::Key_D ||
        e->key() == Qt::Key_P || e->key() == Qt::Key_Q || e->key() == Qt::Key_C)
    {
        command(e->key());
        return true;
    }
    if (e->text() == "\"")
    {
        appendMusic("\"\"");
        flush();
        m_dock->show();
        m_dock->raise();
        m_annotation = true;
        m_globalLyrics = false;
        m_inlineStart = m_editor->textCursor().position() - 1;
        m_inline->clear();
        m_inline->setPlaceholderText(tr("音符上方注释；Enter 完成"));
        m_inline->show();
        m_inline->setFocus();
        return true;
    }
    return false;
}
} // namespace jianpu
