using UnrealBuildTool;

public class ModPackager : ModuleRules
{
	public ModPackager(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"Projects",
			"EditorStyle",
			"InputCore",
			"ContentBrowser",
			"ToolMenus",
			"DesktopPlatform",
		});
	}
}
