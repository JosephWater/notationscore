// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QFont>
#include <QJsonObject>

namespace jianpu
{
struct LayoutSettings
{
    QString preset = "classic", paper = "A4";
    double widthMm = 210, heightMm = 297, leftMm = 18, rightMm = 18, topMm = 18, bottomMm = 18;
    double notePt = 18, lyricPt = 12, titlePt = 26, subtitlePt = 13, authorPt = 11;
    double noteGap = 1, measureGap = 1, voiceGap = 14, systemGap = 24, verseGap = 1.25,
           lyricGap = 10;
    double lineWidth = 1.2, headerGap = 12;
    int measuresPerLine = 4;
    QString chineseFont = "Noto Serif CJK SC", numberFont = "DejaVu Sans";
    QString titleFont, subtitleFont, authorFont, lyricFont;
    bool subtitleBold = false, authorBold = false, noteBold = true, lyricBold = false;
    QString titleAlign = "center", authorAlign = "right";
    bool landscape = false, sourceBreaks = true, showVoices = true, showMeasures = true;
    bool showPages = true, continuationTitle = true, stretchLast = false, titleBold = true;
    static LayoutSettings fromJson(const QJsonObject &);
    static QJsonObject defaults(const QString &preset = "classic");
    QJsonObject toJson() const;
    QFont font(double pixels, bool bold = false, bool number = false) const;
};
} // namespace jianpu
