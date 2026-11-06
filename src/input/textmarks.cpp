// SPDX-License-Identifier: GPL-3.0-only
#include "textmarks.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QXmlStreamWriter>
#include <algorithm>
namespace jianpu
{
bool TextMarks::time(const QJsonValue &v, MusicalTime &out)
{
    auto m = QRegularExpression("^(0|[1-9][0-9]{0,8})/([1-9][0-9]{0,8})$").match(v.toString());
    if (!m.hasMatch())
        return false;
    out = {m.captured(1).toLongLong(), m.captured(2).toLongLong()};
    return true;
}
QString TextMarks::fraction(MusicalTime t)
{
    return QString::number(t.n) + "/" + QString::number(t.d);
}
QStringList TextMarks::kinds()
{
    return {"volta",        "repeat", "jump",  "marker", "tempo", "fermata",
            "articulation", "breath", "pedal", "ottava", "key",   "time"};
}
bool TextMarks::encodable(const MarkData &m)
{
    if (!kinds().contains(m.kind))
        return false;
    if (m.kind == "articulation")
        return QStringList{"staccato",      "tenuto",          "accent", "strong-accent",
                           "staccatissimo", "trill-mark",      "turn",   "inverted-turn",
                           "mordent",       "inverted-mordent"}
            .contains(m.attributes["type"].toString());
    if (m.kind == "jump")
        return QStringList{"DC", "DS", "DC_FINE", "DS_FINE", "DC_CODA", "DS_CODA"}.contains(
            m.attributes["type"].toString());
    if (m.kind == "marker")
        return QStringList{"segno", "coda", "fine", "tocoda"}.contains(
            m.attributes["type"].toString());
    if (m.kind == "ottava")
        return QList<int>{-2, -1, 1, 2}.contains(m.attributes["octaves"].toInt());
    if (m.kind == "tempo")
        return m.attributes["bpm"].toDouble() > 0;
    if (m.kind == "volta")
        return !m.attributes["endings"].toArray().isEmpty();
    return true;
}
QString TextMarks::validate(const QJsonObject &m, const TextParseResult &r)
{
    auto kind = m["kind"].toString();
    if (!kinds().contains(kind))
        return QStringLiteral("未知 M 记号类型：%1").arg(kind);
    int voice = m["voice"].toInt(-1);
    if (voice < 0 || (voice && !r.voiceNames.contains(voice)))
        return QStringLiteral("M 记号声部不存在");
    MusicalTime a, b;
    if (!time(m["start"], a))
        return QStringLiteral("M.start 需要非负精确分数，例如 0/1");
    auto end =
        r.measures.isEmpty() ? MusicalTime() : r.measures.last().start + r.measures.last().duration;
    if (a > end)
        return QStringLiteral("M 记号超出曲尾");
    const bool span = kind == "volta" || kind == "pedal" || kind == "ottava";
    if (span && (!time(m["end"], b) || b <= a || b > end))
        return QStringLiteral("M 记号结束位置无效");
    auto boundary = [&](MusicalTime t)
    {
        if (t == end)
            return true;
        for (const auto &bar : r.measures)
            if (bar.start == t)
                return true;
        return false;
    };
    if ((kind == "volta" && (!boundary(a) || !boundary(b))) ||
        ((kind == "repeat" || kind == "key" || kind == "time") && !boundary(a)))
        return QStringLiteral("此 M 记号需要位于小节边界");
    if (kind == "volta")
    {
        auto endings = m["endings"].toArray();
        if (endings.isEmpty())
            return QStringLiteral("跳房子需要 endings 编号数组");
        for (auto e : endings)
            if (!e.isDouble() || e.toDouble() != e.toInt() || e.toInt() < 1 || e.toInt() > 128)
                return QStringLiteral("跳房子编号需要 1–128 的整数");
        if (!m["closed"].isBool())
            return QStringLiteral("跳房子需要 closed 布尔值");
    }
    if (kind == "repeat")
    {
        bool repeat = false;
        for (const auto &bar : r.measures)
            repeat |= bar.repeatEnd && bar.start + bar.duration == a;
        if (!repeat)
            return QStringLiteral("反复次数需要落在右反复小节线 :| 的结束位置");
    }
    if (kind == "repeat" && (m["count"].toInt() < 2 || m["count"].toInt() > 128))
        return QStringLiteral("反复次数需要 2–128");
    if (kind == "tempo" && (m["bpm"].toDouble() <= 0 || m["bpm"].toDouble() > 1000))
        return QStringLiteral("速度需要 0–1000 BPM");
    if (kind == "jump" &&
        !QStringList{"DC", "DS", "DC_FINE", "DS_FINE", "DC_CODA", "DS_CODA"}.contains(
            m["type"].toString()))
        return QStringLiteral("未知跳转类型");
    if (kind == "marker" &&
        !QStringList{"segno", "coda", "fine", "tocoda"}.contains(m["type"].toString()))
        return QStringLiteral("未知跳转标记");
    if (kind == "articulation" &&
        !QStringList{"staccato", "tenuto", "accent", "strong-accent", "staccatissimo", "trill-mark",
                     "turn", "inverted-turn", "mordent", "inverted-mordent"}
             .contains(m["type"].toString()))
        return QStringLiteral("未知演奏法或装饰音");
    if (kind == "ottava" && !QList<int>{-2, -1, 1, 2}.contains(m["octaves"].toInt()))
        return QStringLiteral("八度线 octaves 需要 -2、-1、1、2");
    if (kind == "key" && (!m["fifths"].isDouble() || std::abs(m["fifths"].toInt()) > 7))
        return QStringLiteral("调号 fifths 需要 -7–7");
    if (kind == "time")
    {
        int n = m["numerator"].toInt(), d = m["denominator"].toInt();
        if (n < 1 || n > 32 || d < 1 || d > 32 || (d & (d - 1)))
            return QStringLiteral("M 拍号无效");
    }
    if (kind == "ottava")
    {
        bool first = false, last = false;
        for (const auto &n : r.notes)
            if (!voice || voice == n.voice)
            {
                first |= n.position == a;
                last |= n.position == b || n.position + n.duration == b;
            }
        if (!first || !last)
            return QStringLiteral("八度线需要落在音符边界");
    }
    if (kind == "fermata" || kind == "articulation" || kind == "breath")
    {
        bool found = false;
        for (const auto &n : r.notes)
            found |= n.position == a && (!voice || n.voice == voice);
        if (!found)
            return QStringLiteral("M 记号没有对应音符");
    }
    return {};
}
QString TextMarks::lines(const ScoreSnapshot &s)
{
    QString result;
    QMap<int, int> voices;
    int number = 0;
    for (const auto &v : s.voices)
        if (!v.percussion)
            voices[v.track] = ++number;
    for (const auto &m : s.marks)
    {
        if (!encodable(m) || (m.kind == "key" && !m.tick) || (m.kind == "time" && !m.tick))
            continue;
        if (!voices.contains(m.track) && m.track >= 0)
            continue;
        auto o = m.attributes;
        o["kind"] = m.kind;
        o["voice"] = m.track < 0 ? 0 : voices.value(m.track);
        o["start"] = fraction(m.position != MusicalTime() ? m.position : MusicalTime(m.tick, 1920));
        if (m.endTick > m.tick)
            o["end"] = fraction(m.endPosition != MusicalTime() ? m.endPosition
                                                               : MusicalTime(m.endTick, 1920));
        if (!m.text.isEmpty())
            o["text"] = m.text;
        result += "M: " + QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)) + "\n";
    }
    for (const auto &bar : s.measures)
        if (bar.repeatEnd && bar.repeatCount != 2)
        {
            QJsonObject o{{"kind", "repeat"},
                          {"voice", 0},
                          {"start", fraction(MusicalTime(bar.tick + bar.duration, 1920))},
                          {"count", bar.repeatCount}};
            result +=
                "M: " + QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)) + "\n";
        }
    return result;
}
QString TextMarks::replaceLines(const QString &source, const ScoreSnapshot &s)
{
    auto lines = source.split('\n');
    lines.removeIf([](const QString &l)
                   { return QRegularExpression("^\\s*M\\s*[:：]").match(l).hasMatch(); });
    auto marks = TextMarks::lines(s);
    auto out = lines.join('\n');
    if (!marks.isEmpty())
    {
        out.replace(QRegularExpression("^V: 1\\.0", QRegularExpression::MultilineOption), "V: 1.1");
        if (!out.endsWith('\n'))
            out += '\n';
        out += marks;
    }
    return out;
}
void TextMarks::directions(QXmlStreamWriter &w, const TextParseResult &r, int voice,
                           MusicalTime start, MusicalTime finish, qint64 divisions)
{
    for (int i = 0; i < r.marks.size(); ++i)
    {
        const auto &m = r.marks[i];
        if (m["voice"].toInt() && m["voice"].toInt() != voice)
            continue;
        // Global directions occur once on the first part.
        auto kind = m["kind"].toString();
        if (!m["voice"].toInt() && voice != r.voiceNames.firstKey() && kind != "key" &&
            kind != "time")
            continue;
        MusicalTime a, b;
        time(m["start"], a);
        time(m["end"], b);
        if (kind == "key" || kind == "time")
        {
            if (a != start)
                continue;
            w.writeStartElement("attributes");
            w.writeStartElement(kind);
            if (kind == "key")
            {
                w.writeTextElement("fifths", QString::number(m["fifths"].toInt()));
                w.writeTextElement("mode", m["minor"].toBool() ? "minor" : "major");
            }
            else
            {
                w.writeTextElement("beats", QString::number(m["numerator"].toInt()));
                w.writeTextElement("beat-type", QString::number(m["denominator"].toInt()));
            }
            w.writeEndElement();
            w.writeEndElement();
            continue;
        }
        if (!QStringList{"tempo", "jump", "marker", "pedal"}.contains(kind))
            continue;
        for (bool stop : {false, true})
        {
            if (stop && kind != "pedal" && kind != "ottava")
                continue;
            auto at = stop ? b : a;
            if (at < start || at >= finish)
            {
                if (!(at == finish &&
                      finish == r.measures.last().start + r.measures.last().duration))
                    continue;
            }
            w.writeStartElement("direction");
            w.writeStartElement("direction-type");
            auto type = m["type"].toString();
            if (kind == "pedal" || kind == "ottava")
            {
                w.writeStartElement(kind == "pedal" ? "pedal" : "octave-shift");
                w.writeAttribute("number", QString::number(i % 16 + 1));
                w.writeAttribute("type", stop                       ? "stop"
                                         : kind == "pedal"          ? "start"
                                         : m["octaves"].toInt() > 0 ? "down"
                                                                    : "up");
                if (kind == "pedal")
                {
                    w.writeAttribute("line", "yes");
                    w.writeAttribute("sign", "yes");
                }
                else
                    w.writeAttribute("size", std::abs(m["octaves"].toInt()) == 2 ? "15" : "8");
                w.writeEndElement();
            }
            else if (kind == "marker" && (type == "segno" || type == "coda"))
                w.writeEmptyElement(type);
            else
            {
                QString label = m["text"].toString();
                if (label.isEmpty())
                    label = kind == "tempo"  ? QString("♩ = %1").arg(m["bpm"].toDouble())
                            : kind == "jump" ? QString(type)
                                                   .replace("_FINE", " al Fine")
                                                   .replace("_CODA", " al Coda")
                            : type == "fine" ? "Fine"
                                             : "To Coda";
                w.writeTextElement("words", label);
            }
            w.writeEndElement();
            auto units = (at - start) * MusicalTime(4 * divisions);
            w.writeTextElement("offset", QString::number(units.n / units.d));
            w.writeStartElement("sound");
            if (kind == "tempo")
                w.writeAttribute("tempo", QString::number(m["bpm"].toDouble()));
            if (kind == "marker")
                w.writeAttribute(type == "tocoda" ? "tocoda" : type,
                                 type == "fine" ? "yes" : m["label"].toString("1"));
            if (kind == "jump")
            {
                w.writeAttribute(type.startsWith("DC") ? "dacapo" : "dalsegno",
                                 type.startsWith("DC") ? "yes" : m["target"].toString("1"));
                if (type.endsWith("FINE"))
                    w.writeAttribute("fine", "yes");
                if (type.endsWith("CODA"))
                    w.writeAttribute("tocoda", m["target"].toString("1"));
            }
            w.writeEndElement();
            w.writeEndElement();
        }
    }
}
void TextMarks::octaves(QXmlStreamWriter &w, const TextParseResult &r, int voice, MusicalTime at)
{
    for (bool stop : {true, false})
        for (int i = 0; i < r.marks.size(); ++i)
        {
            const auto &m = r.marks[i];
            if (m["kind"] != "ottava" || (m["voice"].toInt() && m["voice"].toInt() != voice))
                continue;
            MusicalTime a, b;
            time(m["start"], a);
            time(m["end"], b);
            if (at != (stop ? b : a))
                continue;
            w.writeStartElement("direction");
            w.writeStartElement("direction-type");
            w.writeStartElement("octave-shift");
            w.writeAttribute("number", QString::number(i % 16 + 1));
            w.writeAttribute("type", stop ? "stop" : m["octaves"].toInt() > 0 ? "down" : "up");
            w.writeAttribute("size", std::abs(m["octaves"].toInt()) == 2 ? "15" : "8");
            w.writeEndElement();
            w.writeEndElement();
            w.writeEndElement();
        }
}
void TextMarks::notations(QXmlStreamWriter &w, const TextParseResult &r, int voice, MusicalTime at)
{
    for (const auto &m : r.marks)
    {
        if (m["voice"].toInt() && m["voice"].toInt() != voice)
            continue;
        if (!m["voice"].toInt() && voice != r.voiceNames.firstKey())
            continue;
        MusicalTime a;
        time(m["start"], a);
        if (a != at)
            continue;
        auto kind = m["kind"].toString(), type = m["type"].toString();
        if (kind == "fermata")
            w.writeTextElement("fermata", "normal");
        if (kind == "breath" || kind == "articulation")
        {
            bool ornament =
                type.contains("mordent") || type.contains("turn") || type == "trill-mark";
            w.writeStartElement(ornament ? "ornaments" : "articulations");
            w.writeEmptyElement(kind == "breath" ? "breath-mark" : type);
            w.writeEndElement();
        }
    }
}
void TextMarks::barline(QXmlStreamWriter &w, const TextParseResult &r, int voice, MusicalTime pos,
                        bool left)
{
    for (const auto &m : r.marks)
    {
        if (m["voice"].toInt() && m["voice"].toInt() != voice)
            continue;
        MusicalTime a, b;
        time(m["start"], a);
        time(m["end"], b);
        if (m["kind"] == "volta" && ((left && a == pos) || (!left && b == pos)))
        {
            QStringList numbers;
            for (auto e : m["endings"].toArray())
                numbers << QString::number(e.toInt());
            w.writeStartElement("ending");
            w.writeAttribute("number", numbers.join(','));
            w.writeAttribute("type", left                   ? "start"
                                     : m["closed"].toBool() ? "stop"
                                                            : "discontinue");
            w.writeCharacters(m["text"].toString(numbers.join(',') + "."));
            w.writeEndElement();
        }
    }
}
} // namespace jianpu
