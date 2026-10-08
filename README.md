# Forest Launcher

Qt game launcher for Linux. Manage games with many presets aviable and QOL features.

## Features

- Per-game launch arguments, environment variables, runner selection, and prefix overrides.
- Proton Manager with "Latest" choices or specific-version downloads
- Online-fix support with specific steam account selection (no files for it are provided just the correct environment variables) 
- Desktop, application-menu, and native Steam shortcuts, including multi-account Steam shortcut selection.
- SteamGridDB icons, grids, heroes, and logos selection menu specifically curated for steam library use.
- Optional presets like MangoHud, Lossless Scaling, SDL, and sleep inhibition.

## Installation

Get the matching download from [GitHub Releases](https://github.com/Benaeo/forest-launcher/releases/latest).

### Arch-based distributions
```sh
sudo pacman -U ./forest-launcher-*-x86_64.pkg.tar.zst
```

### Fedora
```sh
sudo dnf install ./forest-launcher-*.fc44.x86_64.rpm
```

### Debian / Ubuntu
```sh
sudo apt install ./forest-launcher_*_amd64.deb
```

## Targeted distros

Release packages target **Linux x86_64**:

| Target | Download |
| --- | --- |
| Arch Linux | `.pkg.tar.zst` |
| Debian 13 / Ubuntu 24.04 LTS | `amd64.deb` |
| Fedora 44 | `fc44.x86_64.rpm` |

## Getting started

1. Open **Forest Launcher** from your application menu. On first launch, optionally add a SteamGridDB API key and review global settings, or select **Skip for now**.
2. Open **Settings** to choose defaults. Use the runner download control or **Proton Manager** to install a runner for Windows games.
3. Choose **Add game**, select the game type, provide its executable and enable or change settings like icon.
4. Adjust the game's options, save it, and **HAVE FUN**.

You can also open the add-game dialog by passing an executable:

```sh
forest-launcher /path/to/game.exe
```

UMU is managed automatically for ordinary Proton launches. Initial component/runner setup requires an internet connection.

The default prefix is `~/Games/forest-launcher/<title>/` but can be changed in the global settings options

## Dependencies

`Qt 6.4+` - `Python 3.11+`

## Optional Dependencies

`Steam` - `Mangohud` - `lsfg-vk`

## Optional integrations

### Steam

Steam library entries, Steam shortcuts, and Steam online-fix integration require the **native Steam client**. Flatpak Steam integration is not currently supported.

Selecting a different online-fix launch account can require a Steam restart; Forest asks before restarting an open client. Epic Games/EOS and Photon online-fix integration are not implemented yet.

### Lossless Scaling

Configure it using the **Lossless Scaling**:

- Multiplier **1** disables frame generation; **2–100** enables it.
- Flow scale supports **25–100%**.
- Performance Mode is optional and initially off.

Forest applies these settings per launch without rewriting its configuration or DLLs. This integration supports direct native/Proton launches.

### SteamGridDB

SteamGridDB browsing requires an API key entered in **Settings**. The key is stored in private application settings, **not encrypted**, and is sent only to the SteamGridDB API. Executable icon extraction does not require a key.

## Data and safety

Default locations follow the XDG directory settings:

| Data | Default location |
| --- | --- |
| Global settings | `~/.config/forest-launcher/settings.json` |
| Game settings | `~/.local/share/forest-launcher/games/<game-title>.json` |
| Saved artwork | `~/.local/share/forest-launcher/artwork/{icon,grid,banner,logo,extracted-icon}/<game-title>.png` |
| Launch logs | `~/.local/state/forest-launcher/logs/<game-title>/<timestamp>.log` |
| Installed Proton runners | `~/.local/share/Steam/compatibilitytools.d/` |
| Per-game Wine prefix | `~/Games/forest-launcher/<game-title>/` |

## Acknowledgements

Forest integrates with [Qt](https://www.qt.io/), [UMU](https://github.com/Open-Wine-Components/umu-launcher), [Proton](https://github.com/ValveSoftware/Proton), [lsfg-vk](https://github.com/PancakeTAS/lsfg-vk), and [SteamGridDB](https://www.steamgriddb.com/). These are independent projects with their own licenses and compatibility requirements.

## License

Forest Launcher is licensed under **GPL-3.0-only**. See [LICENSE](LICENSE). Bundled third-party components retain their respective licenses.
