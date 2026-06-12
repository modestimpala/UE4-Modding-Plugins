#include "FixStaleStructPinsModule.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_SetFieldsInStruct.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "HAL/IConsoleManager.h"
#include "ToolMenus.h"
#include "LevelEditor.h"

#define LOCTEXT_NAMESPACE "FixStaleStructPins"

IMPLEMENT_MODULE(FFixStaleStructPinsModule, FixStaleStructPins)

void FFixStaleStructPinsModule::StartupModule()
{
	ConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("FixStaleStructPins"),
		TEXT("Transfer connections from stale struct pins to reconformed pins. Usage: FixStaleStructPins /Game/Path/BP.BP"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FFixStaleStructPinsModule::FixBlueprintByPath),
		ECVF_Default
	);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FFixStaleStructPinsModule::RegisterMenus));
}

void FFixStaleStructPinsModule::ShutdownModule()
{
	if (ConsoleCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(ConsoleCommand);
		ConsoleCommand = nullptr;
	}

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FFixStaleStructPinsModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& Section = Menu->FindOrAddSection("FixStaleStructPins");
	Section.AddMenuEntry(
		"FixStaleStructPins_Run",
		LOCTEXT("MenuLabel", "Fix Stale Struct Pins..."),
		LOCTEXT("MenuTooltip", "Open the output log, then run: FixStaleStructPins /Game/Path/Blueprint.Blueprint"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.Blueprint"),
		FUIAction()
	);
}

void FFixStaleStructPinsModule::FixBlueprintByPath(const TArray<FString>& Args)
{
	if (Args.Num() < 1)
	{
		UE_LOG(LogTemp, Error, TEXT("Usage: FixStaleStructPins /Game/Path/Blueprint.Blueprint"));
		return;
	}

	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Args[0]);
	if (!BP)
	{
		UE_LOG(LogTemp, Error, TEXT("Could not load blueprint: %s"), *Args[0]);
		return;
	}

	int32 TotalFixed = FixBlueprint(BP);

	if (TotalFixed > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Fixed %d stale pin connection(s) in %s. Don't forget to save!"),
			TotalFixed, *BP->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("No stale struct pins found in %s."), *BP->GetName());
	}
}

int32 FFixStaleStructPinsModule::FixBlueprint(UBlueprint* BP)
{
	if (!BP)
	{
		return 0;
	}

	int32 TotalFixed = 0;

	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node->IsA<UK2Node_BreakStruct>() ||
				Node->IsA<UK2Node_MakeStruct>() ||
				Node->IsA<UK2Node_SetFieldsInStruct>())
			{
				TotalFixed += FixStructNodePins(Cast<UK2Node>(Node));
			}
		}
	}

	if (TotalFixed > 0)
	{
		FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection);
		BP->MarkPackageDirty();
	}

	return TotalFixed;
}

int32 FFixStaleStructPinsModule::FixStructNodePins(UK2Node* Node)
{
	int32 Fixed = 0;

	// Group pins by display name
	TMap<FString, TArray<UEdGraphPin*>> PinsByDisplayName;

	for (UEdGraphPin* Pin : Node->Pins)
	{
		// Skip exec pins
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			continue;
		}

		// Skip the main struct input/output pin
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
		{
			if (Node->IsA<UK2Node_BreakStruct>() && Pin->Direction == EGPD_Input)
			{
				continue;
			}
			if ((Node->IsA<UK2Node_MakeStruct>() || Node->IsA<UK2Node_SetFieldsInStruct>())
				&& Pin->Direction == EGPD_Output)
			{
				continue;
			}
		}

		// Skip sub-pins (expanded struct members)
		if (Pin->ParentPin != nullptr)
		{
			continue;
		}

		FString DisplayName = Pin->PinFriendlyName.IsEmpty()
			? Pin->PinName.ToString()
			: Pin->PinFriendlyName.ToString();

		// Include direction in the key so input/output pins with the same name
		// (e.g. on SetFieldsInStruct) are not grouped together
		FString Key = FString::Printf(TEXT("%s_%d"), *DisplayName, (int32)Pin->Direction);
		PinsByDisplayName.FindOrAdd(Key).Add(Pin);
	}

	// For each group with exactly 2 pins, transfer connections from old to new
	for (auto& Pair : PinsByDisplayName)
	{
		TArray<UEdGraphPin*>& Pins = Pair.Value;
		if (Pins.Num() != 2)
		{
			continue;
		}

		UEdGraphPin* PinA = Pins[0];
		UEdGraphPin* PinB = Pins[1];

		UEdGraphPin* OldPin = nullptr;
		UEdGraphPin* NewPin = nullptr;

		if (PinA->LinkedTo.Num() > 0 && PinB->LinkedTo.Num() == 0)
		{
			OldPin = PinA;
			NewPin = PinB;
		}
		else if (PinB->LinkedTo.Num() > 0 && PinA->LinkedTo.Num() == 0)
		{
			OldPin = PinB;
			NewPin = PinA;
		}
		else
		{
			// Both connected or both unconnected - skip
			continue;
		}

		UE_LOG(LogTemp, Display, TEXT("  [%s] Transferring %d link(s): %s -> %s"),
			*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
			OldPin->LinkedTo.Num(),
			*OldPin->PinName.ToString(),
			*NewPin->PinName.ToString());

		// Transfer each connection
		TArray<UEdGraphPin*> OldLinks = OldPin->LinkedTo;
		for (UEdGraphPin* LinkedPin : OldLinks)
		{
			OldPin->BreakLinkTo(LinkedPin);
			NewPin->MakeLinkTo(LinkedPin);
			Fixed++;
		}

		// Transfer default values
		if (!OldPin->DefaultValue.IsEmpty() && NewPin->DefaultValue.IsEmpty())
		{
			NewPin->DefaultValue = OldPin->DefaultValue;
		}
		if (OldPin->DefaultObject && !NewPin->DefaultObject)
		{
			NewPin->DefaultObject = OldPin->DefaultObject;
		}
		if (!OldPin->DefaultTextValue.IsEmpty() && NewPin->DefaultTextValue.IsEmpty())
		{
			NewPin->DefaultTextValue = OldPin->DefaultTextValue;
		}
	}

	if (Fixed > 0)
	{
		Node->GetGraph()->NotifyGraphChanged();
	}

	return Fixed;
}

#undef LOCTEXT_NAMESPACE
