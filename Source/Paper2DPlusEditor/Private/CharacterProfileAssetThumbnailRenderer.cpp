// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterProfileAssetThumbnailRenderer.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Engine/Texture2D.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"

/** FCharacterProfileAssetThumbnailRenderer — Custom Content Browser thumbnail rendering for CharacterProfile assets. */

bool UPaper2DPlusCharacterProfileThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	const UPaper2DPlusCharacterProfileAsset* Asset = Cast<UPaper2DPlusCharacterProfileAsset>(Object);
	return Asset && PickRepresentativeSprite(Asset) != nullptr;
}

UPaperSprite* UPaper2DPlusCharacterProfileThumbnailRenderer::PickRepresentativeSprite(const UPaper2DPlusCharacterProfileAsset* Asset)
{
	if (!Asset) return nullptr;

	auto ResolveFirstFrameSprite = [](const FFlipbookProfileEntry& Entry) -> UPaperSprite*
	{
		UPaperFlipbook* FB = Entry.Identity.Flipbook.IsValid()
			? Entry.Identity.Flipbook.Get()
			: Entry.Identity.Flipbook.LoadSynchronous();
		if (!FB || FB->GetNumKeyFrames() == 0) return nullptr;
		return FB->GetKeyFrameChecked(0).Sprite;
	};

	// Honor user override first: look up `ThumbnailFlipbookName` in the Flipbooks array.
	// Falls through to the first-valid-sprite scan if the named flipbook is missing or
	// its first-frame sprite has been deleted.
	if (!Asset->ThumbnailFlipbookName.IsEmpty())
	{
		for (const FFlipbookProfileEntry& Entry : Asset->Flipbooks)
		{
			if (Entry.Identity.FlipbookName == Asset->ThumbnailFlipbookName)
			{
				if (UPaperSprite* S = ResolveFirstFrameSprite(Entry))
				{
					return S;
				}
				break; // name match but no sprite — fall through to fallback scan
			}
		}
	}

	// Fallback: first flipbook with a resolvable first-frame sprite.
	for (const FFlipbookProfileEntry& Entry : Asset->Flipbooks)
	{
		if (UPaperSprite* S = ResolveFirstFrameSprite(Entry)) return S;
	}
	return nullptr;
}

void UPaper2DPlusCharacterProfileThumbnailRenderer::Draw(
	UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
	FRenderTarget* Viewport, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	const UPaper2DPlusCharacterProfileAsset* Asset = Cast<UPaper2DPlusCharacterProfileAsset>(Object);
	if (!Asset) return;

	UPaperSprite* Sprite = PickRepresentativeSprite(Asset);
	if (!Sprite) return;

	UTexture* SourceTex = Sprite->GetSourceTexture();
	if (!SourceTex || !SourceTex->GetResource()) return;

	const FVector2D SourceUV = Sprite->GetSourceUV();
	const FVector2D SourceDims = Sprite->GetSourceSize();
	const float TexW = static_cast<float>(SourceTex->GetSurfaceWidth());
	const float TexH = static_cast<float>(SourceTex->GetSurfaceHeight());
	if (SourceDims.X <= 0.0f || SourceDims.Y <= 0.0f || TexW <= 0.0f || TexH <= 0.0f) return;

	// Aspect-preserving fit inside the thumbnail viewport: pillarbox for tall sprites,
	// letterbox for wide ones. Leave a tiny 2px margin so the sprite doesn't bleed to the edge.
	const float Margin = 2.0f;
	const float AvailW = FMath::Max(1.0f, static_cast<float>(Width) - 2.0f * Margin);
	const float AvailH = FMath::Max(1.0f, static_cast<float>(Height) - 2.0f * Margin);
	const float Scale = FMath::Min(AvailW / SourceDims.X, AvailH / SourceDims.Y);
	const float DrawW = SourceDims.X * Scale;
	const float DrawH = SourceDims.Y * Scale;
	const float DrawX = static_cast<float>(X) + (Width - DrawW) * 0.5f;
	const float DrawY = static_cast<float>(Y) + (Height - DrawH) * 0.5f;

	// Dark backing so non-square sprites have a consistent frame. Matches editor content-browser tones.
	FCanvasTileItem Backing(
		FVector2D(static_cast<float>(X), static_cast<float>(Y)),
		FVector2D(static_cast<float>(Width), static_cast<float>(Height)),
		FLinearColor(0.05f, 0.05f, 0.05f, 1.0f));
	Backing.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(Backing);

	// UV rect of the sprite within its source texture.
	const FVector2D UV0(SourceUV.X / TexW, SourceUV.Y / TexH);
	const FVector2D UV1((SourceUV.X + SourceDims.X) / TexW, (SourceUV.Y + SourceDims.Y) / TexH);
	const FVector2D UVSize = UV1 - UV0;

	FCanvasTileItem SpriteTile(
		FVector2D(DrawX, DrawY),
		SourceTex->GetResource(),
		FVector2D(DrawW, DrawH),
		UV0,
		UV1,
		FLinearColor::White);
	SpriteTile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(SpriteTile);
}
