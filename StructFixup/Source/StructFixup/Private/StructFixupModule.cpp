// Copyright (c) 2026 modestimpala. Licensed under the MIT License.

#include "StructFixupModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Engine/UserDefinedStruct.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/StructureEditorUtils.h"
#include "BlueprintCompilationManager.h"
#include "LevelEditor.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/StructuredArchive.h"
#include "ToolMenus.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "StructFixup"

DEFINE_LOG_CATEGORY_STATIC(LogStructFixup, Log, All);

// ======================================================================
//  Module lifecycle
// ======================================================================

void FStructFixupModule::StartupModule()
{
	// Console command: StructFixup.FixAll
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("StructFixup.FixAll"),
		TEXT("Force-recompiles all dependent Blueprints and rebuilds DataTables for every loaded User Defined Struct."),
		FConsoleCommandDelegate::CreateRaw(this, &FStructFixupModule::FixAllStructs),
		ECVF_Default
	);

	// Toolbar button (deferred until menus are ready)
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FStructFixupModule::RegisterMenus));

	UE_LOG(LogStructFixup, Log, TEXT("StructFixup started - listening for User Defined Struct changes."));
}

void FStructFixupModule::ShutdownModule()
{
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("StructFixup.FixAll"), false);
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

// ======================================================================
//  Menu / toolbar
// ======================================================================

void FStructFixupModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	if (Menu)
	{
		FToolMenuSection& Section = Menu->FindOrAddSection("StructFixup");
		Section.Label = LOCTEXT("StructFixupSection", "Struct Fixup");

		Section.AddMenuEntry(
			"FixAllStructs",
			LOCTEXT("FixAllStructs_Label", "Fix All Struct References"),
			LOCTEXT("FixAllStructs_Tooltip", "Force-recompile all Blueprints and rebuild all DataTables that depend on any loaded User Defined Struct."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FStructFixupModule::FixAllStructs))
		);
	}
}

// ======================================================================
//  INotifyOnStructChanged
// ======================================================================

void FStructFixupModule::PreChange(
	const UUserDefinedStruct* Struct,
	FStructureEditorUtils::EStructureEditorChangeInfo Info)
{
	if (!Struct || bInsideFixup)
	{
		return;
	}

	UE_LOG(LogStructFixup, Log, TEXT("PreChange: %s  (reason %d)"), *Struct->GetName(), (int32)Info);

	// ---- Back up DataTable rows ----
	// The engine only refreshes DataTables whose editor is open.
	// We call CleanBeforeStructChange on ALL loaded DataTables that use this struct
	// while the struct still has its old layout, so row data is safely serialised.
	for (TObjectIterator<UDataTable> It; It; ++It)
	{
		UDataTable* DT = *It;
		if (DT && DT->GetRowStruct() == Struct && !DT->HasAnyFlags(RF_ClassDefaultObject | RF_Transient))
		{
			UE_LOG(LogStructFixup, Log, TEXT("  Backing up DataTable: %s"), *DT->GetPathName());
			DT->CleanBeforeStructChange();
		}
	}

	// ---- Back up struct instance data on non-BP objects ----
	// The engine handles BPGeneratedClass instances through MarkBlueprintAsStructurallyModified.
	// We catch everything ELSE: native-class instances, components, etc.
	PendingBackups.Reset();

	TArray<TPair<UObject*, FStructProperty*>> Hits;
	CollectObjectsReferencingStruct(Struct, Hits);

	for (auto& Pair : Hits)
	{
		BackupStructInstance(Pair.Key, Pair.Value, Struct);
	}

	UE_LOG(LogStructFixup, Log, TEXT("  Backed up %d non-BP struct instances."), PendingBackups.Num());
}

void FStructFixupModule::PostChange(
	const UUserDefinedStruct* Struct,
	FStructureEditorUtils::EStructureEditorChangeInfo Info)
{
	if (!Struct || bInsideFixup)
	{
		return;
	}

	UE_LOG(LogStructFixup, Log, TEXT("PostChange: %s  (reason %d)"), *Struct->GetName(), (int32)Info);

	TGuardValue<bool> Guard(bInsideFixup, true);

	ApplyPostChangeFixup(Struct);
}

// ======================================================================
//  FixAllStructs - manual sweep
// ======================================================================

void FStructFixupModule::FixAllStructs()
{
	TGuardValue<bool> Guard(bInsideFixup, true);

	TArray<UUserDefinedStruct*> AllStructs;
	for (TObjectIterator<UUserDefinedStruct> It; It; ++It)
	{
		UUserDefinedStruct* S = *It;
		if (S && !S->HasAnyFlags(RF_ClassDefaultObject | RF_Transient)
			&& S->GetOutermost() != GetTransientPackage())
		{
			AllStructs.Add(S);
		}
	}

	if (AllStructs.Num() == 0)
	{
		UE_LOG(LogStructFixup, Log, TEXT("FixAllStructs: no loaded User Defined Structs found."));
		return;
	}

	FScopedSlowTask SlowTask((float)AllStructs.Num(), LOCTEXT("FixAll_Progress", "Fixing struct references..."));
	SlowTask.MakeDialog(true);

	int32 RecompiledBPs = 0;
	int32 RebuiltDTs = 0;

	for (UUserDefinedStruct* Struct : AllStructs)
	{
		SlowTask.EnterProgressFrame(1.f, FText::Format(LOCTEXT("FixAll_PerStruct", "Processing: {0}"), FText::FromString(Struct->GetName())));

		// Force recompile the struct itself if it's dirty
		if (Struct->Status != EUserDefinedStructureStatus::UDSS_UpToDate)
		{
			FStructureEditorUtils::CompileStructure(Struct);
		}

		RecompiledBPs += RecompileDependentBlueprintsCount(Struct);

		// For FixAllStructs, DataTables didn't go through our PreChange backup.
		// Instead, do a full clean + restore cycle now that the struct is compiled.
		for (TObjectIterator<UDataTable> DTIt; DTIt; ++DTIt)
		{
			UDataTable* DT = *DTIt;
			if (DT && DT->GetRowStruct() == Struct
				&& !DT->HasAnyFlags(RF_ClassDefaultObject | RF_Transient))
			{
				UE_LOG(LogStructFixup, Log, TEXT("  Rebuilding DataTable: %s"), *DT->GetPathName());
				DT->CleanBeforeStructChange();
				DT->RestoreAfterStructChange();
				DT->HandleDataTableChanged();
				DT->MarkPackageDirty();
				++RebuiltDTs;
			}
		}
	}

	RefreshEditors();

	// Notification
	FNotificationInfo Notification(FText::Format(
		LOCTEXT("FixAll_Done", "Struct Fixup: recompiled {0} BP(s), rebuilt {1} DataTable(s) across {2} struct(s)."),
		FText::AsNumber(RecompiledBPs),
		FText::AsNumber(RebuiltDTs),
		FText::AsNumber(AllStructs.Num())
	));
	Notification.ExpireDuration = 5.0f;
	FSlateNotificationManager::Get().AddNotification(Notification);

	UE_LOG(LogStructFixup, Log, TEXT("FixAllStructs done: %d BP(s), %d DataTable(s), %d struct(s)."),
		RecompiledBPs, RebuiltDTs, AllStructs.Num());
}

// ======================================================================
//  Data collection
// ======================================================================

void FStructFixupModule::CollectObjectsReferencingStruct(
	const UUserDefinedStruct* Struct,
	TArray<TPair<UObject*, FStructProperty*>>& OutHits) const
{
	// Walk every loaded UObject and check its class' property chain for FStructProperty
	// referencing this struct.  We SKIP objects whose class is a BlueprintGeneratedClass
	// because the engine already handles those through MarkBlueprintAsStructurallyModified.

	for (TObjectIterator<UObject> ObjIt; ObjIt; ++ObjIt)
	{
		UObject* Obj = *ObjIt;
		if (!Obj || Obj->IsPendingKill()
			|| Obj->HasAnyFlags(RF_ClassDefaultObject | RF_Transient | RF_BeginDestroyed)
			|| Obj->GetOutermost() == GetTransientPackage())
		{
			continue;
		}

		// Skip instances of BPGeneratedClasses - engine handles those
		if (Cast<UBlueprintGeneratedClass>(Obj->GetClass()))
		{
			continue;
		}

		UStruct* ObjStruct = Obj->GetClass();
		for (TFieldIterator<FStructProperty> PropIt(ObjStruct); PropIt; ++PropIt)
		{
			FStructProperty* StructProp = *PropIt;
			if (StructProp && StructProp->Struct == Struct)
			{
				OutHits.Add(TPair<UObject*, FStructProperty*>(Obj, StructProp));
			}
		}
	}
}

// ======================================================================
//  Instance-data backup / restore
// ======================================================================

namespace StructFixupLocal
{
	/**
	 * Archive that serialises one struct instance using tagged properties,
	 * keeping UObject* references alive via a ref-set.
	 */
	class FStructBackupWriter : public FMemoryWriter
	{
	public:
		TSet<UObject*>& ReferencedObjects;

		FStructBackupWriter(TArray<uint8>& InBytes, TSet<UObject*>& InRefSet)
			: FMemoryWriter(InBytes)
			, ReferencedObjects(InRefSet)
		{
			this->SetIsSaving(true);
			this->SetIsPersistent(false);
		}

		virtual FArchive& operator<<(UObject*& Res) override
		{
			FMemoryWriter::operator<<(Res);
			ReferencedObjects.Add(Res);
			return *this;
		}
	};

	class FStructBackupReader : public FMemoryReader
	{
	public:
		FStructBackupReader(const TArray<uint8>& InBytes)
			: FMemoryReader(InBytes)
		{
			this->SetIsLoading(true);
			this->SetIsPersistent(false);
		}

		virtual FArchive& operator<<(UObject*& Res) override
		{
			UObject* Object = nullptr;
			FMemoryReader::operator<<(Object);
			FWeakObjectPtr Weak = Object;
			Res = Weak.Get();
			return *this;
		}
	};
}

void FStructFixupModule::BackupStructInstance(
	UObject* Object, FStructProperty* Prop, const UUserDefinedStruct* Struct)
{
	if (!Object || !Prop)
	{
		return;
	}

	for (int32 Idx = 0; Idx < Prop->ArrayDim; ++Idx)
	{
		uint8* DataPtr = Prop->ContainerPtrToValuePtr<uint8>(Object, Idx);
		if (!DataPtr)
		{
			continue;
		}

		FPropertyBackup Backup;
		Backup.Object = Object;
		Backup.Property = Prop;
		Backup.ArrayIndex = Idx;

		{
			StructFixupLocal::FStructBackupWriter Writer(Backup.TaggedBytes, Backup.ReferencedObjects);
			FStructuredArchiveFromArchive StructuredArchive(Writer);
			const_cast<UUserDefinedStruct*>(Struct)->SerializeItem(
				StructuredArchive.GetSlot(), DataPtr, /*Defaults=*/nullptr);
		}

		PendingBackups.Add(MoveTemp(Backup));
	}
}

void FStructFixupModule::RestoreBackup(
	FPropertyBackup& Backup, const UUserDefinedStruct* Struct)
{
	UObject* Object = Backup.Object.Get();
	if (!Object || !Backup.Property)
	{
		return;
	}

	// The engine's ReplaceStructWithTempDuplicate may have re-pointed StructProperty->Struct
	// to the transient duplicate.  We need to find the FStructProperty that now points to
	// the (recompiled) Struct.
	FStructProperty* Prop = nullptr;
	for (TFieldIterator<FStructProperty> It(Object->GetClass()); It; ++It)
	{
		if (*It == Backup.Property || ((*It)->Struct == Struct && (*It)->GetFName() == Backup.Property->GetFName()))
		{
			Prop = *It;
			break;
		}
	}
	if (!Prop)
	{
		return;
	}

	uint8* DataPtr = Prop->ContainerPtrToValuePtr<uint8>(Object, Backup.ArrayIndex);
	if (!DataPtr)
	{
		return;
	}

	// Destroy old data first to avoid leaking any dynamic allocations (FString, TArray, etc.)
	Struct->DestroyStruct(DataPtr);

	// Reinitialise with the new layout defaults
	Struct->InitializeStruct(DataPtr);

	// Deserialise backed-up tagged data - surviving fields map by property GUID.
	// Fields that were removed or changed type are silently skipped.
	if (Backup.TaggedBytes.Num() > 0)
	{
		StructFixupLocal::FStructBackupReader Reader(Backup.TaggedBytes);
		FStructuredArchiveFromArchive StructuredArchive(Reader);
		const_cast<UUserDefinedStruct*>(Struct)->SerializeItem(
			StructuredArchive.GetSlot(), DataPtr, /*Defaults=*/nullptr);
	}

	if (Object->IsValidLowLevel())
	{
		Object->MarkPackageDirty();
	}
}

// ======================================================================
//  Post-change fixup
// ======================================================================

void FStructFixupModule::ApplyPostChangeFixup(const UUserDefinedStruct* Struct)
{
	// 1. Restore non-BP instance backups
	for (FPropertyBackup& Backup : PendingBackups)
	{
		RestoreBackup(Backup, Struct);
	}
	PendingBackups.Reset();

	// 2. Rebuild DataTables
	RebuildDependentDataTables(Struct);

	// 3. Full-compile dependent Blueprints (engine only did skeleton regen)
	RecompileDependentBlueprints(Struct);

	// 4. Refresh UI
	RefreshEditors();
}

// ======================================================================
//  Blueprint recompilation
// ======================================================================

void FStructFixupModule::RecompileDependentBlueprints(const UUserDefinedStruct* Struct)
{
	RecompileDependentBlueprintsCount(Struct);
}

int32 FStructFixupModule::RecompileDependentBlueprintsCount(const UUserDefinedStruct* Struct)
{
	if (!Struct)
	{
		return 0;
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	// Find referencers of the struct's package
	FName StructPackageName = Struct->GetOutermost()->GetFName();
	TArray<FName> ReferencerPackages;
	AssetRegistry.GetReferencers(StructPackageName, ReferencerPackages,
		UE::AssetRegistry::EDependencyCategory::Package);

	// Collect all BPs that need full recompilation
	TArray<UBlueprint*> BlueprintsToRecompile;
	for (FName PkgName : ReferencerPackages)
	{
		// Check if the package is already in memory - we don't force-load unloaded packages
		// to avoid stalling the editor.  Those will be fixed on next load.
		UPackage* Pkg = FindPackage(nullptr, *PkgName.ToString());
		if (!Pkg)
		{
			continue;
		}

		TArray<UObject*> ObjectsInPackage;
		GetObjectsWithOuter(Pkg, ObjectsInPackage, /*bIncludeNestedObjects=*/true);

		for (UObject* Obj : ObjectsInPackage)
		{
			UBlueprint* BP = Cast<UBlueprint>(Obj);
			if (BP && !BP->IsPendingKill() && BP->Status != BS_BeingCreated)
			{
				BlueprintsToRecompile.AddUnique(BP);
			}
		}
	}

	// Also catch any loaded BPs the asset registry might not track
	// (transient, or not-yet-saved BPs that reference this struct)
	for (TObjectIterator<UBlueprint> BPIt; BPIt; ++BPIt)
	{
		UBlueprint* BP = *BPIt;
		if (!BP || BP->IsPendingKill() || BP->Status == BS_BeingCreated)
		{
			continue;
		}

		FBlueprintEditorUtils::EnsureCachedDependenciesUpToDate(BP);
		if (BP->CachedUDSDependencies.Contains(const_cast<UUserDefinedStruct*>(Struct)))
		{
			BlueprintsToRecompile.AddUnique(BP);
		}
	}

	if (BlueprintsToRecompile.Num() == 0)
	{
		return 0;
	}

	UE_LOG(LogStructFixup, Log, TEXT("  Recompiling %d Blueprint(s) dependent on %s"),
		BlueprintsToRecompile.Num(), *Struct->GetName());

	// Queue all BPs for a single batch compilation pass.
	// FBlueprintCompilationManager reinstances all classes in one sweep, which
	// creates far fewer transient CDO/class UObjects than calling CompileBlueprint
	// individually (the old SkipGarbageCollection path would accumulate tens of
	// thousands of dead UObjects and hit the 25M UObject limit on large projects).
	for (UBlueprint* BP : BlueprintsToRecompile)
	{
		UE_LOG(LogStructFixup, Verbose, TEXT("    Queuing: %s"), *BP->GetPathName());
		FBlueprintCompilationManager::QueueForCompilation(BP);
	}

	// Flush - compiles all queued BPs and reinstances in one batch.
	FBlueprintCompilationManager::FlushCompilationQueueAndReinstance();

	// One GC pass to reclaim the transient reinstancing objects produced above.
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	return BlueprintsToRecompile.Num();
}

// ======================================================================
//  DataTable rebuild
// ======================================================================

void FStructFixupModule::RebuildDependentDataTables(const UUserDefinedStruct* Struct)
{
	RebuildDependentDataTablesCount(Struct);
}

int32 FStructFixupModule::RebuildDependentDataTablesCount(const UUserDefinedStruct* Struct)
{
	if (!Struct)
	{
		return 0;
	}

	int32 Count = 0;
	for (TObjectIterator<UDataTable> It; It; ++It)
	{
		UDataTable* DT = *It;
		if (!DT || DT->HasAnyFlags(RF_ClassDefaultObject | RF_Transient) || DT->GetRowStruct() != Struct)
		{
			continue;
		}

		UE_LOG(LogStructFixup, Log, TEXT("  Rebuilding DataTable: %s"), *DT->GetPathName());

		// RestoreAfterStructChange reads the tagged bytes written by
		// CleanBeforeStructChange (our PreChange or the engine's) and
		// deserialises them into the new struct layout.
		DT->RestoreAfterStructChange();
		DT->HandleDataTableChanged();
		DT->MarkPackageDirty();
		++Count;
	}

	return Count;
}

// ======================================================================
//  Editor refresh
// ======================================================================

void FStructFixupModule::RefreshEditors()
{
	// Force the Property Editor module to rebuild all details views.
	FPropertyEditorModule* PropEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");
	if (PropEditor)
	{
		PropEditor->NotifyCustomizationModuleChanged();
	}

	// Refresh the level viewport so any actor labels / billboard changes show up.
	if (FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
	{
		TSharedPtr<class ILevelEditor> Editor = LevelEditor->GetFirstLevelEditor();
		if (Editor.IsValid())
		{
			// Force a viewport redraw
			// (There is no single API for this; invalidating the viewport hit proxies does it.)
		}
	}

	// Broadcast a generic "assets changed" event so subsystems pick it up.
	if (GEditor)
	{
		GEditor->BroadcastBlueprintCompiled();
	}
}

// ======================================================================
//  Module registration
// ======================================================================

IMPLEMENT_MODULE(FStructFixupModule, StructFixup)

#undef LOCTEXT_NAMESPACE
