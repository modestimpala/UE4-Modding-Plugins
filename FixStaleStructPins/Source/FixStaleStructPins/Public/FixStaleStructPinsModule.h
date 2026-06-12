#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UBlueprint;
class UK2Node;

/**
 * FFixStaleStructPinsModule
 *
 * Editor plugin that fixes stale Blueprint struct pins after struct reconformation.
 *
 * Problem:  When a User Defined Struct is modified, the engine reconforms
 *           Break/Make/SetFieldsInStruct nodes by adding NEW pins with updated
 *           field GUIDs. However, the OLD pins (orphaned GUIDs) keep their
 *           connections, while the new correct pins sit unconnected. This leaves
 *           the Blueprint in a broken state where data flows through dead pins.
 *
 * Solution: For each struct node, group pins by display name. When exactly two
 *           pins share a display name (one connected = old, one empty = new),
 *           transfer all connections and default values from old to new, then
 *           recompile the Blueprint.
 *
 * Usage:    Console command:  FixStaleStructPins /Game/Path/Blueprint.Blueprint
 *           Or via the Tools menu entry.
 */
class FFixStaleStructPinsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Fix stale struct pins in a Blueprint loaded by path. */
	void FixBlueprintByPath(const TArray<FString>& Args);

	/** Fix stale struct pins in a specific Blueprint object. */
	int32 FixBlueprint(UBlueprint* BP);

private:
	/** Examine a single struct node for duplicate pin pairs and transfer connections. */
	int32 FixStructNodePins(UK2Node* Node);

	void RegisterMenus();

	IConsoleObject* ConsoleCommand = nullptr;
};
