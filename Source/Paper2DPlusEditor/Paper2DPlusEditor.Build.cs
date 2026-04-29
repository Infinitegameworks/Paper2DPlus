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
			"Slate",
			"SlateCore",
			"DataValidation",
			"AssetTools",
			"AssetRegistry",
			"ContentBrowser",
			"InputCore",
			"PropertyEditor",
			"EditorFramework",
			"ToolMenus",
			"DesktopPlatform",
			"DirectoryWatcher",
			"MessageLog",
			"GameplayTagsEditor",
			"AppFramework",
			"ClassViewer",
			"Json"
		});

		// UE 5.0: FEditorStyle lives in EditorStyle module (deprecated in 5.1+, replaced by FAppStyle in SlateCore)
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion == 0)
		{
			PrivateDependencyModuleNames.Add("EditorStyle");
		}
	}
}
