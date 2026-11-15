# Installing OSDS

OSDS 2.0 and later is the Unreal Engine 5.8 client, for Windows. Downloads are on the [Releases page](https://github.com/simcruisegame/osds/releases):

| File | Type |
|---|---|
| `OSDS-<version>-windows-x64-setup.exe` | Installer: Start menu entry and uninstaller |
| `OSDS-<version>-windows-x64.zip` | Portable folder: unzip and run `SimCruise.exe` |
| `OSDS-<version>-SHA256SUMS.txt` | SHA-256 checksums of the two files above |

OSDS was named SimCruise until November 2026. Releases up to 2.0.3 use the old name: their files start with `SimCruise-` and the Start menu entry is **SimCruise**. In all versions the program file is `SimCruise.exe`.

There is no macOS build of the Unreal client yet (it needs a Mac to build on). The Godot 1.x releases for macOS and Windows stay on the Releases page; their source is at the git tag `godot-final`.

## System requirements

**We tested the game on one PC only:** Windows 11, an NVIDIA GeForce RTX 2070 Super with Max-Q Design (a laptop GPU, 8 GB), an 8-core Intel Core i9-10885H and 32 GB of memory. We did not test AMD or Intel graphics cards, other NVIDIA cards, or Windows 10. The "Minimum" column is our estimate, not a test result. If the game does not work on your PC, [open an issue](https://github.com/simcruisegame/osds/issues) with your Windows version, graphics card and memory.

| | Minimum (estimate, not tested) | Tested |
|---|---|---|
| System | Windows 10 or 11, 64-bit | Windows 11, 64-bit |
| Processor | 4 cores, 2017 or newer | 8 cores, Intel Core i9-10885H |
| Memory | 16 GB (8 GB can be slow or stop) | 32 GB |
| Graphics | DirectX 12 with Shader Model 6 and 6 GB of video memory (for example NVIDIA GTX 1660 or AMD RX 5600), Low or Medium preset | NVIDIA RTX 2070 Super with Max-Q Design, 8 GB, High preset |
| Disk | 3 GB for the game, and up to 2 GB more for the map cache and downloaded photos | SSD |
| Network | Not needed for the bundled Ocean Drive, Miami Beach | Broadband for all other places (the game downloads open map data, terrain and photos when you drive) |
| Microsoft parts | Visual C++ 2015-2022 runtime (x64) and GameInput: the installer installs them | |
| Controls | Keyboard | Xbox controller, or a USB wheel with a Windows GameInput driver |

The game picks a graphics preset for your GPU at the start (`Low`, `Medium`, `High` or `Ultra`) and caps the frame rate at 60 fps. The measured frame rates on the tested laptop are in [the release report](release/v2.0.0-report.md). Without DirectX 12 the game falls back to DirectX 11 with a simpler picture.

## Internet

Only **Ocean Drive, Miami Beach** works with no internet: its streets are in the installer. All other places need an internet connection, because the game downloads their map, terrain and photos when you go there. With no connection, every start goes to Miami Beach.

## Install with the installer

1. Download `OSDS-<version>-windows-x64-setup.exe`.
2. Optional: check the download. In PowerShell, `Get-FileHash .\OSDS-<version>-windows-x64-setup.exe` must print the value in `OSDS-<version>-SHA256SUMS.txt`.
3. Run it. Windows SmartScreen may say "Windows protected your PC", because this free open-source build is not code-signed (signing needs a paid certificate). Click **More info**, then **Run anyway**.
4. Choose **Install for me only** (no administrator rights; the game goes to `%LOCALAPPDATA%\Programs\OSDS`; an update goes into the folder of the version you have) or **Install for all users** (Program Files; needs administrator rights).
5. Read the page "Information" (internet, Microsoft parts, the PC that you need) and follow the installer. It adds OSDS to the Start menu, and to the desktop if you tick it.
6. At the end, the installer examines the PC for the Microsoft Visual C++ 2015-2022 runtime (x64) and Microsoft GameInput, and installs each one that is missing or too old. They are for the whole PC, so Windows asks for permission: click **Yes**. If you click **No**, the installer tells you what is missing. Without the runtime the game cannot start; without GameInput racing wheels do not work. The game's launcher asks again when you start it, and the two Microsoft installers stay in `<install folder>\Engine\Extras\Redist\en-us` (you can run them as administrator).

To uninstall, use **Settings > Apps > Installed apps > OSDS > Uninstall**, or **Uninstall OSDS** in the Start menu. The uninstaller deletes the game, the map cache, the downloaded photos, the other caches and the logs in `%LOCALAPPDATA%\SimCruise`. Then it asks whether to delete your settings, car tuning and screenshots too (the default is No: they stay for a new install). A silent uninstall (`unins000.exe /VERYSILENT`) keeps them. It does not remove the Microsoft runtime or GameInput, because other programs can use them.

## Run the portable zip

Unzip `OSDS-<version>-windows-x64.zip` to any folder and run `SimCruise.exe`. Keep the folder together: `SimCruise\tools\atlas.exe` (the map helper) and `SimCruise\data\` must stay beside the game. The zip does not install the Microsoft parts: when they are missing, `SimCruise.exe` asks to install them from `Engine\Extras\Redist\en-us` in the folder. To remove it, delete the folder, and `%LOCALAPPDATA%\SimCruise` for the caches.

## Where it keeps things

OSDS writes to `%LOCALAPPDATA%\SimCruise` (the old name, so updates keep it):

| Folder | What | Uninstall |
|---|---|---|
| `map-cache` | Map data, terrain and aerial photos of the places you drove (the map helper keeps it near 100 MB) | deleted |
| `Saved\Live`, `Saved\NeuralDetail`, `Saved\Knowledge`, `Saved\Logs`, other `Saved` folders | Downloaded photos and weather, made detail, place facts, logs | deleted |
| `Saved\SimCruise` | `preferences.json` (car, gearbox, sound, HUD, lens, head motion, rumble) and `tuning-<car>.json` | you choose (kept by default) |
| `Saved\Config` | Graphics and input settings | you choose (kept by default) |
| `Saved\Screenshots` | Your captures | you choose (kept by default) |

## Problems

- **Nothing happens, or "MSVCP140.dll was not found" / "VCRUNTIME140_1.dll was not found":** the Microsoft Visual C++ runtime is missing. Start `SimCruise.exe` and click **Yes** when it asks, or run `Engine\Extras\Redist\en-us\vc_redist.x64.exe` in the install folder, or get it from Microsoft: <https://aka.ms/vs/17/release/vc_redist.x64.exe>.
- **"The Game Input Library is missing":** click **Yes** to install it, or run `Engine\Extras\Redist\en-us\GameInputRedist.msi` in the install folder.
- **The game starts on Ocean Drive instead of a random road:** the random start needs the internet. Without it, the game always starts on the bundled Ocean Drive, Miami Beach.
- **Low frame rate:** start the game with a lower preset, for example `SimCruise.exe -SimCruiseGraphics=medium` (a shortcut's target can carry it).
- **Report a bug:** open an issue on GitHub with your Windows version, GPU and what you did.

## The Godot 1.x client (old)

The 1.x releases (Godot engine, macOS and Windows) are kept for reference. Their install notes are in this file at the git tag `godot-final`.
