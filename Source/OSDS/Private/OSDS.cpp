// Copyright OSDS contributors. All Rights Reserved.

#include "OSDS.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace
{
	/**
	 * Up to 2.1.1 the game's code name was SimCruise, and its data lived in %LOCALAPPDATA%\SimCruise:
	 * the map cache, and under Saved the preferences and per-car tuning (Saved\SimCruise), live and
	 * scan caches. From 2.1.2 the project is OSDS (%LOCALAPPDATA%\OSDS). On the first start after the
	 * update each part the new folder does not have yet is moved across (a rename on the same disk,
	 * so even a large map cache takes no time); nothing is overwritten and nothing is deleted.
	 */
	void MoveDataFromSimCruise()
	{
		IFileManager& Files = IFileManager::Get();
		const FString Settings = FPlatformProcess::UserSettingsDir();
		const FString Old = FPaths::Combine(Settings, TEXT("SimCruise"));
		const FString New = FPaths::Combine(Settings, TEXT("OSDS"));
		if (!Files.DirectoryExists(*Old)) return;
		auto MoveIfMissing = [&Files](const FString& From, const FString& To)
		{
			if (!Files.DirectoryExists(*From) || Files.DirectoryExists(*To)) return;
			Files.MakeDirectory(*FPaths::GetPath(To), true);
			if (Files.Move(*To, *From, /*bReplace*/ false, /*bEvenIfReadOnly*/ true, /*bAttributes*/ true))
			{
				UE_LOG(LogTemp, Display, TEXT("Moved %s to %s (the game's old name)"), *From, *To);
			}
		};
		MoveIfMissing(FPaths::Combine(Old, TEXT("map-cache")), FPaths::Combine(New, TEXT("map-cache")));
		// Saved: the preferences folder kept the code name too (Saved\SimCruise -> Saved\OSDS).
		MoveIfMissing(FPaths::Combine(Old, TEXT("Saved"), TEXT("SimCruise")), FPaths::Combine(New, TEXT("Saved"), TEXT("OSDS")));
		TArray<FString> Children;
		Files.FindFiles(Children, *FPaths::Combine(Old, TEXT("Saved"), TEXT("*")), /*Files*/ false, /*Directories*/ true);
		for (const FString& Child : Children)
		{
			if (Child == TEXT("SimCruise") || Child == TEXT("Logs") || Child == TEXT("Crashes") || Child == TEXT("Config")) continue;
			MoveIfMissing(FPaths::Combine(Old, TEXT("Saved"), Child), FPaths::Combine(New, TEXT("Saved"), Child));
		}
	}

	class FOsdsGameModule : public FDefaultGameModuleImpl
	{
	public:
		virtual void StartupModule() override
		{
			MoveDataFromSimCruise();
		}
	};
}

IMPLEMENT_PRIMARY_GAME_MODULE(FOsdsGameModule, OSDS, "OSDS");
