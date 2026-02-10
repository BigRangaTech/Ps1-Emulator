# GUI Setup

## Dependencies
The GUI requires:
- SDL2
- SDL2_ttf

If your distro packages are installed, CMake will detect them and build `ps1emu_gui`.

## Flatpak
The Flatpak manifest installs the app and bundles assets under `/app/share/ps1emu`.
If SDL2 or SDL2_ttf are missing in your runtime, add modules for them in
`flatpak/org.ps1emu.PS1Emu.yml`.

The Flatpak command uses `ps1emu_gui_wrapper`, which ensures a writable config
under the user's XDG config directory and copies the default config if needed.

## BIOS Workflow
The GUI supports:
- Browsing BIOS files in `./Bios` or `./bios`.
- Importing BIOS files into the app data folder for Flatpak-safe access.

## Fonts
The GUI looks for fonts in:
- `assets/fonts/AtkinsonHyperlegible-Regular.ttf`
- `assets/fonts/SpaceGrotesk-Regular.ttf`
- `assets/fonts/IBM-Plex-Sans-Regular.ttf`

If none are found, it falls back to common system fonts.
