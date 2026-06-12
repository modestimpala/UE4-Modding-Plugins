#pragma once

#include "CoreMinimal.h"

/**
 * Orchestrates: scoped cook (CookDir) -> UnrealPak -> deploy/rename.
 * Entry point is PackageMod(), which must be called on the game thread.
 */
class FModPackager
{
public:
	/**
	 * @param ModPackagePath  Content Browser path of the mod folder, e.g. "/Game/Mods/AriralChat".
	 */
	static void PackageMod(const FString& ModPackagePath);

	/** True if the given content path is at/under the configured mods root (e.g. "/Game/Mods/..."). */
	static bool IsPackageableModFolder(const FString& ContentPath);
};
