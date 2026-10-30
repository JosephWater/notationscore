// SPDX-License-Identifier: GPL-3.0-only
#include "layoutpanel.h"
#include "core/scoredocument.h"
#include "layout/layoutsettings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>
namespace jianpu
{
LayoutPanel::LayoutPanel(ScoreDocument *d, QWidget *parent) : QWidget(parent), m_document(d)
{
    setObjectName("layoutPanel");
    auto *root = new QVBoxLayout(this);
    m_presets = new QComboBox;
    m_presets->setObjectName("layoutPreset");
    setFocusProxy(m_presets);
    m_presets->addItem(tr("经典出版"), "classic");
    m_presets->addItem(tr("现代清爽"), "modern");
    m_presets->addItem(tr("儿童教材"), "children");
    QSettings saved;
    saved.beginGroup("layoutPresets");
    for (const auto &name : saved.childKeys())
        m_presets->addItem(name, "custom:" + name);
    root->addWidget(m_presets);
    auto *tabs = new QTabWidget;
    root->addWidget(tabs);
    auto tab = [&](const QString &name)
    {
        auto *panel = new QWidget;
        auto *form = new QFormLayout(panel);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setWidget(panel);
        tabs->addTab(scroll, name);
        return form;
    };
    auto *page = tab(tr("页面"));
    auto *type = tab(tr("字体"));
    auto *spacing = tab(tr("间距"));
    auto *flow = tab(tr("换行"));
    auto number = [&](QFormLayout *form, const char *key, const QString &label, double low,
                      double high, double step = 1)
    {
        auto *box = new QDoubleSpinBox;
        box->setObjectName(key);
        box->setRange(low, high);
        box->setSingleStep(step);
        box->setDecimals(step < 1 ? 2 : 1);
        form->addRow(label, box);
        m_fields[key] = box;
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { apply(); });
    };
    auto check = [&](QFormLayout *form, const char *key, const QString &label)
    {
        auto *box = new QCheckBox(label);
        box->setObjectName(key);
        form->addRow(box);
        m_fields[key] = box;
        connect(box, &QCheckBox::toggled, this, [this] { apply(); });
    };
    auto combo = [&](QFormLayout *form, const char *key, const QString &label,
                     const QList<QPair<QString, QString>> &values)
    {
        auto *box = new QComboBox;
        box->setObjectName(key);
        for (auto pair : values)
            box->addItem(pair.first, pair.second);
        form->addRow(label, box);
        m_fields[key] = box;
        connect(box, &QComboBox::currentIndexChanged, this, [this] { apply(); });
    };
    combo(page, "paper", tr("纸张"),
          {{"A4", "A4"}, {"A5", "A5"}, {"Letter", "Letter"}, {tr("自定义"), "custom"}});
    check(page, "landscape", tr("横向"));
    number(page, "widthMm", tr("宽度 mm"), 100, 1000);
    number(page, "heightMm", tr("高度 mm"), 100, 1000);
    for (auto p : QList<QPair<QString, QString>>{{"leftMm", tr("左边距 mm")},
                                                 {"rightMm", tr("右边距 mm")},
                                                 {"topMm", tr("上边距 mm")},
                                                 {"bottomMm", tr("下边距 mm")}})
        number(page, p.first.toLatin1().constData(), p.second, 5, 80);
    auto font = [&](const char *key, const QString &label)
    {
        auto *box = new QFontComboBox;
        box->setObjectName(key);
        type->addRow(label, box);
        m_fields[key] = box;
        connect(box, &QFontComboBox::currentFontChanged, this, [this] { apply(); });
    };
    font("chineseFont", tr("默认中文字体"));
    font("numberFont", tr("数字字体"));
    font("titleFont", tr("标题字体"));
    font("subtitleFont", tr("副标题字体"));
    font("authorFont", tr("署名字体"));
    font("lyricFont", tr("歌词字体"));
    for (auto p : QList<QPair<QString, QString>>{{"titlePt", tr("主标题 pt")},
                                                 {"subtitlePt", tr("副标题 pt")},
                                                 {"authorPt", tr("署名 pt")},
                                                 {"notePt", tr("音符 pt")},
                                                 {"lyricPt", tr("歌词 pt")}})
        number(type, p.first.toLatin1().constData(), p.second, 6, 60);
    check(type, "titleBold", tr("主标题加粗"));
    check(type, "subtitleBold", tr("副标题加粗"));
    check(type, "authorBold", tr("署名加粗"));
    check(type, "noteBold", tr("数字加粗"));
    check(type, "lyricBold", tr("歌词加粗"));
    combo(type, "titleAlign", tr("标题对齐"),
          {{tr("居中"), "center"}, {tr("左对齐"), "left"}, {tr("右对齐"), "right"}});
    combo(type, "authorAlign", tr("署名对齐"),
          {{tr("右对齐"), "right"}, {tr("居中"), "center"}, {tr("左对齐"), "left"}});
    number(spacing, "noteGap", tr("音符间距倍率"), 0.5, 3, 0.05);
    number(spacing, "measureGap", tr("小节间距倍率"), 0.5, 3, 0.05);
    number(spacing, "lineWidth", tr("线宽 px"), 0.4, 4, 0.1);
    number(spacing, "voiceGap", tr("声部间距 px"), 0, 100);
    number(spacing, "systemGap", tr("谱行间距 px"), 0, 200);
    number(spacing, "verseGap", tr("歌词行距倍率"), 1, 3, 0.05);
    number(spacing, "lyricGap", tr("音符至歌词 px"), 0, 80);
    number(spacing, "headerGap", tr("标题下方间距 px"), 0, 60);
    number(flow, "measuresPerLine", tr("每行目标小节数"), 1, 32);
    check(flow, "sourceBreaks", tr("遵循文字曲行换行"));
    check(flow, "stretchLast", tr("末行撑满"));
    check(flow, "showVoices", tr("显示声部名称"));
    check(flow, "showMeasures", tr("显示小节号"));
    check(flow, "showPages", tr("显示页码"));
    check(flow, "continuationTitle", tr("续页显示曲名"));
    auto *reset = new QPushButton(tr("恢复当前预设"));
    auto *save = new QPushButton(tr("保存为自定义预设…"));
    root->addWidget(reset);
    root->addWidget(save);
    auto preset = [this]
    {
        auto id = m_presets->currentData().toString();
        QJsonObject value;
        if (id.startsWith("custom:"))
        {
            QSettings settings;
            value =
                QJsonDocument::fromJson(settings.value("layoutPresets/" + id.mid(7)).toByteArray())
                    .object();
        }
        else
            value = LayoutSettings::defaults(id);
        value["preset"] = id;
        auto root = m_document->viewSettings();
        root["layout"] = value;
        m_document->setViewSettings(root);
    };
    connect(m_presets, &QComboBox::activated, this, [preset] { preset(); });
    connect(reset, &QPushButton::clicked, this, [preset] { preset(); });
    connect(save, &QPushButton::clicked, this,
            [this]
            {
                bool ok = false;
                auto name = QInputDialog::getText(this, tr("自定义排版预设"), tr("名称"),
                                                  QLineEdit::Normal, {}, &ok)
                                .trimmed();
                if (!ok || name.isEmpty() || name.contains('/'))
                    return;
                QSettings settings;
                settings.setValue(
                    "layoutPresets/" + name,
                    QJsonDocument(LayoutSettings::fromJson(m_document->viewSettings()).toJson())
                        .toJson(QJsonDocument::Compact));
                if (m_presets->findData("custom:" + name) < 0)
                    m_presets->addItem(name, "custom:" + name);
                m_presets->setCurrentIndex(m_presets->findData("custom:" + name));
            });
    auto *habit = new QComboBox;
    habit->setObjectName("accidentalScope");
    habit->addItem(tr("升降号在同小节沿用"), "measure");
    habit->addItem(tr("升降号只影响当前音符"), "note");
    root->addWidget(habit);
    connect(habit, &QComboBox::activated, this,
            [this, habit]
            {
                auto view = m_document->viewSettings();
                view["inputHabits"] =
                    QJsonObject{{"accidentalScope", habit->currentData().toString()}};
                m_document->setViewSettings(view, "input-habit");
                m_document->applyText(m_document->sourceText(), "input-habit");
            });
    connect(d, &ScoreDocument::changed, this, &LayoutPanel::refresh);
    refresh();
}
void LayoutPanel::apply()
{
    if (m_refreshing)
        return;
    auto settings = LayoutSettings::fromJson(m_document->viewSettings()).toJson();
    for (auto it = m_fields.begin(); it != m_fields.end(); ++it)
    {
        auto *widget = it.value();
        if (auto *f = qobject_cast<QFontComboBox *>(widget))
            settings[it.key()] = f->currentFont().family();
        else if (auto *box = qobject_cast<QComboBox *>(widget))
            settings[it.key()] = box->currentData().toString();
        else if (auto *box = qobject_cast<QDoubleSpinBox *>(widget))
            settings[it.key()] = box->value();
        else if (auto *box = qobject_cast<QCheckBox *>(widget))
            settings[it.key()] = box->isChecked();
    }
    auto view = m_document->viewSettings();
    view["layout"] = settings;
    m_document->setViewSettings(view);
}
void LayoutPanel::refresh()
{
    m_refreshing = true;
    auto settings = LayoutSettings::fromJson(m_document->viewSettings()).toJson();
    for (auto it = m_fields.begin(); it != m_fields.end(); ++it)
    {
        QSignalBlocker blocker(it.value());
        auto value = settings[it.key()];
        if (auto *f = qobject_cast<QFontComboBox *>(it.value()))
            f->setCurrentFont(QFont(value.toString().isEmpty() ? settings["chineseFont"].toString()
                                                               : value.toString()));
        else if (auto *box = qobject_cast<QComboBox *>(it.value()))
            box->setCurrentIndex(box->findData(value.toString()));
        else if (auto *box = qobject_cast<QDoubleSpinBox *>(it.value()))
            box->setValue(value.toDouble());
        else if (auto *box = qobject_cast<QCheckBox *>(it.value()))
            box->setChecked(value.toBool());
    }
    m_presets->setCurrentIndex(std::max(0, m_presets->findData(settings["preset"].toString())));
    m_refreshing = false;
}
} // namespace jianpu
