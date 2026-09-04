// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The Content Browser door for Aseprite files.
 *
 * Dropping files on the asset view normally runs the engine's per-file import loop, and that loop
 * stops the moment a factory reports a cancel — which UAsepriteFactory must do, because the file is a
 * batch SOURCE for the Bulk Sprite Extractor rather than an asset of its own. So a five-file drop
 * delivered exactly one file. This extender claims the drop BEFORE that loop and hands the whole
 * list to the window in one call; anything in the drop that is not an Aseprite file still goes down
 * the ordinary import road.
 */
namespace Paper2DPlusEditor::AsepriteContentBrowserDrop
{
	/** True for a `.ase` / `.aseprite` path, case-insensitive. */
	bool IsAsepriteFile(const FString& Path);

	/** Split a dropped file list into Aseprite files and everything else. Order is preserved on both
	 *  sides, so a batch lands in the window in the order the user picked the files. */
	void SplitDroppedFiles(const TArray<FString>& Files, TArray<FString>& OutAseFiles, TArray<FString>& OutOtherFiles);

	/** Register the asset-view drag-and-drop extender. Once per module load. */
	void Register();

	/** Remove it again. Safe when the Content Browser module has already gone. */
	void Unregister();
}
