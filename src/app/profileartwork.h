#pragma once

#include <QLabel>

// The profile's picture, scaled down (never up) to fit kMaxSize; hidden while there is none.
class ProfileArtwork : public QLabel
{
    Q_OBJECT
public:
    static constexpr int kMaxSize = 384; // logical pixels
    explicit ProfileArtwork(QWidget *parent = nullptr);
    void setImageFile(const QString &path); // "" or an unreadable file hides the label
};
