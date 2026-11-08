#pragma once

#include <QString>
#include <QStringList>

#include <QtGlobal>

// The D450 prints at 203 dots per inch. A TSPL BITMAP row is 8 dots per
// byte, so the width is rounded down to a multiple of 8. 4 x 6 in is
// 812 x 1218 dots before that round-down (808 x 1218 on the wire).

inline int labelDotsFromMm(double mm)
{
    return qRound(mm * (203.0 / 25.4));
}

inline int labelWidthDots(double mm)
{
    const int dots = qMax(0, labelDotsFromMm(mm));
    return dots - (dots % 8);
}

inline int labelHeightDots(double mm)
{
    return qMax(0, labelDotsFromMm(mm));
}

struct LabelPreset {
    const char *id;
    double widthMm;
    double heightMm;
};

// Inch stock is stored in millimetres (inches * 25.4) so the SIZE command
// and the dot math share one number.
inline constexpr LabelPreset kLabelPresets[] = {
    {"4x6", 101.6, 152.4},
    {"4x4", 101.6, 101.6},
    {"4x3", 101.6, 76.2},
    {"4x2", 101.6, 50.8},
    {"3x2", 76.2, 50.8},
    {"2x1", 50.8, 25.4},
    {"50x30mm", 50.0, 30.0},
};

inline constexpr int kLabelPresetCount = int(sizeof(kLabelPresets) / sizeof(kLabelPresets[0]));
inline constexpr int kCustomSizeIndex = kLabelPresetCount;

// "4x6" and "4x6in" are inches. "50x30mm" is millimetres. Preset ids win.
inline bool parseLabelSize(const QString &text, double *widthMm, double *heightMm)
{
    QString s = text.trimmed().toLower();
    for (const auto &preset : kLabelPresets) {
        if (s == QLatin1String(preset.id)) {
            *widthMm = preset.widthMm;
            *heightMm = preset.heightMm;
            return true;
        }
    }

    const bool millimetres = s.endsWith(QLatin1String("mm"));
    if (millimetres)
        s.chop(2);
    if (s.endsWith(QLatin1String("in")))
        s.chop(2);

    const QStringList parts = s.split(QLatin1Char('x'));
    if (parts.size() != 2)
        return false;

    bool okW = false;
    bool okH = false;
    double w = parts.at(0).toDouble(&okW);
    double h = parts.at(1).toDouble(&okH);
    if (!okW || !okH || w <= 0.0 || h <= 0.0)
        return false;
    if (!millimetres) {
        w *= 25.4;
        h *= 25.4;
    }
    *widthMm = w;
    *heightMm = h;
    return true;
}
