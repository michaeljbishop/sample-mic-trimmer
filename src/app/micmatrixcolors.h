#ifndef MICMATRIXCOLORS_H
#define MICMATRIXCOLORS_H

#include <QColor>
#include <QList>
#include <QPair>
#include "palette.h"
#include <algorithm>
#include <cmath>

// The matrix colours, one dark set derived from Palette (the app always runs dark, see darktheme.h), all
// defined here and nowhere else; the painters never use the system palette. tst_micmatrixview checks every pair for WCAG contrast.
namespace MatrixColors {
inline QColor mix(const QColor &a, const QColor &b, double toB)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * toB, a.greenF() + (b.greenF() - a.greenF()) * toB,
                            a.blueF() + (b.blueF() - a.blueF()) * toB);
}

inline const QColor base = Palette::background;     // gaps between tiles, viewport, total column
inline const QColor off = Palette::white04;          // not wanted tile
inline const QColor offText = Palette::alto;
inline const QColor offSize = Palette::body;         // size text on a dark tile
inline const QColor on = Palette::selection;         // wanted tile; name cell / heading with everything wanted
inline const QColor onText = Palette::background;
inline const QColor half = mix(on, off, 0.55);              // only some sections wanted: "on" damped toward "off"
inline const QColor halfText = Palette::alto;
inline const QColor partly = mix(Palette::amber, off, 0.2); // partly present: a calmer amber
inline const QColor partlyText = Palette::background;
inline const QColor locked = Palette::boulder;       // always on
inline const QColor lockedText = Palette::white;
inline const QColor group = Palette::white10;        // group header rows
inline const QColor groupText = Palette::white;
inline const QColor totalText = Palette::body;
inline const QColor ringOuter = Palette::white;      // current cell: 1px white outside, 1px dark inside
inline const QColor ringInner = Palette::background;

// Every (fill, text) pair that CellDelegate::paint() or MicMatrixHeader::paintSection() draws needs a line here:
// tst_micmatrixview checks each for WCAG contrast >= 4.5.
inline const QList<QPair<QColor, QColor>> &pairs() // (fill, text)
{
    static const QList<QPair<QColor, QColor>> p{{off, offText}, {off, offSize}, {on, onText}, {half, halfText},
                                                {partly, partlyText}, {locked, lockedText}, {group, groupText},
                                                {base, totalText}};
    return p;
}

// The legend under the matrix: a swatch colour and what it means, in the order of the states.
inline const QList<QPair<QColor, QString>> &legend()
{
    static const QList<QPair<QColor, QString>> l{{on, QStringLiteral("Included")},
                                                 {half, QStringLiteral("Some included")},
                                                 {partly, QStringLiteral("Partly present (Apply completes it)")},
                                                 {locked, QStringLiteral("Always on")},
                                                 {off, QStringLiteral("Not included")}};
    return l;
}

// The tile fills the current-cell outline is drawn on: at least one of the two ring colours must stand out (>= 3:1).
inline const QList<QColor> &tileFills()
{
    static const QList<QColor> f{off, on, half, partly, locked};
    return f;
}

// WCAG 2 contrast ratio of two opaque colours.
inline double contrast(const QColor &a, const QColor &b)
{
    auto lum = [](const QColor &c) {
        auto ch = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
    };
    const double la = lum(a), lb = lum(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
} // namespace MatrixColors

#endif // MICMATRIXCOLORS_H
