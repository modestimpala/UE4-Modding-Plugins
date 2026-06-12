#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ModPackagerSettings.generated.h"

/**
 * Project Settings -> Plugins -> Mod Packager.
 * Stored in DefaultGame.ini (config=Game).
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Mod Packager"))
class MODPACKAGER_API UModPackagerSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UModPackagerSettings()
		: bPromptForDeployDirEachRun(true)
		, bCompressPak(true)
		, bIterativeCook(true)
		, bCleanModCookDirFirst(true)
		, TargetPlatform(TEXT("WindowsNoEditor"))
	{
	}

	/**
	 * Default folder that finished .pak files are copied into.
	 * e.g. your r2modman profile's shimloader\pak folder, or the game's
	 * ...\VotV\Content\Paks\LogicMods folder.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Deploy", meta = (RelativeToGameDir = false))
	FString DefaultDeployDirectory;

	/** If true, a folder picker is shown every time (defaulting to DefaultDeployDirectory). */
	UPROPERTY(EditAnywhere, config, Category = "Deploy")
	bool bPromptForDeployDirEachRun;

	/** Compress assets inside the .pak (UnrealPak -compress). */
	UPROPERTY(EditAnywhere, config, Category = "Cook & Pak")
	bool bCompressPak;

	/** Use iterative cooking so unchanged assets are skipped on repeat builds. */
	UPROPERTY(EditAnywhere, config, Category = "Cook & Pak")
	bool bIterativeCook;

	/** Delete this mod's previously cooked output before cooking, so the pak only contains current assets. */
	UPROPERTY(EditAnywhere, config, Category = "Cook & Pak")
	bool bCleanModCookDirFirst;

	/** Cook target platform. WindowsNoEditor for shipping game / mods. */
	UPROPERTY(EditAnywhere, config, Category = "Cook & Pak")
	FString TargetPlatform;

	/** Extra args appended to the cook commandlet (advanced; usually leave empty). */
	UPROPERTY(EditAnywhere, config, Category = "Cook & Pak")
	FString ExtraCookerArgs;

	/**
	 * Optional override for the content root folder that holds mods (relative to /Game).
	 * The "Package Mod" entry only appears for folders at or under this path.
	 * Leave as "Mods" for /Game/Mods/<ModName>.
	 */
	UPROPERTY(EditAnywhere, config, Category = "General")
	FString ModsRootFolder = TEXT("Mods");
};
