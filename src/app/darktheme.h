#ifndef DARKTHEME_H
#define DARKTHEME_H

class QApplication;
class QPushButton;

// The app always runs dark, whatever the macOS appearance: Fusion style (so the
// palette applies everywhere), dark colour scheme, a palette built from Palette (palette.h). Call right after creating
// the QApplication, before any window.
void applyDarkTheme(QApplication &app);

// The call-to-action look (the selection blue with dark text, grey when disabled) for a button such as Apply.
void stylePrimaryButton(QPushButton *button);

#endif // DARKTHEME_H
