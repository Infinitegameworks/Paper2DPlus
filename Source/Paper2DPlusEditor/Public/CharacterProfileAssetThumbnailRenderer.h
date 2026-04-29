// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ThumbnailRendering/DefaultSizedThumbnailRenderer.h"
#include "CharacterProfileAssetThumbnailRenderer.generated.h"

class UPaper2DPlusCharacterProfileAsset;
class UPaperSprite;

/**
 * Renders a preview thumbnail for `UPaper2DPlusCharacterProfileAsset` in the Content Browser.
 *
 * Picks the first flipbook with a resolvable first-frame sprite and draws that sprite
 * scaled-to-fit the thumbnail canvas (aspect-preserving letterbox/pillarbox). Replaces
 * the stock generic asset icon so profile assets are scannable at a glance.
 *
 * Registered in `FPaper2DPlusEditorModule::StartupModule` via `UThumbnailManager`.
 */
UCLASS()
class PAPER2DPLUSEDITOR_API UPaper2DPlusCharacterProfileThumbnailRenderer : public UDefaultSizedThumbnailRenderer
{
	GENERATED_BODY()

public:
	virtual bool CanVisualizeAsset(UObject* Object) override;

	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
		FRenderTarget* Viewport, FCanvas* Canvas, bool bAdditionalViewFamily) override;

private:
	/** Walk the asset's flipbooks and return the first frame-0 sprite found, or nullptr. */
	static UPaperSprite* PickRepresentativeSprite(const UPaper2DPlusCharacterProfileAsset* Asset);
};
