# Microsoft redistributables in the Windows package

The Windows package and installer carry two installers from Microsoft, unchanged, in `Engine\Extras\Redist\en-us`. A clean Windows 10 or 11 does not have them, and the game needs them (`scripts\dep-check.py` lists the DLLs). The installer runs each one only when the PC needs it (`packaging\windows\osds.iss`); the launcher `OSDS.exe` offers them when they are missing.

| File | What | Version in 2.0.3 | Where it comes from | Terms |
|---|---|---|---|---|
| `vc_redist.x64.exe` (and `vc_redist.arm64.exe`, which the launcher asks for on Windows on Arm) | Microsoft Visual C++ 2015-2022 Redistributable: `MSVCP140.dll`, `MSVCP140_1.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll` and others | 14.50.35719.0 | Unreal Engine 5.8.4, `Engine\Extras\Redist\en-us`, which `BuildCookRun -prereqs` stages for packaged games | Microsoft Visual Studio licence terms: the Visual C++ Redistributable is "Distributable Code" that may be distributed unmodified with a program. See [Redistributing Visual C++ files](https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files) and [the latest supported downloads](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist). |
| `GameInputRedist.msi` | Microsoft GameInput (`GameInputRedist.dll` and its service): USB racing wheels | 10.1.26100.6879 | Unreal Engine 5.8.4, `Engine\Extras\Redist\en-us`, staged with `[GameInput] IncludeRedistFiles=True` and `-prereqs` | "Microsoft GameInput Redistributable" licence of the `Microsoft.GameInput` package: the files in its distributables list may be copied and distributed in object code form. See [the licence of Microsoft.GameInput 3.4.259](https://packages.nuget.org/packages/Microsoft.GameInput/3.4.259/License) and [GameInput installation for PC](https://learn.microsoft.com/gaming/gdk/_content/gc/input/overviews/input-nuget). |

Both files have a valid Microsoft Corporation Authenticode signature; `scripts\package.ps1` and `scripts\installer.ps1` stop when a signature is not valid. Epic Games ships them in the engine for this use (packaged games), under the [Unreal Engine EULA](https://www.unrealengine.com/eula).

Open point: the engine's `GameInputRedist.msi` has its own version number (10.1.26100.6879), not the NuGet package's. Before a release, a person with the right to decide confirms that the GameInput terms above are the ones of this MSI.

The uninstaller does not remove them, because other programs can use them.
