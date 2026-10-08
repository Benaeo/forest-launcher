# Changelog

Release headings match the version in `CMakeLists.txt`. Keep releases newest first.
GitHub releases and the packaged release-note viewer use this file as their single source.

## Unreleased

## 0.31.1

### Added
- Optional first-launch setup:
  - Enter a SteamGridDB API key directly, with a Show key checkbox and a link to obtain one.
  - Review global settings, or skip setup and keep the defaults.
- Post-update release notes:
  - A compact, resizable update announcement with Dismiss and Read news.
  - Release notes for all versions since your previous installation, shown on one page.
- Editable Wine prefix base directory and title-based prefix naming preferences.
- Editable JSON global settings and title-named JSON game records.
- Human-readable desktop and application-menu shortcut filenames.
- Five timestamped launch logs per game with bounded trailing output.

### Changed
- Selected artwork is persisted only when saving a game.
- Operational records use the state directory instead of the game-data directory.
- Release-note text is shipped with the package and works offline; images load online like Help → News.

### Fixed
- Locate and repair missing Lossless Scaling DLL paths before launching games.
- Match release-note images, Markdown styling, and hollow nested bullets to the existing News page.

## 0.22.2

# FIRST RELEASE

## Highlights
- **Steam online-fix support**
  - Automatic detection for supported games
  - Choose the Steam account used for online-fix launches
- **Lossless Scaling integration**
  - Automatic DLL discovery
  - Global defaults and per-game settings
- Quick settings for **MangoHud, No sleep, Lossless Scaling and SDL**
- **SteamGridDB artwork**
  - Add games to selected Steam accounts and choose their artwork
- **Automatic Latest Proton updates**
  - Select a specific runner and version when preferred

---
## Early development
Forest Launcher still needs UI/UX refinement and bug fixes. Please open issues for usability feedback, bug reports and feature suggestions. Online-fix support is currently Steam-only and may have missing features or bugs.

<p align="center">
<img width="280" height="280" alt="Screenshot_20261006_174149" src="https://github.com/user-attachments/assets/cb11b4d7-b45a-4393-b699-97893ce4fd04" />
</p>
