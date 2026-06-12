// Copyright (c) 2026 modestimpala. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Kismet2/StructureEditorUtils.h"

class FStructProperty;

/**
 * FStructFixupModule
 *
 * Editor plugin that comprehensively fixes User Defined Struct reinstancing.
 *
 * Problem:  When you modify a UDS in UE4, the engine's built-in CompileStruct
 *           pipeline misses several categories of dependent objects:
 *           - DataTables whose editor is not open
 *           - Actor/component instances in loaded levels with overridden struct values
 *           - Blueprints discovered only through asset-registry dependencies (not loaded)
 *           - Nested struct chains (struct A embeds struct B; B changes but A's instances
 *             are not fully migrated)
 *           These orphaned instances retain raw memory from the OLD layout, causing
 *           "unknown structure" errors, corruption, or build failures.
 *
 * Solution: Hook into the engine's PreChange/PostChange struct notifications
 *           and perform the steps the engine skips:
 *           1. PreChange: serialise every live struct instance to tagged bytes
 *              (tagged serialization maps fields by GUID, surviving layout changes).
 *           2. PostChange: re-initialise those instances with the new layout,
 *              deserialise the backed-up data (surviving fields restore automatically),
 *              force-recompile every dependent Blueprint found via the Asset Registry,
 *              rebuild every DataTable that uses the struct, and refresh editors.
 *
 *           Also exposes the console command  StructFixup.FixAll  and a
 *           toolbar button for a manual full sweep of every loaded UDS.
 */
class FStructFixupModule
	: public IModuleInterface
	, public FStructureEditorUtils::INotifyOnStructChanged   // PreChange / PostChange
{
public:
	// IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// INotifyOnStructChanged - called by the engine around every UDS edit
	virtual void PreChange(const UUserDefinedStruct* Struct,
		FStructureEditorUtils::EStructureEditorChangeInfo Info) override;
	virtual void PostChange(const UUserDefinedStruct* Struct,
		FStructureEditorUtils::EStructureEditorChangeInfo Info) override;

	/** Manually fix all loaded User Defined Structs and their dependents. */
	void FixAllStructs();

private:
	// ---------------------------------------------------------------
	//  Instance-data backup  (PreChange → PostChange)
	// ---------------------------------------------------------------
	struct FPropertyBackup
	{
		TWeakObjectPtr<UObject> Object;
		FProperty*              Property;
		int32                   ArrayIndex;      // for static-array elements
		TArray<uint8>           TaggedBytes;     // serialised with field GUIDs
		TSet<UObject*>          ReferencedObjects;
	};

	/** Backups captured during PreChange, consumed during PostChange. */
	TArray<FPropertyBackup> PendingBackups;

	// ---------------------------------------------------------------
	//  Core work
	// ---------------------------------------------------------------

	/** Gather all loaded UObjects whose property tree contains a particular struct. */
	void CollectObjectsReferencingStruct(const UUserDefinedStruct* Struct,
		TArray<TPair<UObject*, FStructProperty*>>& OutHits) const;

	/** Serialize struct instance data for one property into a backup entry. */
	void BackupStructInstance(UObject* Object, FStructProperty* Prop, const UUserDefinedStruct* Struct);

	/** After the struct has been recompiled, apply backups and do the extra fixup. */
	void ApplyPostChangeFixup(const UUserDefinedStruct* Struct);

	/** Migrate data for a single backed-up entry. */
	void RestoreBackup(FPropertyBackup& Backup, const UUserDefinedStruct* Struct);

	/** Force-recompile every Blueprint that depends on Struct (via Asset Registry). */
	void RecompileDependentBlueprints(const UUserDefinedStruct* Struct);
	int32 RecompileDependentBlueprintsCount(const UUserDefinedStruct* Struct);

	/** Find and rebuild every loaded DataTable whose RowStruct == Struct. */
	void RebuildDependentDataTables(const UUserDefinedStruct* Struct);
	int32 RebuildDependentDataTablesCount(const UUserDefinedStruct* Struct);

	/** Broadcast a general refresh so property editors rebuild. */
	void RefreshEditors();

	// ---------------------------------------------------------------
	//  Toolbar / menu extension
	// ---------------------------------------------------------------
	void RegisterMenus();

	bool bInsideFixup = false;   // re-entrancy guard
};
