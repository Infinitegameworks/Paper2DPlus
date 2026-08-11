// Copyright 2026 Infinite Gameworks. All Rights Reserved.

using UnrealBuildTool;

public class Paper2DPlusBlueprintNodes : ModuleRules
{
	public Paper2DPlusBlueprintNodes(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"Paper2DPlus",
			"BlueprintGraph"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"KismetCompiler",
			"UnrealEd"
		});
	}
}
