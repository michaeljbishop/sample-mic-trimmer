#include "darktheme.h"
#include <QApplication>
#include <QPalette>
#include <QPushButton>
#include <QStyleHints>
#include "palette.h"

void applyDarkTheme(QApplication &app)
{
    using namespace Palette;
    app.setStyle(QStringLiteral("Fusion"));
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    QPalette p;
    p.setColor(QPalette::Window, background);
    p.setColor(QPalette::WindowText, alto);
    p.setColor(QPalette::Base, white04);
    p.setColor(QPalette::AlternateBase, white10);
    p.setColor(QPalette::Text, alto);
    p.setColor(QPalette::Button, white10);
    p.setColor(QPalette::ButtonText, white);
    p.setColor(QPalette::BrightText, white);
    p.setColor(QPalette::Highlight, selection);
    p.setColor(QPalette::HighlightedText, background);
    p.setColor(QPalette::ToolTipBase, black);
    p.setColor(QPalette::ToolTipText, white);
    p.setColor(QPalette::PlaceholderText, boulder);
    p.setColor(QPalette::Link, selection);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, boulder);
    p.setColor(QPalette::Disabled, QPalette::Button, background);
    app.setPalette(p);
}

void stylePrimaryButton(QPushButton *button)
{
    using namespace Palette;
    QPalette p = button->palette();
    p.setColor(QPalette::Button, selection);
    p.setColor(QPalette::ButtonText, background);
    p.setColor(QPalette::Disabled, QPalette::Button, white10);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, boulder);
    button->setPalette(p);
    QFont f = button->font();
    f.setBold(true);
    button->setFont(f);
}
