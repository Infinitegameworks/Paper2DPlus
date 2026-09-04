// Copyright 2026 Infinite Gameworks. All Rights Reserved.

using UnrealBuildTool;

public class Paper2DPlusEditor : ModuleRules
{
	public Paper2DPlusEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"Paper2DPlus",
			"Paper2D"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"UnrealEd",
			"Paper2DPlusBlueprintNodes",
			"Slate",
			"SlateCore",
			"DataValidation",
			"AssetTools",
			"AssetRegistry",
			"ContentBrowser",
			"ContentBrowserData",  // virtual -> internal path conversion for the .ase Content Browser drop extender
			"InputCore",
			"PropertyEditor",
			"EditorFramework",
			"ToolMenus",
			"DesktopPlatform",
			"DirectoryWatcher",
			"GraphEditor",
			"Kismet",
			"BlueprintGraph",
			"KismetCompiler",
			"MessageLog",
			"GameplayTags",
			"GameplayTagsEditor",
			"Settings",
			"DeveloperSettings",
			"AppFramework",
			"ClassViewer",
			"ApplicationCore",
			"Json",
			"Projects",  // IPluginManager — locate the plugin Resources/ dir for FPaper2DPlusEditorStyle
			"ImageWrapper"  // PNG fixture I/O for the de-bake real-data validation test (TASK-107)
		});

		// UE 5.0: FEditorStyle lives in EditorStyle module (deprecated in 5.1+, replaced by FAppStyle in SlateCore)
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion == 0)
		{
			PrivateDependencyModuleNames.Add("EditorStyle");
		}

		// UE 5.8: hierarchical asset-creation menu paths are implemented by AssetDefinition.
		if (Target.Version.MajorVersion > 5 || (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 8))
		{
			PrivateDependencyModuleNames.Add("AssetDefinition");
		}

	}
}
