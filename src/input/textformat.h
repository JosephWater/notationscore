// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "core/scoretypes.h"
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <memory>

namespace jianpu
{
struct SourceChange
{
    int offset = 0, removed = 0, added = 0;
};
struct SourceChanges
{
    QString before;
    QVector<SourceChange> edits;
};
struct InputDiagnostic
{
    enum class Category
    {
        Syntax,
        Incomplete,
        LyricsOverflow,
        UnassignedLyrics
    };
    int offset = 0, length = 1, line = 1, column = 1;
    QString message;
    bool blocking = true;
    Category category = Category::Syntax;
};
struct TextNote
{
    QString id, raw;
    int offset = 0, length = 1, voice = 1, segment = 0;
    int degree = 1, octave = 0, accidental = 99, slashes = 0, extensions = 0, dots = 0;
    int step = 0, alter = 0, midi = 60;
    MusicalTime duration{1, 4}, written{1, 4}, position;
    int tuplet = 0, normal = 0, tupletId = -1;
    QVector<TextNote> graces;
    bool graceAfter = false, sourceBreak = false, tieForward = false, tieBack = false;
    QStringList dynamics;
    QString annotation;
    QMap<int, QString> lyrics;
    QSet<int> lyricSkips;
};
struct TextSpan
{
    QString kind;
    int first = -1, last = -1, offset = 0;
    bool closed = false;
    int voice = 1;
};
struct TextBoundary
{
    int voice = 1, after = 0, type = 1, offset = 0;
    MusicalTime position;
    bool automatic = false;
};
struct TextMeasure
{
    MusicalTime start, duration;
    int barline = 1;
    bool repeatStart = false, repeatEnd = false;
};
struct TextParseResult
{
    struct Overflow
    {
        int voice = 1, segment = 0, verse = 0;
        QStringList words;
    };
    QString source, title = QStringLiteral("未命名曲谱");
    QStringList subtitles, authors;
    int fifths = 0, numerator = 4, denominator = 4;
    bool minor = false;
    bool fatal = false;
    bool rewriteLyrics = false;
    QVector<TextNote> notes;
    QVector<TextSpan> spans;
    QVector<TextBoundary> boundaries;
    QVector<TextMeasure> measures;
    QVector<InputDiagnostic> diagnostics;
    QMap<int, QString> voiceNames;
    QMap<int, int> verseRows;
    QStringList pendingLyrics;
    QVector<Overflow> overflow;
    bool hasErrors() const;
    bool canProject() const;
    QJsonArray overflowJson() const;
    void restoreOverflow(const QJsonArray &);
    QStringList messages() const;
};
class TextFormatAdapter
{
  public:
    virtual ~TextFormatAdapter() = default;
    virtual QString formatId() const = 0;
    virtual TextParseResult parse(const QString &, const QJsonObject &habits = {}) const = 0;
    virtual QByteArray musicXml(const TextParseResult &) const = 0;
    virtual QString fromSnapshot(const ScoreSnapshot &, bool *protectedContent = nullptr) const = 0;
    virtual QString alignedSource(const TextParseResult &, const QVector<MusicalTime> &) const = 0;
    static std::unique_ptr<TextFormatAdapter> create(const QString &id = "fanqie-1.0");
};
class FanqieFormat final : public TextFormatAdapter
{
  public:
    QString formatId() const override { return "fanqie-1.0"; }
    TextParseResult parse(const QString &, const QJsonObject &habits = {}) const override;
    QByteArray musicXml(const TextParseResult &) const override;
    QString fromSnapshot(const ScoreSnapshot &, bool *protectedContent = nullptr) const override;
    static QString token(const TextNote &);
    static QStringList lyricTokens(const QString &);
    // Replace musical fields while retaining headers, comments and unknown fields.
    static QString replaceMusic(const QString &source, const QString &projection);
    // Reflow musical fields only; retain all header fields and comments. Tied
    // continuations use ^ so measure/system boundaries remain editable text.
    QString alignedSource(const TextParseResult &,
                          const QVector<MusicalTime> &systemStarts) const override;
};
} // namespace jianpu
