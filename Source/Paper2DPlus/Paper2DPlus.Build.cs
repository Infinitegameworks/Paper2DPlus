// Copyright 2026 Infinite Gameworks. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

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
			"DeveloperSettings",
			"AssetRegistry"
		});

		// U30 hybrid appearance backend: runtime global-shader registration and the GPU-only
		// premultiplied-color/alpha resolve. No plugin content asset is required or staged.
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Projects",
			"RenderCore",
			"RHI"
		});

		// Release tooling can force the genuinely optional configuration even on a machine where
		// every installed engine happens to contain PaperZD. The default remains auto-detection.
		bool bForceNoPaperZD = string.Equals(
			Environment.GetEnvironmentVariable("PAPER2DPLUS_FORCE_NO_PAPERZD"),
			"1",
			StringComparison.Ordinal);
		bool bHasPaperZD = false;
		if (bForceNoPaperZD)
		{
			Console.WriteLine("[Paper2DPlus] PAPER2DPLUS_FORCE_NO_PAPERZD=1: WITH_PAPERZD=0; PaperZD module dependency omitted.");
		}

		// Probe for PaperZD as a sibling plugin (project Plugins dir)
		string SiblingPath = Path.Combine(PluginDirectory, "..", "PaperZD");
		if (!bForceNoPaperZD && Directory.Exists(SiblingPath))
		{
			bHasPaperZD = true;
		}

		// Probe for PaperZD in engine Marketplace plugins (directory name may include a hash suffix)
		if (!bForceNoPaperZD && !bHasPaperZD)
		{
			string MarketplaceDir = Path.GetFullPath(Path.Combine(EngineDirectory, "Plugins", "Marketplace"));
			if (Directory.Exists(MarketplaceDir))
			{
				foreach (string Dir in Directory.GetDirectories(MarketplaceDir, "PaperZD*"))
				{
					bHasPaperZD = true;
					break;
				}
			}
		}

		if (bHasPaperZD)
		{
			PublicDependencyModuleNames.Add("PaperZD");
			PublicDefinitions.Add("WITH_PAPERZD=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_PAPERZD=0");
		}
	}
}
