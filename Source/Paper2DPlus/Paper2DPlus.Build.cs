// Copyright 2026 Infinite Gameworks. All Rights Reserved.

using UnrealBuildTool;

public class Paper2DPlus : ModuleRules
{
	public Paper2DPlus(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"Paper2D",
			"Json",
			"JsonUtilities",
			"GameplayTags",
			"DeveloperSettings"
		});
	}
}
