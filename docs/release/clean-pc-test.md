# Clean PC test of the installer

The release criterion: a person downloads the installer from GitHub on any PC, it installs the game and the Microsoft parts that the game needs, and the person can play. This test proves it on a PC that never had OSDS, Unreal Engine or Visual Studio. The release PC cannot prove it: it has the Visual C++ runtime and GameInput already.

Do the test on one of these:

- a different PC, or a new Windows install;
- **Windows Sandbox** (Windows 10/11 Pro or Enterprise): a clean Windows with no Visual C++ runtime and no GameInput, deleted when you close it. To turn it on once (administrator PowerShell, then restart): `Enable-WindowsOptionalFeature -Online -FeatureName Containers-DisposableClientVM -All`. Sandbox has a virtual graphics card, so the game can be slow or fall back to DirectX 11 there; it proves the install and the start, not the frame rate;
- a virtual machine with a new Windows image.

A new Windows user on the release PC is **not** a clean test: the runtime is installed for all users.

## Before you start

On the clean PC, open PowerShell and run this command. Write down the result. On a clean PC both lines say `False`:

```powershell
Test-Path C:\Windows\System32\vcruntime140_1.dll; Test-Path C:\Windows\System32\GameInputRedist.dll
```

## Steps

1. Download `OSDS-<version>-windows-x64-setup.exe` and `OSDS-<version>-SHA256SUMS.txt` from the release page with a web browser (not with a copy from the release PC).
2. Run `Get-FileHash .\OSDS-<version>-windows-x64-setup.exe` in the download folder. The value must be the same as in the SHA256SUMS file.
3. Start the installer. If SmartScreen says "Windows protected your PC", click **More info**, then **Run anyway**.
4. Select **Install for me only**.
5. Make sure that the page "Information" tells you about the internet, the Microsoft parts and the PC that you need.
6. Click through to the end. After the files are copied, Windows asks for permission twice (Visual C++ runtime, then GameInput). Click **Yes** both times.
7. On the last page, keep **Launch OSDS** ticked and click **Finish**.
8. Drive for one minute on Ocean Drive. Press Esc and quit.
9. Run the command from "Before you start" again. Both lines must now say `True`.
10. Disconnect the internet. Start OSDS from the Start menu. The game must start on Ocean Drive, Miami Beach. Connect the internet again.
11. Uninstall: **Settings > Apps > Installed apps > OSDS > Uninstall**. When it asks "Do you also want to delete your OSDS settings ...", click **No**.
12. Open `%LOCALAPPDATA%\OSDS` in File Explorer. Only `Saved\OSDS`, `Saved\Config` (and `Saved\Screenshots`, if you took screenshots) may be there. `map-cache`, `Saved\Live` and `Saved\Logs` must be gone.

Optional, the "No" path: on a second clean PC, click **No** in step 6. The installer must show a message that names the missing parts and says how to install them. Then start OSDS: the launcher must ask to install the Visual C++ runtime. Click **Yes**; the game must then start.

## Pass and fail

| Step | Pass | Fail (write down what you see) |
|---|---|---|
| 6 | Two permission prompts from "Microsoft Corporation", no error message | An error message, or a message "These Microsoft parts are not installed" after you clicked Yes |
| 7-8 | The game window opens and you can drive | Nothing opens; "MSVCP140.dll was not found", "VCRUNTIME140_1.dll was not found" or "The Game Input Library is missing"; a crash window |
| 9 | Both `True` | One is `False` |
| 10 | Miami Beach | A black screen, a stop, or no message |
| 12 | Only your settings stay | `map-cache` or `Saved\Live` is still there |

## The logs, when something fails

- Installer log: run the installer from PowerShell with a log, then attach the log file to the issue:
  `.\OSDS-<version>-windows-x64-setup.exe /LOG="$env:USERPROFILE\Desktop\osds-setup.log"`.
  The lines "Visual C++ runtime:" and "GameInput:" tell what it found and the exit code of each Microsoft installer.
- Game log: `%LOCALAPPDATA%\OSDS\Saved\Logs\OSDS.log`.
- Crash: `%LOCALAPPDATA%\OSDS\Saved\Crashes`.

Also write down the Windows version (`winver`), the graphics card (**Task Manager > Performance > GPU**) and the memory.

## What this test does not cover

One clean test covers one PC. It does not prove other graphics makers (AMD, Intel), other NVIDIA cards, Windows 10, Windows on Arm, or an "all users" install. Write down which of these you tested.
