// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ai/aiconfig.h"
#include <QObject>
#include <QStringList>
class QWidget;
namespace jianpu
{
class MainWindow;
class AiRecognitionUi
{
  public:
    static void settings(QWidget *parent);
    static void chooseImages(QWidget *parent);
    static MainWindow *recognize(const AiConfig &, const QStringList &);
};
} // namespace jianpu
