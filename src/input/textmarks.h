// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "textformat.h"
class QXmlStreamWriter;
namespace jianpu
{
class TextMarks
{
  public:
    static bool time(const QJsonValue &, MusicalTime &);
    static QString fraction(MusicalTime);
    static QString validate(const QJsonObject &, const TextParseResult &);
    static QStringList kinds();
    static bool encodable(const MarkData &);
    static QString lines(const ScoreSnapshot &);
    static QString replaceLines(const QString &, const ScoreSnapshot &);
    static void directions(QXmlStreamWriter &, const TextParseResult &, int voice,
                           MusicalTime start, MusicalTime finish, qint64 divisions);
    static void octaves(QXmlStreamWriter &, const TextParseResult &, int voice, MusicalTime);
    static void notations(QXmlStreamWriter &, const TextParseResult &, int voice, MusicalTime);
    static void barline(QXmlStreamWriter &, const TextParseResult &, int voice,
                        MusicalTime position, bool left);
};
} // namespace jianpu
