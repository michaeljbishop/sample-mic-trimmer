#ifndef PALETTE_H
#define PALETTE_H

#include <QColor>

// The app's colours: one dark set. The matrix colours (micmatrixcolors.h) and the theme (darktheme.cpp) derive from it.
namespace Palette {
inline const QColor background(0x23, 0x23, 0x23);
inline const QColor thunder(0x33, 0x33, 0x33);
inline const QColor white(0xff, 0xff, 0xff);       // headings
inline const QColor black(0x00, 0x00, 0x00);
inline const QColor body(0xa7, 0xa7, 0xa7);        // body text
inline const QColor alto(0xe0, 0xe0, 0xe0);
inline const QColor boulder(0x76, 0x76, 0x76);
inline const QColor emperor(0x4f, 0x4f, 0x4f);
// The selection / highlight colour of the app, light enough that the dark text on it passes WCAG 4.5:1.
inline const QColor selection(0x59, 0x90, 0xc0);
inline const QColor amber(0xff, 0xc1, 0x07);
inline const QColor success(0x10, 0xb9, 0x81);
inline const QColor error(0xff, 0x42, 0x55);
// A translucent white surface composited over the background.
inline QColor whiteOverBackground(double alpha)
{
    return QColor::fromRgbF(background.redF() + (1 - background.redF()) * alpha,
                            background.greenF() + (1 - background.greenF()) * alpha,
                            background.blueF() + (1 - background.blueF()) * alpha);
}
// The error red blended 40% toward alto, a calmer salmon that reads on the dark window (used for the refusal
// message beside Apply and "Needs Z more space").
inline const QColor errorMuted = QColor::fromRgbF(error.redF() * 0.6 + alto.redF() * 0.4,
                                                  error.greenF() * 0.6 + alto.greenF() * 0.4,
                                                  error.blueF() * 0.6 + alto.blueF() * 0.4);
inline const QColor white04 = whiteOverBackground(0.04);
inline const QColor white10 = whiteOverBackground(0.10);
} // namespace Palette

#endif // PALETTE_H
