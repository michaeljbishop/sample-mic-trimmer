`SmtIcon.icon` is the source (made for Icon Composer; a background layer and a foreground layer). `Assets.car` and
`SmtIcon.icns` are generated from it and checked in, so the build needs no Xcode:

    xcrun actool SmtIcon.icon --compile out --platform macosx --minimum-deployment-target 13.0 \
        --app-icon SmtIcon --output-partial-info-plist out/partial.plist

`Assets.car` is what macOS 26 and later read (`CFBundleIconName`); without it the icon is shown on a grey plate.
`SmtIcon.icns` is for older macOS (`CFBundleIconFile`). `AppIcon.ico` is the Windows icon.
