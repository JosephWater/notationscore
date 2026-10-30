// SPDX-License-Identifier: GPL-3.0-only
#include "layoutsettings.h"
#include <QCoreApplication>
#include <QFontDatabase>
#include <algorithm>
static void initJianpuFonts() { Q_INIT_RESOURCE(jianpu_fonts); }
namespace jianpu
{
QJsonObject LayoutSettings::defaults(const QString &name)
{
    LayoutSettings s;
    s.preset = name;
    if (name == "modern")
    {
        s.chineseFont = "Noto Sans CJK SC";
        s.lineWidth = 1;
        s.systemGap = 36;
        s.voiceGap = 20;
    }
    if (name == "children")
    {
        s.chineseFont = "Noto Sans CJK SC";
        s.notePt = 22;
        s.lyricPt = 16;
        s.titlePt = 28;
        s.systemGap = 44;
        s.voiceGap = 24;
        s.noteGap = 1.15;
    }
    return s.toJson();
}
QJsonObject LayoutSettings::toJson() const
{
    QJsonObject o;
#define S(x) o[#x] = x
    S(preset);
    S(paper);
    S(widthMm);
    S(heightMm);
    S(leftMm);
    S(rightMm);
    S(topMm);
    S(bottomMm);
    S(notePt);
    S(lyricPt);
    S(titlePt);
    S(subtitlePt);
    S(authorPt);
    S(noteGap);
    S(measureGap);
    S(voiceGap);
    S(systemGap);
    S(verseGap);
    S(lyricGap);
    S(lineWidth);
    S(headerGap);
    S(measuresPerLine);
    S(chineseFont);
    S(numberFont);
    S(titleFont);
    S(subtitleFont);
    S(authorFont);
    S(lyricFont);
    S(subtitleBold);
    S(authorBold);
    S(noteBold);
    S(lyricBold);
    S(titleAlign);
    S(authorAlign);
    S(landscape);
    S(sourceBreaks);
    S(showVoices);
    S(showMeasures);
    S(showPages);
    S(continuationTitle);
    S(stretchLast);
    S(titleBold);
#undef S
    return o;
}
LayoutSettings LayoutSettings::fromJson(const QJsonObject &root)
{
    auto o = root.contains("layout") ? root["layout"].toObject() : root;
    LayoutSettings s;
    auto merged = defaults(o.value("preset").toString("classic"));
    for (auto it = o.begin(); it != o.end(); ++it)
        merged[it.key()] = it.value();
#define D(x, lo, hi) s.x = std::clamp(merged[#x].toDouble(s.x), double(lo), double(hi))
#define T(x) s.x = merged[#x].toString(s.x)
#define B(x) s.x = merged[#x].toBool(s.x)
    T(preset);
    T(paper);
    T(chineseFont);
    T(numberFont);
    T(titleFont);
    T(subtitleFont);
    T(authorFont);
    T(lyricFont);
    T(titleAlign);
    T(authorAlign);
    D(widthMm, 100, 1000);
    D(heightMm, 100, 1000);
    D(leftMm, 5, 80);
    D(rightMm, 5, 80);
    D(topMm, 5, 80);
    D(bottomMm, 5, 80);
    D(notePt, 8, 48);
    D(lyricPt, 6, 36);
    D(titlePt, 10, 60);
    D(subtitlePt, 8, 36);
    D(authorPt, 6, 30);
    D(noteGap, 0.5, 3);
    D(measureGap, 0.5, 3);
    D(voiceGap, 0, 100);
    D(systemGap, 0, 200);
    D(verseGap, 1, 3);
    D(lyricGap, 0, 80);
    D(lineWidth, 0.4, 4);
    D(headerGap, 0, 60);
    s.measuresPerLine = std::clamp(merged["measuresPerLine"].toInt(4), 1, 32);
    B(subtitleBold);
    B(authorBold);
    B(noteBold);
    B(lyricBold);
    B(landscape);
    B(sourceBreaks);
    B(showVoices);
    B(showMeasures);
    B(showPages);
    B(continuationTitle);
    B(stretchLast);
    B(titleBold);
#undef D
#undef T
#undef B
    if (s.paper == "A4")
    {
        s.widthMm = 210;
        s.heightMm = 297;
    }
    if (s.paper == "A5")
    {
        s.widthMm = 148;
        s.heightMm = 210;
    }
    if (s.paper == "Letter")
    {
        s.widthMm = 215.9;
        s.heightMm = 279.4;
    }
    return s;
}
QFont LayoutSettings::font(double px, bool bold, bool number) const
{
    static const bool loaded = []
    {
        initJianpuFonts();
        for (const auto &path : {":/fonts/assets/fonts/NotoSerifCJKsc-Regular.otf",
                                 ":/fonts/assets/fonts/NotoSansCJKsc-Regular.otf",
                                 ":/fonts/assets/fonts/DejaVuSans.ttf"})
            QFontDatabase::addApplicationFont(path);
        return true;
    }();
    Q_UNUSED(loaded);
    QFont f(number ? numberFont : chineseFont);
    f.setPointSizeF(px * 0.75);
    f.setBold(bold);
    return f;
}
} // namespace jianpu
