// SPDX-License-Identifier: GPL-3.0-only
#include "airecognition.h"
#include "ai/aiclient.h"
#include "ai/recognitiontask.h"
#include "input/inputcontroller.h"
#include "mainwindow.h"
#include "scorecanvas.h"
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
namespace jianpu
{
void AiRecognitionUi::settings(QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("AI 接口设置"));
    dialog.resize(670, 330);
    auto c = AiConfig::load();
    QFormLayout layout(&dialog);
    QLineEdit endpoint(c.endpoint), model(c.model), key(c.key);
    key.setEchoMode(QLineEdit::Password);
    endpoint.setObjectName("aiEndpoint");
    model.setObjectName("aiModel");
    key.setObjectName("aiKey");
    QSpinBox checks;
    checks.setRange(0, 2);
    checks.setValue(c.checks);
    checks.setSuffix(QObject::tr(" 轮（1=看图复核，2=再按需修复）"));
    layout.addRow(QObject::tr("完整请求地址"), &endpoint);
    layout.addRow(QObject::tr("模型"), &model);
    layout.addRow(QObject::tr("API key"), &key);
    layout.addRow(QObject::tr("识别后检查"), &checks);
    QLabel notice(
        QObject::tr("谱图会发送到以上服务。key 仅保存在本机用户配置中，不写入工程或发行包。"));
    notice.setWordWrap(true);
    layout.addRow(&notice);
    QLabel status;
    status.setWordWrap(true);
    layout.addRow(&status);
    QPushButton test(QObject::tr("测试连接"));
    layout.addRow(&test);
    AiClient client(&dialog);
    auto current = [&]
    {
        auto result = c;
        result.endpoint = endpoint.text().trimmed();
        result.model = model.text().trimmed();
        result.key = key.text().trimmed();
        result.checks = checks.value();
        return result;
    };
    QObject::connect(&test, &QPushButton::clicked, &dialog,
                     [&]
                     {
                         test.setEnabled(false);
                         status.setText(QObject::tr("正在测试…"));
                         client.start(
                             current(),
                             {{"messages", QJsonArray{QJsonObject{{"role", "user"},
                                                                  {"content", "Reply only OK."}}}},
                              {"stream", true},
                              {"max_tokens", 128}});
                     });
    QObject::connect(&client, &AiClient::completed, &dialog,
                     [&](const QString &, const QJsonObject &)
                     {
                         status.setText(QObject::tr("连接与流式返回正常"));
                         test.setEnabled(true);
                     });
    QObject::connect(&client, &AiClient::failed, &dialog,
                     [&](const QString &why)
                     {
                         status.setText(why);
                         test.setEnabled(true);
                     });
    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout.addRow(&buttons);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog,
                     [&]
                     {
                         QString error;
                         if (current().save(&error))
                             dialog.accept();
                         else
                             status.setText(error);
                     });
    dialog.exec();
}
void AiRecognitionUi::chooseImages(QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("AI 图片识谱"));
    dialog.resize(820, 530);
    QVBoxLayout layout(&dialog);
    QLabel hint(QObject::tr("按曲谱页序添加图片。识别结果将在独立新窗口显示；原窗口保持不变。图片会"
                            "发送至已配置的 AI 服务。"));
    hint.setWordWrap(true);
    layout.addWidget(&hint);
    QHBoxLayout body;
    QListWidget list;
    list.setObjectName("recognitionImages");
    list.setDragDropMode(QAbstractItemView::InternalMove);
    list.setSelectionMode(QAbstractItemView::SingleSelection);
    body.addWidget(&list, 1);
    QLabel preview;
    preview.setAlignment(Qt::AlignCenter);
    preview.setMinimumSize(330, 330);
    body.addWidget(&preview, 1);
    layout.addLayout(&body, 1);
    QHBoxLayout controls;
    QPushButton add(QObject::tr("添加图片…")), up(QObject::tr("上移")), down(QObject::tr("下移")),
        remove(QObject::tr("移除")), settings(QObject::tr("接口设置…"));
    for (auto *b : {&add, &up, &down, &remove, &settings})
        controls.addWidget(b);
    layout.addLayout(&controls);
    auto append = [&]
    {
        auto files = QFileDialog::getOpenFileNames(&dialog, QObject::tr("选择简谱图片"), {},
                                                   QObject::tr("谱图 (*.png *.jpg *.jpeg *.webp)"));
        for (const auto &f : files)
        {
            auto *item = new QListWidgetItem(QFileInfo(f).fileName(), &list);
            item->setData(Qt::UserRole, f);
            item->setToolTip(f);
        }
        if (list.currentRow() < 0 && list.count())
            list.setCurrentRow(0);
    };
    QObject::connect(&add, &QPushButton::clicked, &dialog, append);
    QObject::connect(&list, &QListWidget::currentRowChanged, &dialog,
                     [&](int row)
                     {
                         if (row < 0)
                         {
                             preview.clear();
                             return;
                         }
                         QImageReader r(list.item(row)->data(Qt::UserRole).toString());
                         r.setAutoTransform(true);
                         auto size = r.size();
                         size.scale(400, 400, Qt::KeepAspectRatio);
                         r.setScaledSize(size);
                         preview.setPixmap(QPixmap::fromImage(r.read()).scaled(
                             400, 400, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                     });
    auto move = [&](int delta)
    {
        int row = list.currentRow(), target = row + delta;
        if (row >= 0 && target >= 0 && target < list.count())
        {
            auto *item = list.takeItem(row);
            list.insertItem(target, item);
            list.setCurrentRow(target);
        }
    };
    QObject::connect(&up, &QPushButton::clicked, &dialog, [&] { move(-1); });
    QObject::connect(&down, &QPushButton::clicked, &dialog, [&] { move(1); });
    QObject::connect(&remove, &QPushButton::clicked, &dialog,
                     [&] { delete list.takeItem(list.currentRow()); });
    QObject::connect(&settings, &QPushButton::clicked, &dialog,
                     [&] { AiRecognitionUi::settings(&dialog); });
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons.button(QDialogButtonBox::Ok)->setText(QObject::tr("开始识别"));
    layout.addWidget(&buttons);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog,
                     [&]
                     {
                         if (!list.count())
                             return;
                         auto c = AiConfig::load();
                         auto error = c.validate();
                         if (!error.isEmpty())
                         {
                             QMessageBox::information(&dialog, QObject::tr("接口设置"), error);
                             return;
                         }
                         QStringList paths;
                         for (int i = 0; i < list.count(); ++i)
                             paths << list.item(i)->data(Qt::UserRole).toString();
                         recognize(c, paths);
                         dialog.accept();
                     });
    dialog.exec();
}
MainWindow *AiRecognitionUi::recognize(const AiConfig &config, const QStringList &paths)
{
    auto *window = new MainWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(QObject::tr("AI 识谱 · 新曲谱"));
    auto *task = new RecognitionTask(window);
    task->setObjectName("recognitionTask");
    auto *dock = new QDockWidget(QObject::tr("AI 图片识谱"), window);
    dock->setObjectName("recognitionDock");
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    auto *panel = new QWidget(dock);
    auto *layout = new QVBoxLayout(panel);
    auto *status = new QLabel(QObject::tr("准备识别…"), panel);
    status->setObjectName("recognitionStatus");
    status->setWordWrap(true);
    auto *progress = new QProgressBar(panel);
    progress->setRange(0, 0);
    auto *cancel = new QPushButton(QObject::tr("取消识别"), panel);
    layout->addWidget(status);
    layout->addWidget(progress);
    layout->addWidget(cancel);
    dock->setWidget(panel);
    window->addDockWidget(Qt::BottomDockWidgetArea, dock);
    auto *input = window->inputController();
    input->beginExternalSource();
    QList<QAction *> enabledActions;
    for (auto *action : window->findChildren<QAction *>())
        if (action->isEnabled())
        {
            enabledActions << action;
            action->setEnabled(false);
        }
    window->menuBar()->setEnabled(false);
    window->canvas()->setEnabled(false);
    for (auto *d : window->findChildren<QDockWidget *>())
        if (d != dock && d->objectName() != "sourceDock")
            d->setEnabled(false);
    auto *timer = new QTimer(task);
    timer->setInterval(50);
    auto pending = std::make_shared<QString>();
    QObject::connect(task, &RecognitionTask::sourceChanged, task,
                     [pending](const QString &text) { *pending = text; });
    QObject::connect(timer, &QTimer::timeout, window,
                     [input, pending] { input->updateExternalSource(*pending); });
    timer->start();
    QObject::connect(task, &RecognitionTask::stageChanged, status,
                     [status](int stage, const QString &label)
                     { status->setText(QObject::tr("第 %1 阶段：%2").arg(stage + 1).arg(label)); });
    QObject::connect(cancel, &QPushButton::clicked, task, &RecognitionTask::cancel);
    QObject::connect(window, &MainWindow::closingRequested, task, &RecognitionTask::cancel);
    QObject::connect(task, &RecognitionTask::finished, window,
                     [=](const QString &source, bool success, const QString &message)
                     {
                         timer->stop();
                         bool committed = input->endExternalSource(source, success, message);
                         progress->setRange(0, 1);
                         progress->setValue(committed ? 1 : 0);
                         cancel->setEnabled(false);
                         status->setText(
                             committed ? message : QObject::tr("保留识别草稿：\n%1").arg(message));
                         for (auto *action : enabledActions)
                             action->setEnabled(true);
                         window->menuBar()->setEnabled(true);
                         window->canvas()->setEnabled(true);
                         for (auto *d : window->findChildren<QDockWidget *>())
                             d->setEnabled(true);
                         window->setWindowTitle(committed ? QObject::tr("AI 识谱完成 · 请核对原图")
                                                          : QObject::tr("AI 识谱草稿 · 待修正"));
                     });
    window->show();
    QTimer::singleShot(0, task, [task, config, paths] { task->start(config, paths); });
    return window;
}
} // namespace jianpu
