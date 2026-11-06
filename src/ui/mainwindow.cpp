// SPDX-License-Identifier: GPL-3.0-only
#include "mainwindow.h"
#include "airecognition.h"
#include "input/inputcontroller.h"
#include "layout/visualelement.h"
#include "layoutpanel.h"
#include "scorecanvas.h"
#include "sourceeditor.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace jianpu
{
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_document(this)
{
    setObjectName("jianpuMainWindow");
    m_canvas = new ScoreCanvas(&m_document);
    auto *scroll = new QScrollArea;
    scroll->setWidget(m_canvas);
    scroll->setAlignment(Qt::AlignHCenter);
    setCentralWidget(scroll);
    buildMenus();
    buildPanels();
    connect(&m_document, &ScoreDocument::changed, this, &MainWindow::refresh);
    connect(m_canvas, &ScoreCanvas::selectionChanged, this, &MainWindow::selectionChanged);
    connect(m_canvas, &ScoreCanvas::operationFailed, this, &MainWindow::showError);
    connect(m_canvas, &ScoreCanvas::editLyricsRequested, this, &MainWindow::editLyrics);
    connect(m_canvas, &ScoreCanvas::editMarkRequested, this, &MainWindow::editSelectedMark);
    connect(m_canvas, &ScoreCanvas::zoomChanged, this, [this](qreal z)
            { statusBar()->showMessage(tr("缩放 %1% · Ctrl+滚轮缩放").arg(qRound(z * 100))); });
    resize(1380, 940);
    if (!m_document.newScore())
        showError(m_document.error());
    m_input = new InputController(&m_document, m_canvas, this);
    connect(m_input, &InputController::error, this,
            [this](const QString &error)
            {
                statusBar()->showMessage(error, 12000);
                m_report->setPlainText(error);
            });
    auto *layoutDock = new QDockWidget(tr("排版"), this);
    layoutDock->setObjectName("layoutDock");
    layoutDock->setWidget(new LayoutPanel(&m_document));
    addDockWidget(Qt::RightDockWidgetArea, layoutDock);
    auto *action = new QAction(tr("排版设置"), this);
    action->setShortcut(QKeySequence("Ctrl+Shift+L"));
    addAction(action);
    connect(action, &QAction::triggered, this,
            [layoutDock]
            {
                layoutDock->show();
                layoutDock->raise();
                layoutDock->widget()->setFocus(Qt::ShortcutFocusReason);
            });
    m_viewMenu->addSeparator();
    for (auto *dock : findChildren<QDockWidget *>())
    {
        auto *toggle = dock->toggleViewAction();
        toggle->setObjectName("toggle_" + dock->objectName());
        m_viewMenu->addAction(toggle);
    }
    auto *voiceDock = findChild<QDockWidget *>("voicesDock");
    auto *paletteDock = findChild<QDockWidget *>("paletteDock");
    auto *sourceDock = findChild<QDockWidget *>("sourceDock");
    auto *propertiesDock = findChild<QDockWidget *>("propertiesDock");
    if (voiceDock && paletteDock)
        tabifyDockWidget(voiceDock, paletteDock);
    if (voiceDock && sourceDock)
    {
        tabifyDockWidget(voiceDock, sourceDock);
        sourceDock->raise();
    }
    if (propertiesDock)
    {
        tabifyDockWidget(propertiesDock, layoutDock);
        layoutDock->raise();
    }
    if (auto *report = findChild<QDockWidget *>("reportDock"))
        report->setMaximumHeight(130);
    QTimer::singleShot(0, this,
                       [this, sourceDock, layoutDock]
                       {
                           resizeDocks({sourceDock, layoutDock}, {350, 280}, Qt::Horizontal);
                           auto *area = qobject_cast<QScrollArea *>(centralWidget());
                           if (area && !m_document.viewSettings().contains("zoom") &&
                               m_canvas->layoutResult().size.width() > 0)
                               m_canvas->setZoom(
                                   std::clamp((area->viewport()->width() - 24) /
                                                  m_canvas->layoutResult().size.width(),
                                              0.35, 1.0));
                       });

    refresh();
}
void MainWindow::buildMenus()
{
    auto *file = menuBar()->addMenu(tr("文件(&F)"));
    auto *edit = menuBar()->addMenu(tr("编辑(&E)"));
    auto *view = menuBar()->addMenu(tr("视图(&V)"));
    m_viewMenu = view;
    view->setObjectName("viewMenu");
    auto *scoreMenu = menuBar()->addMenu(tr("曲谱(&S)"));
    auto *toolbar = addToolBar(tr("文件与编辑"));
    toolbar->setMovable(false);
    auto *newAction = file->addAction(
        tr("新建"), this,
        [this]
        {
            if (confirmDiscard())
                m_document.newScore();
        },
        QKeySequence::New);
    auto *openAction = file->addAction(
        tr("打开…"), this,
        [this]
        {
            QString path = QFileDialog::getOpenFileName(
                this, tr("打开曲谱"), {},
                tr("曲谱 (*.jianpu *.musicxml *.xml *.mxl *.mid *.midi *.txt *.jptxt)"));
            if (!path.isEmpty())
                openPath(path);
        },
        QKeySequence::Open);
    file->addAction(tr("AI 图片识谱…"), this, [this] { AiRecognitionUi::chooseImages(this); })
        ->setObjectName("aiRecognize");
    file->addAction(tr("AI 接口设置…"), this, [this] { AiRecognitionUi::settings(this); })
        ->setObjectName("aiSettings");
    auto *saveAction = file->addAction(tr("保存"), this, [this] { save(); }, QKeySequence::Save);
    file->addAction(tr("另存为…"), this, [this] { save(true); }, QKeySequence::SaveAs);
    auto *exportMenu = file->addMenu(tr("导出"));
    for (const auto &format : {"musicxml", "mxl", "mid"})
        exportMenu->addAction(QString::fromLatin1(format).toUpper(), this,
                              [this, format] { exportFormat(format); });
    file->addAction(tr("导出文字稿…"), this,
                    [this]
                    {
                        if (m_input && !m_input->flush())
                            return;
                        auto path = QFileDialog::getSaveFileName(
                            this, tr("导出文字稿"), {}, tr("UTF-8 文字稿 (*.jptxt *.txt)"));
                        if (path.isEmpty())
                            return;
                        QSaveFile file(path);
                        auto bytes = m_document.sourceText().toUtf8();
                        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
                            !file.commit())
                            showError(tr("文字稿保存失败"));
                    });
    file->addSeparator();
    file->addAction(tr("退出"), this, &QWidget::close, QKeySequence::Quit);
    m_undo = edit->addAction(
        tr("撤销"), this,
        [this]
        {
            if (m_input)
                m_input->flush();
            m_document.undo();
        },
        QKeySequence::Undo);
    m_redo = edit->addAction(
        tr("重做"), this,
        [this]
        {
            if (m_input)
                m_input->flush();
            m_document.redo();
        },
        QKeySequence::Redo);
    edit->addSeparator();
    edit->addAction(tr("复制"), m_canvas, &ScoreCanvas::copy, QKeySequence::Copy);
    edit->addAction(tr("粘贴"), m_canvas, &ScoreCanvas::paste, QKeySequence::Paste);
    edit->addAction(
        tr("删除"), this, [this] { m_canvas->deleteSelection(); }, QKeySequence::Delete);
    auto *insertPaste = edit->addAction(tr("插入粘贴"), QKeySequence("Ctrl+Shift+V"), m_canvas,
                                        &ScoreCanvas::pasteInsert);
    insertPaste->setShortcutContext(Qt::WidgetShortcut);
    m_canvas->addAction(insertPaste);
    edit->addAction(tr("编辑歌词…"), this, &MainWindow::editLyrics, QKeySequence("Ctrl+L"));
    toolbar->addActions({newAction, openAction, saveAction});
    toolbar->addSeparator();
    toolbar->addActions({m_undo, m_redo});
    toolbar->addSeparator();
    auto *entryMode = new QComboBox;
    entryMode->setObjectName("entryMode");
    entryMode->addItem(tr("末尾追加"), int(EntryMode::Append));
    entryMode->addItem(tr("修改／覆盖"), int(EntryMode::Overwrite));
    entryMode->addItem(tr("插入"), int(EntryMode::Insert));
    entryMode->setToolTip(tr("直接输入在选中内容后插入；插入粘贴在所选起点前插入"));
    toolbar->addWidget(entryMode);
    connect(entryMode, &QComboBox::activated, this,
            [this, entryMode](int)
            {
                if (m_input && !m_input->flush())
                    return;
                auto mode = EntryMode(entryMode->currentData().toInt());
                if (mode == EntryMode::Append)
                    m_canvas->selectEnd();
                m_canvas->entryController()->setMode(mode);
                m_canvas->setFocus();
            });
    connect(m_canvas->entryController(), &ScoreEntryController::modeChanged, entryMode,
            [entryMode](EntryMode mode)
            { entryMode->setCurrentIndex(entryMode->findData(int(mode))); });
    m_duration = new QComboBox;
    for (int value : {1, 2, 4, 8, 16, 32, 64, 128})
        m_duration->addItem(tr("1/%1 音符").arg(value), value);
    m_duration->setCurrentIndex(2);
    toolbar->addWidget(m_duration);
    m_dots = new QSpinBox;
    m_dots->setRange(0, 2);
    m_dots->setPrefix(tr("附点 "));
    toolbar->addWidget(m_dots);
    auto updateDuration = [this]
    { m_canvas->setInputDuration(m_duration->currentData().toInt(), m_dots->value()); };
    connect(m_duration, &QComboBox::currentIndexChanged, this, updateDuration);
    connect(m_dots, &QSpinBox::valueChanged, this, updateDuration);
    toolbar->addAction(tr("应用时值"), this,
                       [this]
                       {
                           m_canvas->apply({EditRequest::Kind::Duration,
                                            {},
                                            m_duration->currentData().toInt(),
                                            m_dots->value()});
                       });
    for (const auto &entry :
         QList<QPair<QString, EditRequest::Kind>>{{"♯", EditRequest::Kind::Sharp},
                                                  {"♭", EditRequest::Kind::Flat},
                                                  {"♮", EditRequest::Kind::Natural}})
        toolbar->addAction(entry.first, this, [this, entry] { m_canvas->apply({entry.second}); });
    view->addAction(
        tr("放大"), this, [this] { m_canvas->setZoom(m_canvas->zoom() * 1.2); },
        QKeySequence::ZoomIn);
    view->addAction(
        tr("缩小"), this, [this] { m_canvas->setZoom(m_canvas->zoom() / 1.2); },
        QKeySequence::ZoomOut);
    view->addAction(tr("100%"), this, [this] { m_canvas->setZoom(1); });
    view->addAction(tr("适合宽度"), this,
                    [this] { m_canvas->setZoom((centralWidget()->width() - 35) / 842.0); });
    scoreMenu->addAction(tr("添加小节"), this,
                         [this] { m_canvas->apply({EditRequest::Kind::AppendMeasure}); });
    scoreMenu->addAction(tr("删除所选小节"), this,
                         [this] { m_canvas->apply({EditRequest::Kind::DeleteMeasure}); });
    scoreMenu->addAction(tr("添加声部"), this,
                         [this] { m_canvas->apply({EditRequest::Kind::AddVoice}); });
    scoreMenu->addAction(
        tr("添加乐器…"), this,
        [this]
        {
            QStringList names{tr("钢琴"), tr("小提琴"), tr("长笛"), tr("单簧管"), tr("大提琴")};
            QStringList templates{"piano", "violin", "flute", "clarinet", "violoncello"};
            bool ok;
            auto name =
                QInputDialog::getItem(this, tr("添加乐器"), tr("乐器"), names, 0, false, &ok);
            if (ok)
                m_canvas->apply(
                    {EditRequest::Kind::AddPart, {}, 0, 0, templates[names.indexOf(name)]});
        });
    scoreMenu->addAction(tr("添加和弦音…"), this,
                         [this]
                         {
                             bool ok;
                             int pitch = QInputDialog::getInt(this, tr("添加和弦音"),
                                                              tr("MIDI 音高（中央 C = 60）"), 64, 0,
                                                              127, 1, &ok);
                             if (ok)
                                 m_canvas->apply({EditRequest::Kind::AddChordNote, {}, pitch});
                         });
    scoreMenu->addAction(
        tr("修改调号…"), this,
        [this]
        {
            QDialog dialog(this);
            dialog.setWindowTitle(tr("调号"));
            QFormLayout form(&dialog);
            QComboBox key, mode;
            for (int i = -7; i <= 7; ++i)
                key.addItem(JianpuLayout::keyLabel(i, false), i);
            key.setCurrentIndex(m_canvas->score().keyFifths + 7);
            mode.addItems({tr("大调（1为主音）"), tr("小调（6为主音）")});
            mode.setCurrentIndex(m_canvas->score().minor ? 1 : 0);
            form.addRow(tr("调号"), &key);
            form.addRow(tr("调式"), &mode);
            QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            form.addRow(&buttons);
            connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() == QDialog::Accepted)
                m_canvas->apply({EditRequest::Kind::SetKey,
                                 {},
                                 key.currentData().toInt(),
                                 mode.currentIndex()});
        });
    scoreMenu->addAction(
        tr("修改拍号…"), this,
        [this]
        {
            bool ok;
            auto text = QInputDialog::getText(this, tr("拍号"), tr("从所选小节开始，例如 3/4"),
                                              QLineEdit::Normal, "4/4", &ok);
            auto pieces = text.split('/');
            if (ok && pieces.size() == 2)
                m_canvas->apply(
                    {EditRequest::Kind::SetTime, {}, pieces[0].toInt(), pieces[1].toInt()});
        });
    auto *help = menuBar()->addMenu(tr("帮助"));
    help->addAction(
        tr("快捷键"), this,
        [this]
        {
            QMessageBox::information(
                this, tr("简谱编辑"),
                tr("点击音符或歌词修改，点击末尾空格连续追加；Insert 切换插入／修改。\n"
                   "从未选中内容开始拖动划选，Shift 点击连选，Ctrl 点击多选。\n"
                   "点击小节内空白选整节；Ctrl 多节选取，Shift 连选小节。\n"
                   "拖动已选内容搬移，Ctrl 拖动复制；蓝色落点表示在目标前插入。\n"
                   "Ctrl+C 复制；Ctrl+V 按当前模式覆盖／插入；Ctrl+Shift+V 强制插入。\n"
                   "1–7 修改音高，0 "
                   "休止符，8 隐藏休止符，9 节奏 X；End 在末尾追加。\n← → 选择前后音符，↑ ↓ "
                   "在音符、各段歌词与文字之间导航；Alt+↑↓ 改音高，Ctrl+↑↓ 切换声部。\n"
                   "点击文字稿或谱面切换编辑。B/Z/D/P "
                   "编辑题目、词曲、调号、拍号；Q 新声部，C 新歌词；/ 减时，- 增时，' 和 , "
                   "改八度。\nL 编辑所选歌词，Ctrl+Z/Y 撤销重做。\n"
                   "标题与记号拖动吸附；圆柄旋转、方柄缩放，Alt 拖动取消吸附；Alt+方向键微调。\n"
                   "跨音符记号需选择同一声部的起止音符。"));
        });
    help->addAction(tr("关于"), this,
                    [this]
                    {
                        QMessageBox::about(
                            this, tr("简谱实验室"),
                            tr("简谱实验室 %1\nC++20 / Qt 6.10.2\n使用 MuseScore GPLv3 "
                               "乐谱内核。\n版权与第三方许可见发行目录 licenses。")
                                .arg(JIANPU_APP_VERSION));
                    });
}
void MainWindow::buildPanels()
{
    auto *voicesDock = new QDockWidget(tr("声部"), this);
    voicesDock->setObjectName("voicesDock");
    m_voices = new QListWidget;
    m_voices->setMinimumWidth(185);
    voicesDock->setWidget(m_voices);
    addDockWidget(Qt::LeftDockWidgetArea, voicesDock);
    m_voices->setDragDropMode(QAbstractItemView::InternalMove);
    connect(
        m_voices->model(), &QAbstractItemModel::rowsMoved, this,
        [this]
        {
            if (m_refreshing)
                return;
            QJsonArray order;
            for (int i = 0; i < m_voices->count(); ++i)
                order.append(m_voices->item(i)->data(Qt::UserRole).toInt());
            auto settings = m_document.viewSettings();
            settings["voiceOrder"] = order;
            m_document.setViewSettings(settings);
        },
        Qt::QueuedConnection);
    connect(m_voices, &QListWidget::currentRowChanged, this,
            [this](int row)
            {
                if (!m_refreshing && row >= 0)
                    m_canvas->setActiveTrack(m_voices->item(row)->data(Qt::UserRole).toInt());
            });
    connect(m_voices, &QListWidget::itemChanged, this,
            [this]
            {
                if (m_refreshing)
                    return;
                QJsonArray hidden;
                for (int i = 0; i < m_voices->count(); ++i)
                    if (m_voices->item(i)->checkState() != Qt::Checked)
                        hidden.append(m_voices->item(i)->data(Qt::UserRole).toInt());
                auto view = m_document.viewSettings();
                view["hiddenTracks"] = hidden;
                m_document.setViewSettings(view);
            });
    auto *paletteDock = new QDockWidget(tr("记号"), this);
    paletteDock->setObjectName("paletteDock");
    m_marks = new QListWidget;
    paletteDock->setWidget(m_marks);
    addDockWidget(Qt::LeftDockWidgetArea, paletteDock);
    const QList<QPair<QString, QString>> palette{{tr("延音线"), "tie"},
                                                 {tr("连奏线"), "slur"},
                                                 {tr("三连音"), "tuplet"},
                                                 {tr("倚音"), "grace"},
                                                 {tr("延长记号"), "fermata"},
                                                 {tr("渐强 ＜"), "crescendo"},
                                                 {tr("渐弱 ＞"), "diminuendo"},
                                                 {tr("速度…"), "tempo"},
                                                 {"pp", "dynamic:pp"},
                                                 {"p", "dynamic:p"},
                                                 {"mp", "dynamic:mp"},
                                                 {"mf", "dynamic:mf"},
                                                 {"f", "dynamic:f"},
                                                 {"ff", "dynamic:ff"},
                                                 {"sfz", "dynamic:sfz"},
                                                 {tr("反复开始"), "repeat-start"},
                                                 {tr("反复结束"), "repeat-end"},
                                                 {tr("反复房子…"), "volta"},
                                                 {"D.C.", "jump:DC"},
                                                 {"D.S.", "jump:DS"},
                                                 {"D.C. al Fine", "jump:DC_FINE"},
                                                 {"D.S. al Fine", "jump:DS_FINE"},
                                                 {"D.S. al Coda", "jump:DS_CODA"},
                                                 {"Segno", "marker:segno"},
                                                 {"Coda", "marker:coda"},
                                                 {"Fine", "marker:fine"},
                                                 {"To Coda", "marker:tocoda"},
                                                 {tr("断奏 ·"), "articulation:articStaccatoAbove"},
                                                 {tr("保持音 —"), "articulation:articTenutoAbove"},
                                                 {tr("重音 ＞"), "articulation:articAccentAbove"},
                                                 {tr("颤音 tr"), "articulation:ornamentTrill"},
                                                 {tr("波音"), "articulation:ornamentMordent"},
                                                 {tr("回音"), "articulation:ornamentTurn"}};
    for (const auto &[label, kind] : palette)
    {
        auto *item = new QListWidgetItem(label, m_marks);
        item->setData(Qt::UserRole, kind);
    }
    connect(m_marks, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) { editMark(item->data(Qt::UserRole).toString()); });
    auto *propertiesDock = new QDockWidget(tr("属性"), this);
    propertiesDock->setObjectName("propertiesDock");
    auto *panel = new QWidget;
    auto *form = new QFormLayout(panel);
    m_selectionInfo = new QLabel(tr("未选择"));
    m_selectionInfo->setWordWrap(true);
    form->addRow(m_selectionInfo);
    m_rotation = new QDoubleSpinBox;
    m_rotation->setObjectName("elementRotation");
    m_rotation->setRange(-360, 360);
    m_rotation->setSuffix(tr(" °"));
    m_rotation->setSingleStep(1);
    m_elementScale = new QDoubleSpinBox;
    m_elementScale->setObjectName("elementScale");
    m_elementScale->setRange(25, 400);
    m_elementScale->setSuffix(tr(" %"));
    m_elementScale->setValue(100);
    form->addRow(tr("旋转"), m_rotation);
    form->addRow(tr("缩放"), m_elementScale);
    auto transform = [this]
    {
        if (m_refreshing || m_canvas->selection().size() != 1)
            return;
        m_document.transformElement(m_canvas->selection().first(), m_rotation->value(),
                                    m_elementScale->value() / 100, "element-transform");
    };
    connect(m_rotation, &QDoubleSpinBox::valueChanged, this, [transform](double) { transform(); });
    connect(m_elementScale, &QDoubleSpinBox::valueChanged, this,
            [transform](double) { transform(); });
    auto *resetTransform = new QPushButton(tr("恢复元素位置与变换"));
    form->addRow(resetTransform);
    connect(resetTransform, &QPushButton::clicked, this,
            [this]
            {
                if (m_canvas->selection().size() != 1)
                    return;
                auto element =
                    m_canvas->layoutResult().elements.value(m_canvas->selection().first());
                if (!element || !element->identity().movable)
                    return;
                auto view = m_document.viewSettings();
                auto offsets = view["elementOffsets"].toObject();
                offsets.remove(element->identity().key);
                view["elementOffsets"] = offsets;
                m_document.setViewSettings(view);
            });
    m_pendingLyrics = new QLabel;
    m_pendingLyrics->setObjectName("pendingLyrics");
    m_pendingLyrics->setWordWrap(true);
    form->addRow(tr("待分配歌词"), m_pendingLyrics);
    auto *clearPending = new QPushButton(tr("移除待分配歌词（可撤销）"));
    form->addRow(clearPending);
    connect(clearPending, &QPushButton::clicked, this,
            [this] { m_document.discardPendingLyrics(); });
    m_verse = new QSpinBox;
    m_verse->setRange(1, 1024);
    form->addRow(tr("歌词段次"), m_verse);
    m_lyrics = new QLineEdit;
    m_lyrics->setObjectName("lyricsEditor");
    m_lyrics->setPlaceholderText(tr("中文歌词"));
    form->addRow(tr("歌词"), m_lyrics);
    auto *applyLyrics = new QPushButton(tr("应用歌词"));
    form->addRow(applyLyrics);
    auto apply = [this]
    {
        m_canvas->apply({EditRequest::Kind::Lyrics, {}, m_verse->value() - 1, 0, m_lyrics->text()});
        m_canvas->setFocus();
    };
    connect(applyLyrics, &QPushButton::clicked, this, apply);
    connect(m_lyrics, &QLineEdit::returnPressed, this, apply);
    connect(m_verse, &QSpinBox::valueChanged, this, &MainWindow::selectionChanged);
    auto *removeMark = new QPushButton(tr("删除所选记号"));
    form->addRow(removeMark);
    auto *editMarkButton = new QPushButton(tr("编辑所选记号…"));
    form->addRow(editMarkButton);
    connect(editMarkButton, &QPushButton::clicked, this, &MainWindow::editSelectedMark);
    connect(removeMark, &QPushButton::clicked, this,
            [this] { m_canvas->apply({EditRequest::Kind::RemoveMark}); });
    propertiesDock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock);
    auto *reportDock = new QDockWidget(tr("导入与显示报告"), this);
    reportDock->setObjectName("reportDock");
    m_report = new QPlainTextEdit;
    m_report->setReadOnly(true);
    m_report->setMaximumHeight(150);
    reportDock->setWidget(m_report);
    addDockWidget(Qt::BottomDockWidgetArea, reportDock);
}
void MainWindow::refresh()
{
    m_refreshing = true;
    setWindowTitle(tr("%1%2 — 简谱实验室 %3")
                       .arg(m_document.modified() ? "*" : "",
                            m_document.filePath().isEmpty()
                                ? m_canvas->score().title
                                : QFileInfo(m_document.filePath()).fileName(),
                            JIANPU_APP_VERSION));
    m_undo->setEnabled(m_document.canUndo());
    m_redo->setEnabled(m_document.canRedo());
    m_voices->clear();
    auto hidden = m_document.viewSettings().value("hiddenTracks").toArray();
    auto orderedVoices = m_canvas->score().voices;
    auto order = m_document.viewSettings().value("voiceOrder").toArray();
    std::stable_sort(orderedVoices.begin(), orderedVoices.end(),
                     [&](const auto &a, const auto &b)
                     {
                         auto rank = [&](int track)
                         {
                             for (int i = 0; i < order.size(); ++i)
                                 if (order[i].toInt() == track)
                                     return i;
                             return 100000 + track;
                         };
                         return rank(a.track) < rank(b.track);
                     });
    for (const auto &voice : orderedVoices)
    {
        auto *item = new QListWidgetItem(
            voice.name + (voice.percussion ? tr("（打击乐，仅保留）") : QString()), m_voices);
        item->setData(Qt::UserRole, voice.track);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(hidden.contains(voice.track) || voice.percussion ? Qt::Unchecked
                                                                             : Qt::Checked);
        if (voice.track == m_canvas->activeTrack())
            m_voices->setCurrentItem(item);
    }
    QStringList pending;
    for (auto value : m_document.inputState()["pendingLyrics"].toArray())
        pending << value.toString();
    m_pendingLyrics->setText(pending.isEmpty() ? tr("无") : pending.join(" "));
    auto reports =
        m_canvas->score().warnings + m_document.inputMessages() + m_canvas->layoutResult().warnings;
    m_report->setPlainText(reports.isEmpty() ? tr("曲谱已载入，常用记号已显示，未发现导入警告。")
                                             : reports.join('\n'));
    statusBar()->showMessage(tr("%1 小节 · %2 声部 · %3 音符")
                                 .arg(m_canvas->score().measures.size())
                                 .arg(m_canvas->score().voices.size())
                                 .arg(m_canvas->score().noteCount()));
    m_refreshing = false;
    selectionChanged();
}
void MainWindow::selectionChanged()
{
    auto ids = m_canvas->modelSelection();
    auto content = m_canvas->contentSelection();
    if (content.kind == SelectionKind::Measures)
    {
        m_rotation->setEnabled(false);
        m_elementScale->setEnabled(false);
        m_lyrics->clear();
        m_selectionInfo->setText(tr("已选择 %1 个小节（全部声部）\nCtrl+C 复制；Ctrl+V "
                                    "覆盖；Ctrl+Shift+V 插入；拖动搬移")
                                     .arg(content.measures.size()));
        return;
    }
    auto movable = m_canvas->selection().size() == 1
                       ? m_canvas->layoutResult().elements.value(m_canvas->selection().first())
                       : nullptr;
    bool enabled = movable && movable->identity().movable;
    m_rotation->setEnabled(enabled);
    m_elementScale->setEnabled(enabled);
    if (enabled)
    {
        auto value = m_document.viewSettings()["elementOffsets"]
                         .toObject()[movable->identity().key]
                         .toObject();
        QSignalBlocker r(m_rotation), s(m_elementScale);
        m_rotation->setValue(value["rotation"].toDouble());
        m_elementScale->setValue(value["scale"].toDouble(1) * 100);
    }
    if (auto *slot = m_canvas->selectedLyric())
    {
        QSignalBlocker blocker(m_verse);
        m_verse->setValue(slot->verse + 1);
        m_lyrics->setText(slot->text);
        m_selectionInfo->setText(
            tr("第 %1 段歌词 · 已选 %2 个词位\n点击填词，Insert 插入；方向键导航")
                .arg(slot->verse + 1)
                .arg(m_canvas->selection().size()));
        return;
    }
    m_selectionInfo->setText(ids.isEmpty() ? tr("未选择音符")
                                           : tr("已选择 %1 个元素").arg(ids.size()));
    if (m_canvas->selection().size() == 1)
    {
        auto element = m_canvas->layoutResult().elements.value(m_canvas->selection().first());
        if (element && element->identity().movable)
            m_selectionInfo->setText(
                element->identity().label +
                tr("\n拖动吸附；Alt+方向键 1 px，Ctrl+Alt 精调；圆柄旋转，方柄缩放"));
    }
    m_lyrics->clear();
    if (ids.isEmpty())
        return;
    for (const auto &mark : m_canvas->score().marks)
        if (ids.contains(mark.id))
        {
            const QMap<QString, QString> names{
                {"dynamic", tr("力度")},          {"tempo", tr("速度")},
                {"volta", tr("反复房子")},        {"hairpin", tr("渐变力度")},
                {"slur", tr("连奏线")},           {"tie", tr("延音线")},
                {"articulation", tr("演奏记号")}, {"fermata", tr("延长记号")},
                {"marker", tr("定位记号")},       {"jump", tr("跳转")},
                {"tuplet", tr("连音组")},         {"key", tr("调号")},
                {"minor-key", tr("调号")}};
            auto text = (mark.kind == "key" || mark.kind == "minor-key")
                            ? JianpuLayout::keyLabel(mark.text.toInt(), mark.kind == "minor-key")
                            : mark.text;
            m_selectionInfo->setText(tr("%1\n%2").arg(names.value(mark.kind, tr("记号")), text));
            return;
        }
    for (const auto &m : m_canvas->score().measures)
        for (const auto &e : m.events)
        {
            bool selected = ids.contains(e.id);
            for (const auto &n : e.notes)
                if (ids.contains(n.id))
                    selected = true;
            if (!selected)
                continue;
            auto duration = e.denominator == 0 ? tr("整小节")
                                               : tr("1/%1，附点 %2").arg(e.denominator).arg(e.dots);
            m_selectionInfo->setText(tr("小节 %1 · 声部 %2\n时值：%3\n%4 个音")
                                         .arg(m.number)
                                         .arg(e.track % 4 + 1)
                                         .arg(duration)
                                         .arg(e.notes.size()));
            for (const auto &lyric : e.lyrics)
                if (lyric.verse == m_verse->value() - 1)
                    m_lyrics->setText(lyric.text);
            return;
        }
}
void MainWindow::editLyrics()
{
    auto *dock = findChild<QDockWidget *>("propertiesDock");
    dock->show();
    dock->raise();
    m_lyrics->setFocus();
    m_lyrics->selectAll();
}
void MainWindow::editSelectedMark()
{
    for (const auto &mark : m_canvas->score().marks)
        if (m_canvas->selection().contains(mark.id))
        {
            bool ok = false;
            EditRequest request{EditRequest::Kind::UpdateMark, {mark.id}};
            if (mark.kind == "dynamic")
                request.text = QInputDialog::getItem(
                    this, tr("力度"), tr("力度"),
                    {"ppp", "pp", "p", "mp", "mf", "f", "ff", "fff", "sfz"}, 4, false, &ok);
            else if (mark.kind == "tempo" || mark.kind == "volta")
                request.value = QInputDialog::getInt(
                    this, tr("编辑记号"), mark.kind == "tempo" ? tr("BPM") : tr("演奏遍次"),
                    mark.kind == "tempo" ? 120 : 1, mark.kind == "tempo" ? 10 : 1,
                    mark.kind == "tempo" ? 400 : 16, 1, &ok);
            else if (mark.kind == "hairpin")
            {
                auto text = QInputDialog::getItem(this, tr("渐变力度"), tr("类型"),
                                                  {tr("渐强"), tr("渐弱")},
                                                  mark.text == ">" ? 1 : 0, false, &ok);
                request.value = text == tr("渐弱") ? 1 : 0;
            }
            else
            {
                statusBar()->showMessage(tr("此记号可删除后重新选择起止音符添加"), 5000);
                return;
            }
            if (ok)
                m_canvas->apply(request);
            return;
        }
    statusBar()->showMessage(tr("请先在谱面上选择记号"), 5000);
}
void MainWindow::editMark(const QString &kind)
{
    int value = 0;
    if (kind == "tempo" || kind == "volta")
    {
        bool ok;
        value = QInputDialog::getInt(this, kind == "tempo" ? tr("速度") : tr("房子编号"),
                                     kind == "tempo" ? tr("每分钟四分音符数") : tr("演奏遍次"),
                                     kind == "tempo" ? 120 : 1, kind == "tempo" ? 10 : 1,
                                     kind == "tempo" ? 400 : 16, 1, &ok);
        if (!ok)
            return;
    }
    m_canvas->apply({kind == "grace" ? EditRequest::Kind::Grace : EditRequest::Kind::AddMark,
                     {},
                     value,
                     0,
                     kind});
    m_canvas->setFocus();
}
void MainWindow::showError(const QString &error)
{
    QMessageBox::warning(this, tr("操作未完成"), error);
}
bool MainWindow::confirmDiscard()
{
    if (m_input && !m_input->flush())
        return false;
    if (!m_document.modified())
        return true;
    auto answer = QMessageBox::question(
        this, tr("未保存的修改"), tr("保存当前曲谱的修改？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    return answer == QMessageBox::Discard || save();
}
bool MainWindow::save(bool saveAs)
{
    if (m_input && !m_input->flush())
        return false;
    QString path = m_document.filePath();
    if (saveAs || path.isEmpty())
        path =
            QFileDialog::getSaveFileName(this, tr("保存简谱工程"), path, tr("简谱工程 (*.jianpu)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".jianpu", Qt::CaseInsensitive))
        path += ".jianpu";
    auto view = m_document.viewSettings();
    view["zoom"] = m_canvas->zoom();
    m_document.setViewSettings(view);
    if (!m_document.save(path))
    {
        showError(m_document.error());
        return false;
    }
    return true;
}
bool MainWindow::openPath(const QString &path, bool interactive)
{
    if (interactive && !confirmDiscard())
        return false;
    ImportOptions options;
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (interactive && (suffix == "mid" || suffix == "midi"))
    {
        QString error;
        auto tracks = ScoreDocument::midiTracks(path, &error);
        if (tracks.isEmpty())
        {
            showError(error.isEmpty() ? tr("文件中没有音符轨道") : error);
            return false;
        }
        QDialog dialog(this);
        dialog.setWindowTitle(tr("MIDI 导入设置"));
        QVBoxLayout layout(&dialog);
        QListWidget list;
        for (const auto &track : tracks)
        {
            auto *item = new QListWidgetItem(
                track.name + (track.percussion ? tr("（打击乐，仅保留）") : QString()), &list);
            item->setData(Qt::UserRole, track.index);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
        }
        layout.addWidget(new QLabel(tr("选择需要导入的轨道")));
        layout.addWidget(&list);
        QComboBox quant;
        for (int value : {16, 32, 64})
            quant.addItem(tr("最短量化：1/%1").arg(value), value);
        layout.addWidget(&quant);
        QCheckBox tuplets(tr("识别连音组（含三连音）"));
        tuplets.setChecked(true);
        layout.addWidget(&tuplets);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout.addWidget(&buttons);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return false;
        for (int i = 0; i < list.count(); ++i)
            if (list.item(i)->checkState() == Qt::Checked)
                options.tracks << list.item(i)->data(Qt::UserRole).toInt();
        if (options.tracks.isEmpty())
        {
            showError(tr("至少选择一个轨道"));
            return false;
        }
        options.quantization = quant.currentData().toInt();
        options.tuplets = tuplets.isChecked();
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = m_document.open(path, options);
    QApplication::restoreOverrideCursor();
    if (!ok && interactive)
        showError(m_document.error());
    if (ok)
    {
        auto view = m_document.viewSettings();
        if (view.contains("zoom"))
            m_canvas->setZoom(view["zoom"].toDouble(1));
        m_canvas->setFocus();
    }
    return ok;
}
void MainWindow::exportFormat(const QString &extension)
{
    if (m_input && !m_input->flush())
        return;
    auto path =
        QFileDialog::getSaveFileName(this, tr("导出曲谱"), {}, tr("曲谱 (*.%1)").arg(extension));
    if (path.isEmpty())
        return;
    if (!path.endsWith('.' + extension, Qt::CaseInsensitive))
        path += '.' + extension;
    if (!m_document.exportFile(path))
        showError(m_document.error());
}
void MainWindow::closeEvent(QCloseEvent *event)
{
    emit closingRequested();
    if (!m_confirmClose || confirmDiscard())
        event->accept();
    else
        event->ignore();
}
} // namespace jianpu
