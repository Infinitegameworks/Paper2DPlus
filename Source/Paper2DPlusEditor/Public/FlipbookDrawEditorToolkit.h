// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"

class UPaperFlipbook;
class FFlipbookDrawModel;
class SFlipbookDrawCanvas;
class SWidget;

/**
 * FFlipbookDrawEditorToolkit — standalone dockable editor for pixel-drawing on a UPaperFlipbook's frames.
 *
 * Opened from the Content Browser right-click on a UPaperFlipbook ("Edit Frames (Paper2D+ Draw)…"); the
 * engine owns the flipbook's double-click editor, so a right-click launcher is the idiomatic hook.
 * Cloned from FCharacterLayerAssetEditorToolkit (FAssetEditorToolkit + FTabManager) but with its own
 * lightweight FFlipbookDrawModel (no FCharacterProfileEditorModel coupling).
 */
class FFlipbookDrawEditorToolkit : public FAssetEditorToolkit
{
public:
	virtual ~FFlipbookDrawEditorToolkit();

	void InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UPaperFlipbook* InFlipbook);

	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

	static const FName CanvasTabId;
	static const FName ToolsTabId;

private:
	UPaperFlipbook* EditedFlipbook = nullptr;
	TSharedPtr<FFlipbookDrawModel> Model;
	TSharedPtr<SFlipbookDrawCanvas> Canvas;

	TSharedRef<SDockTab> SpawnTab_Canvas(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Tools(const FSpawnTabArgs& Args);
	TSharedRef<SWidget> BuildFrameStrip();
};
