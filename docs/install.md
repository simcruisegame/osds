# Installing OSDS

OSDS runs on Windows. Get it from the [Releases page](https://github.com/simcruisegame/osds/releases).

| File | What it is |
|---|---|
| `OSDS-<version>-windows-x64-setup.exe` | The installer. Adds a Start menu entry and an uninstaller. |
| `OSDS-<version>-windows-x64.zip` | Portable version. Unzip and run `OSDS.exe`. |
| `OSDS-<version>-SHA256SUMS.txt` | Checksums to check the two files above. |

Good to know:

- OSDS was called SimCruise until November 2026. Releases up to 2.0.3 use the old name (files start with `SimCruise-`, Start menu entry **SimCruise**).
- The program file is always `OSDS.exe`.
- There is no Mac version of OSDS 2 yet (it needs a Mac to build on). The old 1.x versions for Mac and Windows (made with the Godot engine) are still on the Releases page. Their code is at the git tag `godot-final`.

## What your PC needs

We tested on **one PC only**: Windows 11, NVIDIA RTX 2070 Super with Max-Q Design (laptop GPU, 8 GB), Intel Core i9-10885H (8 cores), 32 GB memory. We did not test AMD or Intel graphics, other NVIDIA cards, or Windows 10. The "Minimum" column is a guess, not a test result.

| | Minimum (guess, not tested) | Tested |
|---|---|---|
| Windows | 10 or 11, 64-bit | 11, 64-bit |
| Processor | 4 cores, 2017 or newer | 8 cores, Intel Core i9-10885H |
| Memory | 16 GB (8 GB can be slow or stop) | 32 GB |
| Graphics | DirectX 12, Shader Model 6, 6 GB video memory (for example NVIDIA GTX 1660 or AMD RX 5600), Low or Medium preset | NVIDIA RTX 2070 Super with Max-Q Design, 8 GB, High preset |
| Disk | 3 GB for the game, plus up to 2 GB for map data and photos | SSD |
| Internet | Not needed for Ocean Drive, Miami Beach | Broadband for every other place |
| Microsoft parts | Visual C++ 2015-2022 runtime (x64) and GameInput. The installer adds them. | |
| Controls | Keyboard | Xbox controller, or a USB wheel with a Windows GameInput driver |

- The game picks a graphics preset for your GPU (Low, Medium, High or Ultra) and caps the frame rate at 60 fps.
- Measured frame rates on the tested laptop: [release report](release/v2.0.0-report.md).
- Without DirectX 12, the game uses DirectX 11 with a simpler picture.
- If the game does not work on your PC, [open an issue](https://github.com/simcruisegame/osds/issues) with your Windows version, graphics card and memory.

## Internet

- **Ocean Drive, Miami Beach** works offline. Its streets come with the game.
- **Every other place** needs internet. The game downloads its map, terrain and photos when you go there.
- With no internet, every start goes to Miami Beach.

## Install with the installer

1. Download `OSDS-<version>-windows-x64-setup.exe`.
2. Optional: check the download. In PowerShell, run `Get-FileHash .\OSDS-<version>-windows-x64-setup.exe`. The result must match the value in `OSDS-<version>-SHA256SUMS.txt`.
3. Run it. Windows may say "Windows protected your PC". This is because the game is not code-signed (that needs a paid certificate). Click **More info**, then **Run anyway**.
4. Choose one:
   - **Install for me only**: no administrator rights needed. The game goes to `%LOCALAPPDATA%\Programs\OSDS`. An update goes into the folder of the version you have.
   - **Install for all users**: goes to Program Files. Needs administrator rights.
5. Read the "Information" page (internet, Microsoft parts, PC needs) and follow the steps. It adds OSDS to the Start menu, and to the desktop if you tick the box.
6. At the end, the installer checks for the Microsoft Visual C++ 2015-2022 runtime (x64) and Microsoft GameInput. It installs any that are missing or too old. Windows asks for permission: click **Yes**.
   - If you click **No**, the installer tells you what is missing. Without the runtime the game cannot start. Without GameInput, racing wheels do not work.
   - The game asks again when you start it. The two Microsoft installers stay in `<install folder>\Engine\Extras\Redist\en-us`; you can run them as administrator.

## Uninstall

- Use **Settings > Apps > Installed apps > OSDS > Uninstall**, or **Uninstall OSDS** in the Start menu.
- It deletes the game, the map cache, downloaded photos, other caches and logs in `%LOCALAPPDATA%\OSDS`.
- It then asks whether to delete your settings, car tuning and screenshots too. The default is No, so they stay for a new install.
- A silent uninstall (`unins000.exe /VERYSILENT`) keeps them.
- It does not remove the Microsoft runtime or GameInput, because other programs may use them.

## Portable zip

- Unzip `OSDS-<version>-windows-x64.zip` to any folder and run `OSDS.exe`.
- Keep the folder together: `OSDS\tools\atlas.exe` (the map helper) and `OSDS\data\` must stay next to the game.
- The zip does not install the Microsoft parts. If they are missing, `OSDS.exe` offers to install them from `Engine\Extras\Redist\en-us` in the folder.
- To remove it, delete the folder, and `%LOCALAPPDATA%\OSDS` for the caches.

## Where your files are

Everything is in `%LOCALAPPDATA%\OSDS` (the old name, so updates keep it).

| Folder | What is in it | On uninstall |
|---|---|---|
| `map-cache` | Map data, terrain and aerial photos of places you drove (kept near 100 MB) | Deleted |
| `Saved\Live`, `Saved\NeuralDetail`, `Saved\Knowledge`, `Saved\Logs`, other `Saved` folders | Downloaded photos and weather, generated detail, place facts, logs | Deleted |
| `Saved\OSDS` | `preferences.json` (car, gearbox, sound, HUD, lens, head motion, rumble) and `tuning-<car>.json` | You choose (kept by default) |
| `Saved\Config` | Graphics and input settings | You choose (kept by default) |
| `Saved\Screenshots` | Your screenshots | You choose (kept by default) |

## Problems

| Problem | Fix |
|---|---|
| Nothing happens, or "MSVCP140.dll was not found" / "VCRUNTIME140_1.dll was not found" | The Visual C++ runtime is missing. Start `OSDS.exe` and click **Yes** when it asks. Or run `Engine\Extras\Redist\en-us\vc_redist.x64.exe` in the install folder. Or get it from Microsoft: <https://aka.ms/vs/17/release/vc_redist.x64.exe>. |
| "The Game Input Library is missing" | Click **Yes** to install it, or run `Engine\Extras\Redist\en-us\GameInputRedist.msi` in the install folder. |
| The game always starts on Ocean Drive | A random start needs internet. Without it, the game starts on Ocean Drive, Miami Beach. |
| Low frame rate | Start with a lower preset, for example `OSDS.exe -OsdsGraphics=medium` (you can add this to a shortcut's target). |
| Something else is wrong | Open an issue on GitHub with your Windows version, graphics card and what you did. |

## The old 1.x version

The 1.x releases (Godot engine, Mac and Windows) are kept for reference. Their install notes are in this file at the git tag `godot-final`.
