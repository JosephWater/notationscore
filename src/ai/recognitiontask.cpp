// SPDX-License-Identifier: GPL-3.0-only
#include "recognitiontask.h"
#include "core/scoredocument.h"
#include "input/textmarks.h"
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>
static void initRecognitionPrompt() { Q_INIT_RESOURCE(jianpu_ai_prompt); }
namespace jianpu
{
RecognitionTask::RecognitionTask(QObject *p) : QObject(p), m_client(this)
{
    connect(&m_client, &AiClient::contentChanged, this,
            [this](const QString &s)
            {
                m_source = s;
                emit sourceChanged(s);
            });
    connect(&m_client, &AiClient::failed, this,
            [this](const QString &why)
            {
                if (m_source.isEmpty())
                    m_source = m_previous;
                finish(false, why);
            });
    connect(&m_client, &AiClient::completed, this,
            [this](const QString &source, const QJsonObject &)
            {
                if (!m_running)
                    return;
                m_source = source;
                // Some providers wrap plain-text answers despite explicit instructions.
                if (m_source.startsWith("```"))
                {
                    auto lines = m_source.trimmed().split('\n');
                    if (lines.size() > 2 && lines.last() == "```")
                    {
                        lines.removeFirst();
                        lines.removeLast();
                        m_source = lines.join('\n') + "\n";
                    }
                }
                emit sourceChanged(m_source);
                auto errors = validateSource(m_source);
                if (m_stage == 0 && m_config.checks >= 1)
                {
                    m_previous = m_source;
                    ++m_stage;
                    QTimer::singleShot(0, this, &RecognitionTask::request);
                    return;
                }
                if (!errors.isEmpty() && m_stage < m_config.checks)
                {
                    m_previous = m_source;
                    ++m_stage;
                    QTimer::singleShot(0, this, &RecognitionTask::request);
                    return;
                }
                finish(errors.isEmpty(), errors.isEmpty()
                                             ? tr("识别及检查完成，请对照原图核对音符和记号。")
                                             : errors.join('\n'));
            });
}
QString RecognitionTask::systemPrompt()
{
    static const bool initialized = []
    {
        initRecognitionPrompt();
        return true;
    }();
    Q_UNUSED(initialized);
    QFile f(":/ai/recognition-system-v1.txt");
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}
QStringList RecognitionTask::validateSource(const QString &source)
{
    auto parsed = FanqieFormat().parse(source);
    QStringList messages = parsed.messages();
    if (parsed.notes.isEmpty())
        messages << tr("文字稿中没有可识别的音符");
    if (QRegularExpression("^\\s*#\\s*(疑点|无法识别|不确定|待核对)",
                           QRegularExpression::MultilineOption)
            .match(source)
            .hasMatch())
        messages << tr("图中仍有疑点，需要人工核对后移除疑点注释");
    if (!parsed.pendingLyrics.isEmpty())
        messages << tr("仍有待分配歌词");
    if (parsed.hasErrors() || parsed.fatal)
    {
        messages.removeDuplicates();
        return messages;
    }
    if (!parsed.voiceNames.isEmpty())
    {
        MusicalTime previous;
        for (const auto &b : parsed.boundaries)
            if (b.voice == parsed.voiceNames.firstKey() && !b.automatic)
            {
                MusicalTime nominal(parsed.numerator, parsed.denominator);
                for (const auto &m : parsed.marks)
                {
                    MusicalTime at;
                    TextMarks::time(m["start"], at);
                    if (m["kind"] == "time" && at <= previous)
                        nominal = {m["numerator"].toInt(), m["denominator"].toInt(4)};
                }
                if (b.position - previous > nominal)
                    messages << tr("小节线之间的时值超过拍号，请对照图片核对，不能自动补改音符");
                previous = b.position;
            }
    }
    if (messages.isEmpty())
    {
        ScoreDocument candidate;
        if (!candidate.newScore() || !candidate.applyText(source))
            messages << candidate.error();
        else
        {
            auto actual = candidate.textSnapshot();
            if (actual.hasErrors())
                messages += actual.messages();
        }
    }
    messages.removeDuplicates();
    return messages;
}
void RecognitionTask::start(const AiConfig &c, const QStringList &paths)
{
    if (m_running)
        return;
    m_config = c;
    m_config.checks = std::clamp(c.checks, 0, 2);
    m_images = {};
    m_source.clear();
    m_previous.clear();
    m_stage = 0;
    auto error = c.validate();
    if (!error.isEmpty())
    {
        emit finished({}, false, error);
        return;
    }
    if (paths.isEmpty())
    {
        emit finished({}, false, tr("请选择谱图"));
        return;
    }
    qsizetype total = 0;
    for (const auto &path : paths)
    {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        auto size = reader.size();
        if (!size.isValid() || qint64(size.width()) * size.height() > 60000000)
        {
            emit finished({}, false, tr("图片无效或尺寸过大：%1").arg(QFileInfo(path).fileName()));
            return;
        }
        auto image = reader.read();
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (image.isNull() || !image.save(&buffer, "PNG"))
        {
            emit finished({}, false, tr("无法解码谱图：%1").arg(QFileInfo(path).fileName()));
            return;
        }
        auto encoded = bytes.toBase64();
        total += encoded.size();
        if (total > 32 * 1024 * 1024)
        {
            emit finished({}, false, tr("图片编码总量超过 32 MiB，请减少或裁剪图片"));
            return;
        }
        m_images.append(QJsonObject{
            {"type", "image_url"},
            {"image_url",
             QJsonObject{{"url", "data:image/png;base64," + QString::fromLatin1(encoded)}}}});
    }
    m_running = true;
    request();
}
void RecognitionTask::request()
{
    if (!m_running)
        return;
    static const QStringList labels{tr("识别谱图"), tr("对照原图复核"), tr("修复剩余问题")};
    emit stageChanged(m_stage, labels.value(m_stage));
    m_source.clear();
    QString instruction = tr("以下图片按曲谱页序排列，合并成同一首曲谱。只输出完整 JianpuLab "
                             "文字稿。跨页不要重复音符、歌词或页眉。\n");
    if (m_stage)
        instruction +=
            tr("请重新看原图，对照下列文字稿逐项核对音高、八度点、减时线、附点、时值、小节、歌词、"
               "声部和所有记号。重点检查纵向叠写的和弦全部音、跨行跳房子的编号及完整覆盖小节、开闭"
               "端点，以及小字演奏记号；不能只重复原稿。纠错后返回完整文字稿。\n原稿：\n") +
            m_previous + "\n本地检查：\n" + validateSource(m_previous).join('\n');
    auto content = m_images;
    content.prepend(QJsonObject{{"type", "text"}, {"text", instruction}});
    QJsonArray messages{QJsonObject{{"role", "system"}, {"content", systemPrompt()}},
                        QJsonObject{{"role", "user"}, {"content", content}}};
    QJsonObject payload{
        {"messages", messages}, {"stream", true}, {"max_tokens", 16384}, {"temperature", 0.1}};
    // DeepSeek otherwise defaults to lengthy hidden reasoning, which can consume
    // the entire output allowance without yielding any transcript to the stream.
    if (m_config.model.startsWith("DeepSeek", Qt::CaseInsensitive))
        payload["thinking"] = QJsonObject{{"type", "disabled"}};
    m_client.start(m_config, payload);
}
void RecognitionTask::finish(bool success, const QString &message)
{
    m_running = false;
    m_images = {};
    emit finished(m_source, success, m_config.redact(message));
}
void RecognitionTask::cancel()
{
    if (!m_running)
        return;
    if (m_client.running())
        m_client.cancel();
    else
        finish(false, tr("已取消识别"));
}
} // namespace jianpu
