#include "ModPackagerModule.h"
#include "ModPackager.h"

#include "ContentBrowserModule.h"
#include "ContentBrowserDelegates.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Textures/SlateIcon.h"
#include "EditorStyleSet.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FModPackagerModule"

namespace
{
	/** Builds the "Package Mod" entry shown when right-clicking a single mod folder. */
	TSharedRef<FExtender> OnExtendPathViewContextMenu(const TArray<FString>& SelectedPaths)
	{
		TSharedRef<FExtender> Extender = MakeShared<FExtender>();

		if (SelectedPaths.Num() == 1 && FModPackager::IsPackageableModFolder(SelectedPaths[0]))
		{
			const FString Path = SelectedPaths[0];
			Extender->AddMenuExtension(
				"PathContextBulkOperations",
				EExtensionHook::After,
				nullptr,
				FMenuExtensionDelegate::CreateLambda([Path](FMenuBuilder& MenuBuilder)
				{
					MenuBuilder.BeginSection("ModPackager", LOCTEXT("ModPackagerSection", "Mod Packager"));
					MenuBuilder.AddMenuEntry(
						LOCTEXT("PackageMod", "Package Mod"),
						LOCTEXT("PackageModTip", "Cook only this folder, build <ModName>.pak, then deploy it."),
						FSlateIcon(FEditorStyle::GetStyleSetName(), "Level.SaveIcon16x"),
						FUIAction(FExecuteAction::CreateLambda([Path]()
						{
							FModPackager::PackageMod(Path);
						})));
					MenuBuilder.EndSection();
				}));
		}

		return Extender;
	}
}

void FModPackagerModule::StartupModule()
{
	RegisterContentBrowserMenu();
}

void FModPackagerModule::ShutdownModule()
{
	UnregisterContentBrowserMenu();
}

void FModPackagerModule::RegisterContentBrowserMenu()
{
	if (!FModuleManager::Get().IsModuleLoaded("ContentBrowser"))
	{
		return;
	}

	FContentBrowserModule& CBModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	TArray<FContentBrowserMenuExtender_SelectedPaths>& Extenders =
		CBModule.GetAllPathViewContextMenuExtenders();

	Extenders.Add(FContentBrowserMenuExtender_SelectedPaths::CreateStatic(&OnExtendPathViewContextMenu));
	PathViewExtenderHandle = Extenders.Last().GetHandle();
}

void FModPackagerModule::UnregisterContentBrowserMenu()
{
	if (!PathViewExtenderHandle.IsValid() || !FModuleManager::Get().IsModuleLoaded("ContentBrowser"))
	{
		return;
	}

	FContentBrowserModule& CBModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	CBModule.GetAllPathViewContextMenuExtenders().RemoveAll(
		[this](const FContentBrowserMenuExtender_SelectedPaths& Delegate)
		{
			return Delegate.GetHandle() == PathViewExtenderHandle;
		});

	PathViewExtenderHandle.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FModPackagerModule, ModPackager)
