// SPDX-License-Identifier: GPL-3.0-only
#include "textformat.h"
#include "textmarks.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTextBoundaryFinder>
#include <QUuid>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

namespace jianpu
{
bool TextParseResult::hasErrors() const
{
    for (const auto &d : diagnostics)
        if (d.blocking)
            return true;
    return false;
}
QStringList TextParseResult::messages() const
{
    QStringList r;
    for (const auto &d : diagnostics)
        r << QStringLiteral("第 %1 行，第 %2 列：%3").arg(d.line).arg(d.column).arg(d.message);
    return r;
}
bool TextParseResult::canProject() const
{
    return !fatal && std::none_of(diagnostics.begin(), diagnostics.end(),
                                  [](const auto &d)
                                  {
                                      return d.blocking &&
                                             (d.category == InputDiagnostic::Category::Syntax ||
                                              d.category == InputDiagnostic::Category::Notation);
                                  });
}
QJsonArray TextParseResult::overflowJson() const
{
    QJsonArray result;
    for (const auto &entry : overflow)
        result << QJsonObject{{"voice", entry.voice},
                              {"segment", entry.segment},
                              {"verse", entry.verse},
                              {"words", QJsonArray::fromStringList(entry.words)}};
    return result;
}
void TextParseResult::restoreOverflow(const QJsonArray &value)
{
    overflow.clear();
    for (auto v : value)
    {
        auto o = v.toObject();
        Overflow entry{o["voice"].toInt(1), o["segment"].toInt(), o["verse"].toInt(), {}};
        for (auto word : o["words"].toArray())
            entry.words << word.toString();
        overflow << entry;
    }
}
std::unique_ptr<TextFormatAdapter> TextFormatAdapter::create(const QString &id)
{
    return (id == "fanqie-1.0" || id == "fanqie-1.1") ? std::make_unique<FanqieFormat>() : nullptr;
}
namespace
{
void diagnostic(TextParseResult &r, int p, const QString &msg, bool blocking = true,
                InputDiagnostic::Category category = InputDiagnostic::Category::Syntax)
{
    auto prefix = r.source.left(p);
    int line = prefix.count('\n') + 1, start = prefix.lastIndexOf('\n');
    r.diagnostics.push_back({p, 1, line, p - start, msg, blocking, category});
}
int keyAlter(int step, int fifths)
{
    static const int sharp[]{3, 0, 4, 1, 5, 2, 6}, flat[]{6, 2, 5, 1, 4, 0, 3};
    for (int i = 0; i < std::abs(fifths); ++i)
        if ((fifths > 0 ? sharp[i] : flat[i]) == step)
            return fifths > 0 ? 1 : -1;
    return 0;
}
int tonicStep(int fifths) { return ((fifths * 4) % 7 + 7) % 7; }
QStringList syllables(const QString &text)
{
    QStringList result;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    int begin = 0;
    bool join = false;
    while (true)
    {
        int end = finder.toNextBoundary();
        if (end < 0)
            break;
        auto c = text.mid(begin, end - begin);
        begin = end;
        if (c == "~")
        {
            join = true;
            continue;
        }
        if (c.trimmed().isEmpty())
            continue;
        if (c != "@" && (c[0].isPunct() || c[0].category() == QChar::Symbol_Other))
        {
            if (!result.isEmpty() && result.last() != "@")
                result.last() += c;
            join = false;
            continue;
        }
        if (join && !result.isEmpty() && result.last() != "@")
            result.last() += c;
        else
            result << c;
        join = false;
    }
    return result;
}
struct Reader
{
    TextParseResult r;
    QJsonObject habits;
    QMap<int, QVector<int>> voiceNotes, lastSegment;
    QMap<int, int> verseCount;
    QVector<int> openSpans;
    int segment = 0;
    bool note(const QString &text, int &p, int base, int voice, TextNote &n, bool grace = false)
    {
        int start = p;
        n.degree = text[p++].digitValue();
        n.voice = voice;
        n.segment = segment;
        n.offset = base + start;
        n.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        int slashLimit = grace ? 7 : 8;
        while (p < text.size())
        {
            auto c = text[p];
            if (c == '/')
            {
                ++n.slashes;
                ++p;
            }
            else if (c == '-')
            {
                ++n.extensions;
                ++p;
            }
            else if (c == '.')
            {
                ++n.dots;
                ++p;
            }
            else if (c == '\'')
            {
                ++n.octave;
                ++p;
            }
            else if (c == ',')
            {
                --n.octave;
                ++p;
            }
            else if (c == '#' || c == '$' || c == '=')
            {
                n.accidental = c == '#' ? 1 : c == '$' ? -1 : 0;
                ++p;
            }
            else if (c == '^' && !grace)
            {
                n.tieForward = true;
                ++p;
            }
            else
                break;
        }
        if (n.slashes > slashLimit || n.dots > 3 || std::abs(n.octave) > 8 || n.extensions > 128)
        {
            diagnostic(r, n.offset, QStringLiteral("音符时值或八度超出支持范围"));
            return false;
        }
        n.written = MusicalTime(1, (grace ? 8 : 4) * (qint64(1) << n.slashes)) +
                    MusicalTime(n.extensions, 4);
        n.written = n.written * MusicalTime((qint64(1) << (n.dots + 1)) - 1, qint64(1) << n.dots);
        n.duration = n.written;
        while (!grace && p < text.size())
        {
            if (text[p] == '[')
            {
                int opening = p++;
                bool after = p < text.size() && text[p] == 'h';
                if (after)
                    ++p;
                while (p < text.size() && text[p] != ']')
                {
                    if (text[p].isSpace())
                    {
                        ++p;
                        continue;
                    }
                    if (text[p] >= '1' && text[p] <= '7')
                    {
                        TextNote g;
                        if (!note(text, p, base, voice, g, true))
                            break;
                        g.graceAfter = after;
                        n.graces << g;
                    }
                    else
                    {
                        diagnostic(r, base + p, QStringLiteral("倚音中需要 1–7 及音符后缀"));
                        ++p;
                    }
                }
                if (p == text.size())
                    diagnostic(r, base + opening, QStringLiteral("倚音缺少 ]"), true,
                               InputDiagnostic::Category::Incomplete);
                else
                    ++p;
            }
            else if (text[p] == '"')
            {
                int opening = p++;
                QString value;
                bool closed = false;
                while (p < text.size())
                {
                    auto c = text[p++];
                    if (c == '"')
                    {
                        closed = true;
                        break;
                    }
                    if (c == '\\' && p < text.size())
                        c = text[p++];
                    value += c;
                }
                n.annotation = value;
                if (!closed)
                    diagnostic(r, base + opening, QStringLiteral("注释缺少结束引号"), true,
                               InputDiagnostic::Category::Incomplete);
            }
            else if (text[p] == '&')
            {
                int start = ++p;
                while (p < text.size() && text[p].isLetter() && text[p].unicode() < 128)
                    ++p;
                auto dynamic = text.mid(start, p - start);
                static const QSet<QString> valid{"pppppp", "ppppp",  "pppp", "ppp", "pp",  "p",
                                                 "mp",     "mf",     "f",    "ff",  "fff", "ffff",
                                                 "fffff",  "ffffff", "fp",   "pf",  "sf",  "sfz",
                                                 "sffz",   "sfp",    "sfpp", "rfz", "fz"};
                if (!valid.contains(dynamic))
                    diagnostic(r, base + start, QStringLiteral("未知力度：%1").arg(dynamic));
                else
                    n.dynamics << dynamic;
            }
            else
                break;
        }
        n.length = p - start;
        n.raw = text.mid(start, n.length);
        return true;
    }
    int lastForVoice(int voice) const
    {
        return voiceNotes.value(voice).isEmpty() ? -1 : voiceNotes.value(voice).last();
    }
    void music(const QString &text, int base, int voice)
    {
        ++segment;
        lastSegment[voice].clear();
        verseCount[voice] = 0;
        bool first = true;
        for (int p = 0; p < text.size();)
        {
            auto c = text[p];
            if (c.isSpace())
            {
                ++p;
                continue;
            }
            if ((c >= '0' && c <= '9') || c == '{')
            {
                TextNote n;
                bool valid = false;
                if (c == '{')
                {
                    int opening = p++;
                    QVector<TextNote> pitches;
                    while (p < text.size() && text[p] != '}')
                    {
                        if (text[p].isSpace())
                        {
                            ++p;
                            continue;
                        }
                        if (text[p] < '1' || text[p] > '7')
                        {
                            diagnostic(r, base + p, QStringLiteral("和弦内需要 1–7 音高"));
                            ++p;
                            continue;
                        }
                        TextNote pitch;
                        if (note(text, p, base, voice, pitch, true))
                        {
                            if (pitch.slashes || pitch.extensions || pitch.dots)
                                diagnostic(r, pitch.offset, QStringLiteral("和弦时值请写在 } 后"));
                            pitches << pitch;
                        }
                    }
                    if (p == text.size())
                        diagnostic(r, base + opening, QStringLiteral("和弦缺少 }"), true,
                                   InputDiagnostic::Category::Incomplete);
                    else
                        ++p;
                    if (!pitches.isEmpty())
                    {
                        int suffix = p;
                        auto tail = QString("1") + text.mid(p);
                        int cursor = 0;
                        valid = note(tail, cursor, base + suffix - 1, voice, n);
                        p += cursor - 1;
                        n.degree = pitches[0].degree;
                        n.octave = pitches[0].octave;
                        n.accidental = pitches[0].accidental;
                        n.chord = pitches.mid(1);
                        n.offset = base + opening;
                        n.length = p - opening;
                        n.raw = text.mid(opening, n.length);
                    }
                    if (r.version != "1.1")
                        diagnostic(r, base + opening, QStringLiteral("和弦需要 V: 1.1"));
                }
                else
                    valid = note(text, p, base, voice, n);
                if (valid)
                {
                    int previous = lastForVoice(voice);
                    n.tieBack = previous >= 0 && r.notes[previous].tieForward;
                    n.sourceBreak = first;
                    first = false;
                    int index = r.notes.size();
                    r.notes << n;
                    voiceNotes[voice] << index;
                    lastSegment[voice] << index;
                }
                continue;
            }
            if (QString("/-.',#$=").contains(c) && lastForVoice(voice) >= 0)
            {
                auto &n = r.notes[lastForVoice(voice)];
                if (c == '/')
                    ++n.slashes;
                else if (c == '-')
                    ++n.extensions;
                else if (c == '.')
                    ++n.dots;
                else if (c == '\'')
                    ++n.octave;
                else if (c == ',')
                    --n.octave;
                else
                    n.accidental = c == '#' ? 1 : c == '$' ? -1 : 0;
                if (n.slashes > 8 || n.dots > 3 || n.extensions > 128 || std::abs(n.octave) > 8)
                    diagnostic(r, base + p, QStringLiteral("音符后缀超出支持范围"));
                else
                {
                    n.written =
                        (MusicalTime(1, 4 * (1LL << n.slashes)) + MusicalTime(n.extensions, 4)) *
                        MusicalTime((1LL << (n.dots + 1)) - 1, 1LL << n.dots);
                    n.duration =
                        n.written * (n.tuplet ? MusicalTime(n.normal, n.tuplet) : MusicalTime(1));
                }
                ++p;
                n.length = base + p - n.offset;
                n.raw = r.source.mid(n.offset, n.length);
                continue;
            }
            if (c == '(')
            {
                int opening = p++;
                QString kind = "slur";
                int actual = 0, normal = 0;
                if (p < text.size() && text[p] == 'y')
                {
                    kind = "tuplet";
                    ++p;
                    auto m = QRegularExpression("^(\\d+):(\\d+)").match(text.mid(p));
                    if (m.hasMatch())
                    {
                        actual = m.captured(1).toInt();
                        normal = m.captured(2).toInt();
                        p += m.capturedLength();
                    }
                    if (actual > 128 || normal > 128 || (actual && !normal))
                        diagnostic(r, base + opening,
                                   QStringLiteral("连音比例须为 1–128 的正整数"));
                }
                r.spans.push_back({kind, int(r.notes.size()), -1, base + opening, false, voice});
                openSpans << r.spans.size() - 1;
                // Explicit ratios are stored on the span's opening location until closure.
                if (kind == "tuplet")
                    ratios[r.spans.size() - 1] = {actual, normal};
                continue;
            }
            if (c == ')')
            {
                int found = -1;
                for (int i = openSpans.size() - 1; i >= 0; --i)
                    if (r.spans[openSpans[i]].voice == voice &&
                        (r.spans[openSpans[i]].kind == "slur" ||
                         r.spans[openSpans[i]].kind == "tuplet"))
                    {
                        found = i;
                        break;
                    }
                if (found < 0)
                    diagnostic(r, base + p, QStringLiteral("没有对应的左括号"));
                else
                {
                    int si = openSpans.takeAt(found);
                    auto &span = r.spans[si];
                    span.last = lastForVoice(voice);
                    while (span.first <= span.last && r.notes[span.first].voice != voice)
                        ++span.first;
                    span.closed = span.last >= span.first && r.notes[span.first].voice == voice;
                    if (!span.closed)
                        diagnostic(r, base + p, QStringLiteral("括号内缺少当前声部音符"));
                    else if (span.kind == "tuplet")
                    {
                        QVector<int> members;
                        for (int i = span.first; i <= span.last; ++i)
                            if (r.notes[i].voice == voice)
                                members << i;
                        int actual = ratios[si].first, normal = ratios[si].second;
                        if (!actual)
                        {
                            actual = members.size();
                            if (actual == 2)
                                normal = 3;
                            else
                            {
                                normal = 1;
                                while (normal * 2 < actual)
                                    normal *= 2;
                                if (actual >= 4 && (actual & (actual - 1)) == 0)
                                    normal = actual * 3 / 4;
                            }
                        }
                        if (actual < 2 || normal < 1 || actual > 128 || normal > 128)
                            diagnostic(r, span.offset,
                                       QStringLiteral("连音组至少需要两个音符，比例最大 128"));
                        else
                            for (int i : members)
                            {
                                if (r.notes[i].tupletId >= 0)
                                {
                                    diagnostic(
                                        r, span.offset,
                                        QStringLiteral("暂不支持嵌套连音组；嵌套连奏线可正常使用"));
                                    break;
                                }
                                r.notes[i].duration =
                                    r.notes[i].duration * MusicalTime(normal, actual);
                                r.notes[i].tuplet = actual;
                                r.notes[i].normal = normal;
                                r.notes[i].tupletId = si;
                            }
                    }
                }
                ++p;
                continue;
            }
            if (c == '<' || c == '>')
            {
                r.spans.push_back({c == '<' ? "crescendo" : "diminuendo", int(r.notes.size()), -1,
                                   base + p, false, voice});
                openSpans << r.spans.size() - 1;
                ++p;
                continue;
            }
            if (c == '!')
            {
                int found = -1;
                for (int i = openSpans.size() - 1; i >= 0; --i)
                    if (r.spans[openSpans[i]].voice == voice &&
                        (r.spans[openSpans[i]].kind == "crescendo" ||
                         r.spans[openSpans[i]].kind == "diminuendo"))
                    {
                        found = i;
                        break;
                    }
                if (found < 0)
                    diagnostic(r, base + p, QStringLiteral("没有对应的渐强／渐弱起点"));
                else
                {
                    auto &span = r.spans[openSpans.takeAt(found)];
                    span.last = lastForVoice(voice);
                    while (span.first <= span.last && r.notes[span.first].voice != voice)
                        ++span.first;
                    span.closed = span.last >= span.first;
                }
                ++p;
                continue;
            }
            if (c == '|' || c == ':')
            {
                int start = p;
                QString token;
                for (const auto &candidate :
                     {QString(":|:"), QString("|:"), QString(":|"), QString("||"), QString("|")})
                    if (text.mid(p, candidate.size()) == candidate)
                    {
                        token = candidate;
                        break;
                    }
                if (token.isEmpty())
                {
                    diagnostic(r, base + p, QStringLiteral("小节线尚未完成"));
                    ++p;
                    continue;
                }
                p += token.size();
                int type = token == "||"    ? 32
                           : token == "|:"  ? 4
                           : token == ":|"  ? 8
                           : token == ":|:" ? 64
                                            : 1;
                r.boundaries.push_back({voice, lastForVoice(voice) + 1, type, base + start, {}});
                r.boundaries.last().automatic =
                    type == 1 &&
                    habits.value("automaticBarOffsets").toArray().contains(base + start);
                continue;
            }
            diagnostic(r, base + p, QStringLiteral("无法识别的谱号：%1").arg(c));
            ++p;
        }
    }
    QMap<int, QPair<int, int>> ratios;
};
} // namespace

TextParseResult FanqieFormat::parse(const QString &source, const QJsonObject &habits) const
{
    Reader p;
    p.r.source = source;
    p.habits = habits;
    try
    {
        int current = 1, offset = 0;
        bool title = false;
        QMap<QPair<int, int>, int> lyricLocations;
        auto lines = source.split('\n');
        for (const auto &original : lines)
        {
            auto line = original;
            if (offset == 0 && line.startsWith(QChar(0xfeff)))
                line.remove(0, 1);
            if (line.trimmed().isEmpty() || line.trimmed().startsWith('#'))
            {
                offset += original.size() + 1;
                continue;
            }
            auto m = QRegularExpression("^\\s*([A-Z])(\\d*)\\s*[:：]\\s*(.*)$").match(line);
            if (!m.hasMatch())
            {
                diagnostic(p.r, offset, QStringLiteral("需要字段前缀，例如 Q: 或 C:"));
                offset += original.size() + 1;
                continue;
            }
            QString field = m.captured(1), value = m.captured(3);
            int start = offset + m.capturedStart(3);
            int voice = m.captured(2).isEmpty() ? current : m.captured(2).toInt();
            if (voice < 1 || voice > 128)
            {
                diagnostic(p.r, offset, QStringLiteral("声部编号范围为 1–128"));
                offset += original.size() + 1;
                continue;
            }
            if (field == "V")
            {
                p.r.version = value.trimmed();
                if (p.r.version != "1.0" && p.r.version != "1.1")
                    diagnostic(p.r, start, QStringLiteral("支持文字格式 V: 1.0 / 1.1"));
            }
            else if (field == "B")
            {
                if (!title)
                {
                    p.r.title = value;
                    title = true;
                }
                else
                    p.r.subtitles << value;
            }
            else if (field == "Z")
                p.r.authors << value;
            else if (field == "D")
            {
                auto key = value.trimmed().replace("♯", "#").replace("♭", "$").replace('b', '$');
                p.r.minor = key.endsWith('m', Qt::CaseInsensitive);
                if (p.r.minor)
                    key.chop(1);
                static const QStringList major{"C$", "G$", "D$", "A$", "E$", "B$", "F", "C",
                                               "G",  "D",  "A",  "E",  "B",  "F#", "C#"};
                static const QStringList minor{"A$", "E$", "B$", "F",  "C",  "G",  "D", "A",
                                               "E",  "B",  "F#", "C#", "G#", "D#", "A#"};
                int index = (p.r.minor ? minor : major).indexOf(key);
                if (index < 0)
                    diagnostic(p.r, start, QStringLiteral("无效调号，例如 D、F#、B$、Am"));
                else
                    p.r.fifths = index - 7;
            }
            else if (field == "P")
            {
                auto sig = QRegularExpression("^(\\d+)\\s*/\\s*(\\d+)\\s*$").match(value);
                int n = sig.captured(1).toInt(), d = sig.captured(2).toInt();
                if (!sig.hasMatch() || n < 1 || n > 32 || d < 1 || d > 32 || (d & (d - 1)))
                    diagnostic(p.r, start, QStringLiteral("无效拍号，例如 4/4 或 6/8"));
                else
                {
                    p.r.numerator = n;
                    p.r.denominator = d;
                }
            }
            else if (field == "M")
            {
                QJsonParseError error;
                auto doc = QJsonDocument::fromJson(value.toUtf8(), &error);
                if (error.error != QJsonParseError::NoError || !doc.isObject())
                    diagnostic(p.r, start, QStringLiteral("M 需要单行 JSON 对象"), true,
                               InputDiagnostic::Category::Notation);
                else
                {
                    auto mark = doc.object();
                    mark["_offset"] = start;
                    p.r.marks << mark;
                }
            }
            else if (field == "Q")
            {
                current = voice;
                p.r.voiceNames[voice] = QStringLiteral("声部 %1").arg(voice);
                p.music(value, start, voice);
            }
            else if (field == "C")
            {
                if (m.captured(2).isEmpty())
                    voice = current;
                auto targets = p.lastSegment.value(voice);
                int verse = p.verseCount[voice]++;
                lyricLocations[{voice, verse}] = start;
                p.r.verseRows[voice] = std::max(p.r.verseRows.value(voice), verse + 1);
                if (targets.isEmpty())
                    diagnostic(p.r, start, QStringLiteral("歌词前需要对应声部的 Q: 曲行"));
                auto words = syllables(value);
                int wi = 0;
                for (int index : targets)
                {
                    auto &note = p.r.notes[index];
                    if (note.degree == 0 || note.degree == 8 || note.tieBack)
                        continue;
                    if (wi >= words.size())
                        break;
                    if (words[wi] != "@")
                        note.lyrics[verse] = words[wi];
                    else
                        note.lyricSkips.insert(verse);
                    ++wi;
                }
                if (wi < words.size())
                {
                    auto pending = words.mid(wi).join("");
                    p.r.overflow << TextParseResult::Overflow{
                        voice, targets.isEmpty() ? p.segment : p.r.notes[targets.first()].segment,
                        verse, words.mid(wi)};
                    p.r.pendingLyrics << pending;
                    diagnostic(p.r, start,
                               QStringLiteral("%1 个歌词词位待分配：%2")
                                   .arg(words.size() - wi)
                                   .arg(pending),
                               true, InputDiagnostic::Category::LyricsOverflow);
                }
            }
            else
                diagnostic(p.r, offset, QStringLiteral("保留未支持的字段 %1").arg(field), false);
            offset += original.size() + 1;
        }
        // A pasted lyric line continues through following Q rows of this voice
        // and verse. Only text beyond the whole voice remains unassigned.
        auto excess = p.r.overflow;
        p.r.overflow.clear();
        p.r.pendingLyrics.clear();
        p.r.diagnostics.erase(
            std::remove_if(p.r.diagnostics.begin(), p.r.diagnostics.end(), [](const auto &d)
                           { return d.category == InputDiagnostic::Category::LyricsOverflow; }),
            p.r.diagnostics.end());
        for (auto entry : excess)
        {
            auto queue = entry.words;
            for (auto &n : p.r.notes)
            {
                if (n.voice != entry.voice || n.segment <= entry.segment || n.degree == 0 ||
                    n.degree == 8 || n.tieBack || queue.isEmpty())
                    continue;
                auto existing = n.lyrics.value(entry.verse);
                if (!existing.isEmpty())
                    queue << existing;
                auto word = queue.takeFirst();
                if (word != "@")
                    n.lyrics[entry.verse] = word;
                else
                    n.lyrics.remove(entry.verse);
                p.r.rewriteLyrics = true;
            }
            if (!queue.isEmpty())
            {
                for (const auto &n : p.r.notes)
                    if (n.voice == entry.voice)
                        entry.segment = n.segment;
                entry.words = queue;
                p.r.overflow << entry;
                p.r.pendingLyrics << queue.join("");
                diagnostic(
                    p.r, 0,
                    QStringLiteral("%1 个歌词词位待分配：%2").arg(queue.size()).arg(queue.join("")),
                    true, InputDiagnostic::Category::LyricsOverflow);
            }
        }
        for (int index : p.openSpans)
            diagnostic(p.r, p.r.spans[index].offset,
                       QStringLiteral("%1尚未结束")
                           .arg(p.r.spans[index].kind == "slur"     ? "连奏线"
                                : p.r.spans[index].kind == "tuplet" ? "连音组"
                                                                    : "渐强／渐弱"),
                       true, InputDiagnostic::Category::Incomplete);
        QMap<int, MusicalTime> time;
        for (auto &n : p.r.notes)
        {
            n.position = time[n.voice];
            time[n.voice] += n.duration;
        }
        for (auto &b : p.r.boundaries)
        {
            b.position = {};
            if (b.after > 0 && b.after <= p.r.notes.size())
            {
                auto &n = p.r.notes[b.after - 1];
                b.position = n.position + n.duration;
            }
        }
        int first = p.r.voiceNames.isEmpty() ? 1 : p.r.voiceNames.firstKey();
        MusicalTime end;
        for (auto t : time)
            end = std::max(end, t);
        MusicalTime nominal(p.r.numerator, p.r.denominator), pos;
        QVector<TextBoundary> primary;
        for (auto b : p.r.boundaries)
            if (b.voice == first && !b.automatic)
                primary << b;
        int limit = 0;
        while (pos < end || p.r.measures.isEmpty())
        {
            for (const auto &mark : p.r.marks)
            {
                MusicalTime at;
                TextMarks::time(mark["start"], at);
                if (mark["kind"] == "time" && at == pos)
                {
                    int n = mark["numerator"].toInt(), d = mark["denominator"].toInt();
                    if (n > 0 && n <= 32 && d > 0 && d <= 32 && !(d & (d - 1)))
                        nominal = {n, d};
                }
            }
            MusicalTime next = pos + nominal;
            for (const auto &b : primary)
                if (b.position > pos && b.position < next)
                    next = b.position;
            TextMeasure measure{pos, next - pos};
            for (const auto &b : primary)
            {
                if (b.position == next)
                {
                    measure.barline = b.type;
                    measure.repeatEnd = b.type == 8 || b.type == 64;
                }
                if (b.position == pos && (b.type == 4 || b.type == 64))
                    measure.repeatStart = true;
            }
            p.r.measures << measure;
            pos = next;
            if (++limit > 10000)
            {
                diagnostic(p.r, 0, QStringLiteral("曲谱超过 10000 小节"));
                break;
            }
        }
        for (const auto &b : p.r.boundaries)
            if (b.voice != first && !b.automatic)
            {
                bool valid = b.position == MusicalTime();
                for (const auto &m : p.r.measures)
                    if (m.start + m.duration == b.position)
                        valid = true;
                if (!valid)
                    diagnostic(p.r, b.offset, QStringLiteral("小节线与第一声部的共同边界冲突"));
            }
        QMap<int, QMap<int, int>> accidentalState;
        QMap<int, int> previousMeasure;
        auto pitch = [&](TextNote &n, QMap<int, int> &state)
        {
            int fifths = p.r.fifths;
            for (const auto &mark : p.r.marks)
            {
                MusicalTime at;
                TextMarks::time(mark["start"], at);
                if (mark["kind"] == "key" && at <= n.position &&
                    (!mark["voice"].toInt() || mark["voice"].toInt() == n.voice))
                    fifths = std::clamp(mark["fifths"].toInt(), -7, 7);
            }
            int absolute = 4 * 7 + tonicStep(fifths) + std::max(0, n.degree - 1) + n.octave * 7;
            int octave = int(std::floor(absolute / 7.0));
            n.step = (absolute % 7 + 7) % 7;
            int key = keyAlter(n.step, fifths);
            n.alter = n.accidental == 99  ? (habits.value("accidentalScope").toString() == "note"
                                                 ? key
                                                 : state.value(absolute, key))
                      : n.accidental == 0 ? 0
                                          : key + n.accidental;
            state[absolute] = n.alter;
            static const int semitones[]{0, 2, 4, 5, 7, 9, 11};
            n.midi = (octave + 1) * 12 + semitones[n.step] + n.alter;
            if (n.degree >= 1 && n.degree <= 7 && (n.midi < 0 || n.midi > 127))
                diagnostic(p.r, n.offset, QStringLiteral("音高超出 MIDI 范围"));
        };
        for (auto &n : p.r.notes)
        {
            int mi = 0;
            while (mi + 1 < p.r.measures.size() && p.r.measures[mi + 1].start <= n.position)
                ++mi;
            if (previousMeasure.value(n.voice, -1) != mi)
                accidentalState[n.voice].clear();
            previousMeasure[n.voice] = mi;
            pitch(n, accidentalState[n.voice]);
            for (auto &g : n.graces)
            {
                g.position = n.position;
                pitch(g, accidentalState[n.voice]);
            }
            for (auto &g : n.chord)
            {
                g.position = n.position;
                pitch(g, accidentalState[n.voice]);
            }
        }
        QMap<int, const TextNote *> previous;
        for (const auto &n : p.r.notes)
        {
            auto *before = previous.value(n.voice, nullptr);
            if (before && before->tieForward &&
                (before->degree == 0 || before->degree == 8 || n.degree == 0 || n.degree == 8 ||
                 before->midi != n.midi))
                diagnostic(p.r, before->offset, QStringLiteral("延音连接 ^ 的前后需要同音高音符"));
            previous[n.voice] = &n;
        }
        for (auto *n : previous)
            if (n->tieForward)
                diagnostic(p.r, n->offset, QStringLiteral("延音连接 ^ 后还需要一个音符"), true,
                           InputDiagnostic::Category::Incomplete);
        for (auto &mark : p.r.marks)
        {
            int offset = mark.take("_offset").toInt();
            if (p.r.version != "1.1")
                diagnostic(p.r, offset, QStringLiteral("M 记号需要 V: 1.1"), true,
                           InputDiagnostic::Category::Notation);
            auto error = TextMarks::validate(mark, p.r);
            if (!error.isEmpty())
                diagnostic(p.r, offset, error, true, InputDiagnostic::Category::Notation);
        }
        return p.r;
    }
    catch (const std::overflow_error &)
    {
        p.r.fatal = true;
        diagnostic(p.r, 0, QStringLiteral("连音比例组合过于复杂，超出精确时值范围"));
        return p.r;
    }
}

namespace
{
struct Piece
{
    int index = -1;
    MusicalTime start, length, written;
    bool before = false, after = false;
    int denominator = 4, dots = 0;
};
QPair<int, int> durationType(MusicalTime t)
{
    for (int denominator = 1; denominator <= 1024; denominator *= 2)
        for (int dots = 0; dots <= 3; ++dots)
            if (t == MusicalTime((1LL << (dots + 1)) - 1, denominator * (1LL << dots)))
                return {denominator, dots};
    return {0, 0};
}
QString typeName(int denominator)
{
    static const QMap<int, QString> names{
        {1, "whole"}, {2, "half"},    {4, "quarter"}, {8, "eighth"},  {16, "16th"},    {32, "32nd"},
        {64, "64th"}, {128, "128th"}, {256, "256th"}, {512, "512th"}, {1024, "1024th"}};
    return names.value(denominator, "quarter");
}
} // namespace
QByteArray FanqieFormat::musicXml(const TextParseResult &r) const
{
    if (std::any_of(r.diagnostics.begin(), r.diagnostics.end(), [](const auto &d)
                    { return d.blocking && d.category == InputDiagnostic::Category::Notation; }))
        return {};
    QByteArray bytes;
    QXmlStreamWriter w(&bytes);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    auto text = [&](const QString &name, const QString &value) { w.writeTextElement(name, value); };
    w.writeStartElement("score-partwise");
    w.writeAttribute("version", "4.0");
    w.writeStartElement("work");
    text("work-title", r.title);
    w.writeEndElement();
    w.writeStartElement("identification");
    for (auto author : r.authors)
    {
        QStringList roles;
        if (author.endsWith(QStringLiteral("词曲")))
            roles = {"composer", "lyricist"};
        else if (author.endsWith(QStringLiteral("作曲")) || author.endsWith(QStringLiteral("曲")))
            roles = {"composer"};
        else if (author.endsWith(QStringLiteral("作词")) || author.endsWith(QStringLiteral("词")))
            roles = {"lyricist"};
        else
            roles = {"composer"};
        auto name = author;
        name.remove(QRegularExpression("\\s*(词曲|作词|作曲|词|曲)$"));
        for (const auto &role : roles)
        {
            w.writeStartElement("creator");
            w.writeAttribute("type", role);
            w.writeCharacters(name);
            w.writeEndElement();
        }
    }
    w.writeEndElement();
    auto voices = r.voiceNames;
    if (voices.isEmpty())
        voices[1] = QStringLiteral("旋律");
    w.writeStartElement("part-list");
    for (auto it = voices.begin(); it != voices.end(); ++it)
    {
        w.writeStartElement("score-part");
        w.writeAttribute("id", QString("P%1").arg(it.key()));
        text("part-name", it.value());
        w.writeEndElement();
    }
    w.writeEndElement();
    qint64 divisions = 1;
    for (const auto &n : r.notes)
    {
        auto d = (n.duration * MusicalTime(4)).d;
        divisions = std::lcm(divisions, d);
        if (divisions > 100000000)
            return {};
    }
    for (const auto &measure : r.measures)
    {
        divisions = std::lcm(divisions, (measure.duration * MusicalTime(4)).d);
        if (divisions > 100000000)
            return {};
    }
    for (const auto &mark : r.marks)
        for (const auto &key : {"start", "end"})
        {
            MusicalTime at;
            if (TextMarks::time(mark[key], at))
                divisions = std::lcm(divisions, (at * MusicalTime(4)).d);
            if (divisions > 100000000)
                return {};
        }
    auto units = [&](MusicalTime t)
    {
        auto v = t * MusicalTime(4 * divisions);
        return QString::number(v.n / v.d);
    };
    for (auto vi = voices.begin(); vi != voices.end(); ++vi)
    {
        w.writeStartElement("part");
        w.writeAttribute("id", QString("P%1").arg(vi.key()));
        std::set<MusicalTime> octavePositions;
        auto writeOctaves = [&](MusicalTime at)
        {
            if (!octavePositions.contains(at))
            {
                octavePositions.insert(at);
                TextMarks::octaves(w, r, vi.key(), at);
            }
        };
        for (int mi = 0; mi < r.measures.size(); ++mi)
        {
            const auto &measure = r.measures[mi];
            MusicalTime finish = measure.start + measure.duration;
            w.writeStartElement("measure");
            w.writeAttribute("number", QString::number(mi + 1));
            if (measure.duration != MusicalTime(r.numerator, r.denominator))
                w.writeAttribute("implicit", "yes");
            if (mi == 0)
            {
                w.writeStartElement("attributes");
                text("divisions", QString::number(divisions));
                w.writeStartElement("key");
                text("fifths", QString::number(r.fifths));
                text("mode", r.minor ? "minor" : "major");
                w.writeEndElement();
                w.writeStartElement("time");
                text("beats", QString::number(r.numerator));
                text("beat-type", QString::number(r.denominator));
                w.writeEndElement();
                w.writeStartElement("clef");
                text("sign", "G");
                text("line", "2");
                w.writeEndElement();
                w.writeEndElement();
            }
            TextMarks::directions(w, r, vi.key(), measure.start, finish, divisions);
            {
                w.writeStartElement("barline");
                w.writeAttribute("location", "left");
                TextMarks::barline(w, r, vi.key(), measure.start, true);
                if (measure.repeatStart)
                {
                    w.writeStartElement("repeat");
                    w.writeAttribute("direction", "forward");
                    w.writeEndElement();
                }
                w.writeEndElement();
            }
            QVector<Piece> pieces;
            MusicalTime used = measure.start;
            for (int i = 0; i < r.notes.size(); ++i)
            {
                const auto &n = r.notes[i];
                if (n.voice != vi.key() || n.position >= finish ||
                    n.position + n.duration <= measure.start)
                    continue;
                MusicalTime a = std::max(n.position, measure.start),
                            b = std::min(n.position + n.duration, finish), remaining = b - a;
                while (remaining > MusicalTime())
                {
                    MusicalTime scale = n.tuplet ? MusicalTime(n.tuplet, n.normal) : MusicalTime(1);
                    MusicalTime written = remaining * scale;
                    auto dt = durationType(written);
                    MusicalTime take = remaining;
                    if (dt.first == 0)
                    {
                        for (int d = 1; d <= 1024; d *= 2)
                            if (MusicalTime(1, d) <= written)
                            {
                                dt = {d, 0};
                                written = {1, d};
                                take = written / scale;
                                break;
                            }
                    }
                    if (dt.first == 0 || take == MusicalTime())
                        return {};
                    pieces.push_back({i, a, take, written, a > n.position,
                                      a + take < n.position + n.duration, dt.first, dt.second});
                    a += take;
                    remaining = remaining - take;
                }
            }
            auto writeNote =
                [&](const TextNote &n, const Piece &piece, bool grace, bool chord = false)
            {
                w.writeStartElement("note");
                if (n.degree == 8)
                    w.writeAttribute("print-object", "no");
                if (chord)
                    w.writeEmptyElement("chord");
                if (grace)
                {
                    w.writeStartElement("grace");
                    w.writeAttribute("slash", n.graceAfter ? "no" : "yes");
                    if (n.graceAfter)
                        w.writeAttribute("steal-time-previous", "10");
                    w.writeEndElement();
                }
                if (n.degree == 0 || n.degree == 8)
                {
                    w.writeStartElement("rest");
                    if (piece.denominator == 0)
                        w.writeAttribute("measure", "yes");
                    w.writeEndElement();
                }
                else
                {
                    w.writeStartElement("pitch");
                    text("step", QString("CDEFGAB")[n.step]);
                    if (n.alter)
                        text("alter", QString::number(n.alter));
                    int natural = n.midi - n.alter;
                    for (const auto &mark : r.marks)
                        if (mark["kind"] == "ottava" &&
                            (!mark["voice"].toInt() || mark["voice"].toInt() == vi.key()))
                        {
                            MusicalTime a, b;
                            TextMarks::time(mark["start"], a);
                            TextMarks::time(mark["end"], b);
                            auto at = grace ? n.position : piece.start;
                            if (at >= a && at < b)
                                natural += 12 * mark["octaves"].toInt();
                        }
                    text("octave", QString::number(natural / 12 - 1));
                    w.writeEndElement();
                }
                if (!grace)
                    text("duration", units(piece.length));
                if ((piece.before || n.tieBack) && n.degree != 0 && n.degree != 8)
                {
                    w.writeStartElement("tie");
                    w.writeAttribute("type", "stop");
                    w.writeEndElement();
                }
                if ((piece.after || n.tieForward) && n.degree != 0 && n.degree != 8)
                {
                    w.writeStartElement("tie");
                    w.writeAttribute("type", "start");
                    w.writeEndElement();
                }
                if (piece.denominator > 0)
                    text("type", typeName(piece.denominator));
                for (int d = 0; d < piece.dots; ++d)
                    w.writeEmptyElement("dot");
                if (n.accidental != 99 && !piece.before)
                    text("accidental", n.alter > 0 ? "sharp" : n.alter < 0 ? "flat" : "natural");
                if (n.degree == 9)
                    text("notehead", "x");
                if (n.tuplet && !grace)
                {
                    w.writeStartElement("time-modification");
                    text("actual-notes", QString::number(n.tuplet));
                    text("normal-notes", QString::number(n.normal));
                    w.writeEndElement();
                }
                w.writeStartElement("notations");
                if (piece.before || n.tieBack)
                {
                    w.writeStartElement("tied");
                    w.writeAttribute("type", "stop");
                    w.writeEndElement();
                }
                if (piece.after || n.tieForward)
                {
                    w.writeStartElement("tied");
                    w.writeAttribute("type", "start");
                    w.writeEndElement();
                }
                if (!grace && !chord && !piece.before)
                    TextMarks::notations(w, r, n.voice, n.position);
                if (!grace && !chord)
                    for (int si = 0; si < r.spans.size(); ++si)
                    {
                        const auto &s = r.spans[si];
                        if (!s.closed || (s.kind != "slur" && s.kind != "tuplet"))
                            continue;
                        bool start = piece.index == s.first && !piece.before,
                             stop = piece.index == s.last && !piece.after;
                        if (start || stop)
                        {
                            w.writeStartElement(s.kind);
                            w.writeAttribute("type", start ? "start" : "stop");
                            w.writeAttribute("number", QString::number(si % 16 + 1));
                            if (s.kind == "tuplet")
                                w.writeAttribute("bracket", "yes");
                            w.writeEndElement();
                        }
                    }
                w.writeEndElement();
                if (!grace && !chord && !piece.before && !n.tieBack)
                    for (auto it = n.lyrics.begin(); it != n.lyrics.end(); ++it)
                    {
                        w.writeStartElement("lyric");
                        w.writeAttribute("number", QString::number(it.key() + 1));
                        text("text", it.value());
                        w.writeEndElement();
                    }
                w.writeEndElement();
            };
            auto direction = [&](const QString &kind, const QString &value, int number = 1)
            {
                w.writeStartElement("direction");
                w.writeStartElement("direction-type");
                if (kind == "dynamic")
                {
                    w.writeStartElement("dynamics");
                    w.writeEmptyElement(value);
                    w.writeEndElement();
                }
                else if (kind == "words")
                    text("words", value);
                else
                {
                    w.writeStartElement("wedge");
                    w.writeAttribute("type", value);
                    w.writeAttribute("number", QString::number(number));
                    w.writeEndElement();
                }
                w.writeEndElement();
                w.writeEndElement();
            };
            auto fillRest = [&](MusicalTime remaining, bool wholeMeasure)
            {
                TextNote rest;
                rest.degree = 8;
                if (wholeMeasure)
                {
                    Piece filler;
                    filler.length = remaining;
                    filler.denominator = 0;
                    writeNote(rest, filler, false);
                    return;
                }
                while (remaining > MusicalTime())
                {
                    auto dt = durationType(remaining);
                    MusicalTime length = remaining;
                    if (!dt.first)
                        for (int d = 1; d <= 1024; d *= 2)
                            if (MusicalTime(1, d) <= remaining)
                            {
                                dt = {d, 0};
                                length = {1, d};
                                break;
                            }
                    if (!dt.first)
                    {
                        w.writeStartElement("forward");
                        text("duration", units(remaining));
                        w.writeEndElement();
                        break;
                    }
                    Piece filler;
                    filler.length = length;
                    filler.denominator = dt.first;
                    filler.dots = dt.second;
                    writeNote(rest, filler, false);
                    remaining = remaining - length;
                }
            };
            for (const auto &piece : pieces)
            {
                if (piece.start > used)
                    fillRest(piece.start - used, false);
                const auto &n = r.notes[piece.index];
                writeOctaves(piece.start);
                if (!piece.before)
                {
                    for (const auto &d : n.dynamics)
                        direction("dynamic", d);
                    if (!n.annotation.isEmpty())
                        direction("words", n.annotation);
                    for (int si = 0; si < r.spans.size(); ++si)
                    {
                        const auto &s = r.spans[si];
                        if (s.closed && s.first == piece.index &&
                            (s.kind == "crescendo" || s.kind == "diminuendo"))
                            direction("wedge", s.kind, si % 16 + 1);
                    }
                    for (const auto &g : n.graces)
                        if (!g.graceAfter)
                        {
                            auto dt = durationType(g.written);
                            writeNote(g, {-1, {}, {}, g.written, false, false, dt.first, dt.second},
                                      true);
                        }
                }
                writeNote(n, piece, false);
                for (auto pitch : n.chord)
                {
                    pitch.tieForward = n.tieForward;
                    pitch.tieBack = n.tieBack;
                    pitch.tuplet = n.tuplet;
                    pitch.normal = n.normal;
                    writeNote(pitch, piece, false, true);
                }
                if (!piece.after)
                {
                    for (const auto &g : n.graces)
                        if (g.graceAfter)
                        {
                            auto dt = durationType(g.written);
                            writeNote(g, {-1, {}, {}, g.written, false, false, dt.first, dt.second},
                                      true);
                        }
                    for (int si = 0; si < r.spans.size(); ++si)
                    {
                        const auto &s = r.spans[si];
                        if (s.closed && s.last == piece.index &&
                            (s.kind == "crescendo" || s.kind == "diminuendo"))
                            direction("wedge", "stop", si % 16 + 1);
                    }
                }
                used = piece.start + piece.length;
                writeOctaves(used);
            }
            if (used < finish)
                fillRest(finish - used, used == measure.start);
            {
                w.writeStartElement("barline");
                w.writeAttribute("location", "right");
                TextMarks::barline(w, r, vi.key(), finish, false);
                if (measure.repeatEnd)
                {
                    w.writeStartElement("repeat");
                    w.writeAttribute("direction", "backward");
                    for (const auto &mark : r.marks)
                    {
                        MusicalTime at;
                        TextMarks::time(mark["start"], at);
                        if (mark["kind"] == "repeat" && at == finish)
                            w.writeAttribute("times", QString::number(mark["count"].toInt(2)));
                    }
                    w.writeEndElement();
                }
                else
                    text("bar-style", measure.barline == 32 ? "light-heavy" : "regular");
                w.writeEndElement();
            }
            w.writeEndElement();
        }
        w.writeEndElement();
    }
    w.writeEndElement();
    w.writeEndDocument();
    return bytes;
}

QStringList FanqieFormat::lyricTokens(const QString &text) { return syllables(text); }
QString FanqieFormat::token(const TextNote &n)
{
    QString t = QString::number(n.degree);
    if (!n.chord.isEmpty())
    {
        auto root = n;
        root.chord.clear();
        root.slashes = root.extensions = root.dots = 0;
        root.tieForward = false;
        QStringList pitches{token(root)};
        for (auto pitch : n.chord)
        {
            pitch.slashes = pitch.extensions = pitch.dots = 0;
            pitch.tieForward = false;
            pitches << token(pitch);
        }
        t = "{" + pitches.join(' ') + "}";
    }
    t += QString(n.slashes, '/');
    t += QString(n.extensions, '-');
    t += QString(n.dots, '.');
    if (n.chord.isEmpty())
        t += QString(std::abs(n.octave), n.octave >= 0 ? '\'' : ',');
    if (n.chord.isEmpty() && n.accidental != 99)
        t += n.accidental > 0 ? '#' : n.accidental < 0 ? '$' : '=';
    if (n.tieForward)
        t += '^';
    return t;
}
QString FanqieFormat::fromSnapshot(const ScoreSnapshot &s, bool *protectedContent) const
{
    bool protect = false;
    for (const auto &mark : s.marks)
        if (!QStringList{"dynamic", "text", "slur", "hairpin", "tuplet", "tie", "key", "minor-key",
                         "time"}
                    .contains(mark.kind) &&
                !TextMarks::encodable(mark) ||
            ((mark.kind == "key" || mark.kind == "minor-key" || mark.kind == "time") &&
             mark.tick != 0))
            protect = true;
    QMap<QString, QString> openings, closings;
    for (const auto &mark : s.marks)
        if (mark.kind == "slur" || mark.kind == "hairpin")
        {
            const EventData *first = nullptr, *last = nullptr;
            for (const auto &measure : s.measures)
                for (const auto &event : measure.events)
                    if (!event.grace && event.track == mark.track)
                    {
                        if (event.tick == mark.tick)
                            first = &event;
                        if (mark.kind == "slur" ? event.tick <= mark.endTick
                                                : event.tick + event.duration <= mark.endTick)
                            last = &event;
                    }
            if (first && last)
            {
                openings[first->id] += mark.kind == "slur" ? "(" : mark.text;
                closings[last->id].prepend(mark.kind == "slur" ? ")" : "!");
            }
        }
    QString out = "V: 1.0\nB: " + s.title + "\n";
    for (const auto &b : s.subtitles)
        out += "B: " + b + "\n";
    for (const auto &z : s.authors)
        out += "Z: " + z + "\n";
    static const QStringList major{"C$", "G$", "D$", "A$", "E$", "B$", "F", "C",
                                   "G",  "D",  "A",  "E",  "B",  "F#", "C#"};
    static const QStringList minor{"A$", "E$", "B$", "F",  "C",  "G",  "D", "A",
                                   "E",  "B",  "F#", "C#", "G#", "D#", "A#"};
    out +=
        "D: " + (s.minor ? minor : major).value(s.keyFifths + 7, "C") + (s.minor ? "m" : "") + "\n";
    out += QString("P: %1/%2\n")
               .arg(s.measures.isEmpty() ? 4 : s.measures[0].numerator)
               .arg(s.measures.isEmpty() ? 4 : s.measures[0].denominator);
    auto appendGap = [&](MusicalTime gap)
    {
        // A voice may have no event in a pickup or a silent measure. Keep its
        // absolute timeline rather than shifting its following music to tick 0.
        int guard = 0;
        while (gap > MusicalTime() && ++guard <= 4096)
        {
            int denominator = 1;
            while (denominator <= 1024 && MusicalTime(1, denominator) > gap)
                denominator *= 2;
            if (denominator > 1024)
            {
                protect = true;
                break;
            }
            TextNote rest;
            rest.degree = 8;
            rest.slashes = std::max(0, int(std::log2(denominator)) - 2);
            rest.extensions = denominator < 4 ? 4 / denominator - 1 : 0;
            out += token(rest) + " ";
            gap = gap - MusicalTime(1, denominator);
        }
    };
    int number = 0;
    for (const auto &v : s.voices)
    {
        if (v.percussion)
        {
            protect = true;
            continue;
        }
        ++number;
        out += QString("Q%1: ").arg(number);
        QMap<int, QString> lyrics;
        QString activeTuplet;
        int verseCount = s.lyricRows.value(v.track);
        for (const auto &m : s.measures)
            for (const auto &e : m.events)
                if (e.track == v.track)
                    for (const auto &l : e.lyrics)
                        verseCount = std::max(verseCount, l.verse + 1);
        auto limit = s.sourceEnds.value(
            v.track, s.measures.isEmpty()
                         ? MusicalTime()
                         : MusicalTime(s.measures.last().tick + s.measures.last().duration, 1920));
        for (int mi = 0; mi < s.measures.size(); ++mi)
        {
            const auto &m = s.measures[mi];
            QMap<int, int> accidentalState;
            auto spelling = [&](const NoteData &note, int fifths, TextNote &token)
            {
                int key = keyAlter(note.step, fifths), absolute = note.octave * 7 + note.step;
                int delta = note.alter - key;
                if (note.alter == 0 && (key != 0 || accidentalState.value(absolute, key) != 0))
                    token.accidental = 0;
                else if (delta != 0)
                {
                    if (std::abs(delta) > 1)
                        protect = true;
                    token.accidental = delta;
                }
                else if (accidentalState.value(absolute, key) != note.alter)
                {
                    // The current grammar cannot explicitly restore an altered
                    // key-signature pitch after a natural in the same measure.
                    protect = true;
                }
                accidentalState[absolute] = note.alter;
            };
            if (MusicalTime(m.tick, 1920) >= limit)
                break;
            if (m.repeatStart && (mi == 0 || !s.measures[mi - 1].repeatEnd))
                out += "|: ";
            MusicalTime cursor(m.tick, 1920);
            for (const auto &e : m.events)
                if (e.track == v.track)
                {
                    if (e.keyFifths != s.keyFifths)
                        protect = true;
                    if (e.grace)
                        continue;
                    auto position =
                        e.length == MusicalTime() ? MusicalTime(e.tick, 1920) : e.position;
                    if (position >= limit)
                        continue;
                    if (position > cursor)
                    {
                        if (!activeTuplet.isEmpty())
                        {
                            out += ") ";
                            activeTuplet.clear();
                        }
                        appendGap(position - cursor);
                    }
                    cursor = position +
                             (e.length == MusicalTime() ? MusicalTime(e.duration, 1920) : e.length);
                    if (activeTuplet != e.tupletGroup)
                    {
                        if (!activeTuplet.isEmpty())
                            out += ") ";
                        if (!e.tupletGroup.isEmpty())
                            out += QString("(y%1:%2 ").arg(e.tuplet).arg(e.tupletNormal);
                        activeTuplet = e.tupletGroup;
                    }
                    TextNote n;
                    n.degree = e.rest ? (e.visible ? 0 : 8) : e.rhythm ? 9 : 1;
                    if (!e.notes.isEmpty())
                    {
                        auto note = e.notes[0];
                        int relative = note.octave * 7 + note.step - 28 - tonicStep(e.keyFifths);
                        n.degree = e.rhythm ? 9 : (relative % 7 + 7) % 7 + 1;
                        n.octave = int(std::floor(relative / 7.0));
                        n.tieForward = note.tieStart;
                        n.tieBack = note.tieEnd;
                        spelling(note, e.keyFifths, n);
                        for (int pi = 1; pi < e.notes.size(); ++pi)
                        {
                            auto pitch = e.notes[pi];
                            TextNote member;
                            int relative =
                                pitch.octave * 7 + pitch.step - 28 - tonicStep(e.keyFifths);
                            member.degree = (relative % 7 + 7) % 7 + 1;
                            member.octave = int(std::floor(relative / 7.0));
                            spelling(pitch, e.keyFifths, member);
                            n.chord << member;
                        }
                    }
                    if (e.denominator >= 4)
                        n.slashes = int(std::log2(e.denominator)) - 2;
                    else
                        n.extensions = e.denominator == 0 ? std::max(0, int(e.duration / 480) - 1)
                                                          : 4 / std::max(1, e.denominator) - 1;
                    n.dots = e.dots;
                    if (e.denominator == 0)
                    {
                        auto length = cursor - position;
                        bool exact = false;
                        for (int denominator = 1; denominator <= 1024 && !exact; denominator *= 2)
                            for (int dots = 0; dots <= 3; ++dots)
                                if (length == MusicalTime((1LL << (dots + 1)) - 1,
                                                          denominator * (1LL << dots)))
                                {
                                    n.slashes = std::max(0, int(std::log2(denominator)) - 2);
                                    n.extensions = denominator < 4 ? 4 / denominator - 1 : 0;
                                    n.dots = dots;
                                    exact = true;
                                    break;
                                }
                    }
                    out += openings.value(e.id) + token(n);
                    for (bool after : {false, true})
                    {
                        QString grace;
                        for (const auto &measure : s.measures)
                            for (const auto &g : measure.events)
                                if (g.grace && g.track == e.track && g.tick == e.tick &&
                                    g.graceAfter == after && !g.notes.isEmpty())
                                {
                                    const auto &pitch = g.notes.first();
                                    TextNote small;
                                    int relative =
                                        pitch.octave * 7 + pitch.step - 28 - tonicStep(e.keyFifths);
                                    small.degree = (relative % 7 + 7) % 7 + 1;
                                    small.octave = int(std::floor(relative / 7.0));
                                    spelling(pitch, e.keyFifths, small);
                                    if (g.denominator >= 8)
                                        small.slashes = int(std::log2(g.denominator)) - 3;
                                    else
                                        protect = true;
                                    grace += token(small);
                                }
                        if (!grace.isEmpty())
                            out += "[" + QString(after ? "h" : "") + grace + "]";
                    }
                    for (const auto &mark : s.marks)
                        if (mark.track == e.track && mark.tick == e.tick)
                        {
                            if (mark.kind == "dynamic")
                                out += "&" + mark.text;
                            else if (mark.kind == "text")
                            {
                                auto text = mark.text;
                                text.replace("\\", "\\\\").replace("\"", "\\\"");
                                out += "\"" + text + "\"";
                            }
                        }
                    out += closings.value(e.id) + " ";
                    if (!e.rest && !n.tieBack)
                    {
                        for (int verse = 0; verse < verseCount; ++verse)
                        {
                            QString word = "@";
                            for (const auto &l : e.lyrics)
                                if (l.verse == verse)
                                    word = l.text;
                            QString joined;
                            auto chars = syllables(word);
                            joined = chars.join("~");
                            lyrics[verse] += joined + " ";
                        }
                    }
                }
            auto end = std::min(limit, MusicalTime(m.tick + m.duration, 1920));
            if (cursor < end)
            {
                if (!activeTuplet.isEmpty())
                {
                    out += ") ";
                    activeTuplet.clear();
                }
                appendGap(end - cursor);
            }
            if (end == MusicalTime(m.tick + m.duration, 1920))
                out += (m.repeatEnd
                            ? (mi + 1 < s.measures.size() && s.measures[mi + 1].repeatStart ? ":|:"
                                                                                            : ":|")
                        : m.barline == 32 ? "||"
                                          : "|") +
                       QString(" ");
        }
        if (!activeTuplet.isEmpty())
            out += ") ";
        out += '\n';
        for (auto it = lyrics.begin(); it != lyrics.end(); ++it)
            out += QString("C%1: %2\n").arg(number).arg(it.value());
    }
    auto markLines = TextMarks::lines(s);
    bool chords = false;
    for (const auto &m : s.measures)
        for (const auto &e : m.events)
            chords |= e.notes.size() > 1;
    if (!markLines.isEmpty() || chords)
    {
        out.replace(0, 6, "V: 1.1");
        out += markLines;
    }
    if (protectedContent)
        *protectedContent = protect;
    if (protect)
        out += "# "
               "受保护对象：此导入曲谱含当前文字语法无法完整表示的内容；继续使用谱面编辑，原始数据"
               "始终保留。\n";
    return out;
}
} // namespace jianpu
