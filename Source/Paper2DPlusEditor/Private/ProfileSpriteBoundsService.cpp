// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ProfileSpriteBoundsService.h"

#include "SpriteExtractionUtils.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Engine/Texture2D.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "ProfileSpriteBoundsService"

namespace ProfileSpriteBoundsServiceInternal
{
	struct FTexturePixels
	{
		TArray<FColor> Pixels;
		int32 Width = 0;
		int32 Height = 0;
		bool bLoadAttempted = false;
		bool bLoaded = false;
	};

	FIntRect GetSourceBounds(const UPaperSprite& Sprite)
	{
		const FVector2D SourceUV = Sprite.GetSourceUV();
		const FVector2D SourceSize = Sprite.GetSourceSize();
		const FIntPoint Min(FMath::RoundToInt(SourceUV.X), FMath::RoundToInt(SourceUV.Y));
		const FIntPoint Size(FMath::RoundToInt(SourceSize.X), FMath::RoundToInt(SourceSize.Y));
		return FIntRect(Min, Min + Size);
	}

	/** Healthy = the sprite's live pivot sits ON the reconstructed frame anchor, whatever the pivot
	 *  mode. Baked-pivot extractions (TrimOffset zero) satisfy this by construction; canonical-era
	 *  assets (centered pivot + non-zero TrimOffset) fail it and repair converts them to the baked
	 *  model. */
	bool PivotSitsOnAnchor(UPaperSprite& Sprite, const FIntPoint& Anchor)
	{
		const FVector2D Pivot = Sprite.GetPivotPosition();
		return FMath::FloorToInt(Pivot.X) == Anchor.X
			&& FMath::FloorToInt(Pivot.Y) == Anchor.Y;
	}

	void Recount(FProfileSpriteBoundsReport& Report)
	{
		Report.HealthyFrames = 0;
		Report.RepairableFrames = 0;
		Report.EmptyFrames = 0;
		Report.UnsupportedFrames = 0;
		Report.AttentionFrames = 0;

		for (FProfileSpriteBoundsFlipbookReport& Flipbook : Report.Flipbooks)
		{
			Flipbook.HealthyFrames = 0;
			Flipbook.RepairableFrames = 0;
			Flipbook.EmptyFrames = 0;
			Flipbook.UnsupportedFrames = 0;
			Flipbook.AttentionFrames = 0;
			for (const FProfileSpriteBoundsFrameReport& Frame : Flipbook.Frames)
			{
				if (Frame.Kind == EProfileSpriteBoundsFrameKind::Unsupported)
				{
					++Flipbook.UnsupportedFrames;
					++Report.UnsupportedFrames;
					// Unsupported is a PROBLEM the designer must act on, even though this check
					// cannot fix it. Counting it only under "unsupported" left it invisible to any
					// surface that asked "is there anything to do here?" via HasRepairs().
					++Flipbook.AttentionFrames;
					++Report.AttentionFrames;
					continue;
				}
				if (Frame.Kind == EProfileSpriteBoundsFrameKind::Empty)
				{
					++Flipbook.EmptyFrames;
					++Report.EmptyFrames;
				}
				if (Frame.bNeedsRepair)
				{
					++Flipbook.RepairableFrames;
					++Report.RepairableFrames;
					++Flipbook.AttentionFrames;
					++Report.AttentionFrames;
				}
				else
				{
					++Flipbook.HealthyFrames;
					++Report.HealthyFrames;
				}
			}
		}
	}
}

FProfileSpriteBoundsReport FProfileSpriteBoundsService::AnalyzeProfile(
	UPaper2DPlusCharacterProfileAsset* Profile)
{
	using namespace ProfileSpriteBoundsServiceInternal;

	FProfileSpriteBoundsReport Report;
	if (!Profile)
	{
		return Report;
	}

	Report.FlipbooksChecked = Profile->Flipbooks.Num();
	TMap<UTexture2D*, FTexturePixels> TextureCache;

	for (int32 EntryIndex = 0; EntryIndex < Profile->Flipbooks.Num(); ++EntryIndex)
	{
		const FFlipbookProfileEntry& Entry = Profile->Flipbooks[EntryIndex];
		FProfileSpriteBoundsFlipbookReport& FlipbookReport = Report.Flipbooks.AddDefaulted_GetRef();
		FlipbookReport.EntryIndex = EntryIndex;
		FlipbookReport.FlipbookName = Entry.Identity.FlipbookName;

		UPaperFlipbook* Flipbook = Entry.Identity.Flipbook.LoadSynchronous();
		if (!Flipbook)
		{
			FProfileSpriteBoundsFrameReport& Frame = FlipbookReport.Frames.AddDefaulted_GetRef();
			Frame.EntryIndex = EntryIndex;
			Frame.Diagnostic = TEXT("Flipbook could not be loaded.");
			continue;
		}
		if (FlipbookReport.FlipbookName.IsEmpty())
		{
			FlipbookReport.FlipbookName = Flipbook->GetName();
		}

		const int32 NumFrames = Flipbook->GetNumKeyFrames();
		if (NumFrames == 0)
		{
			FProfileSpriteBoundsFrameReport& Frame = FlipbookReport.Frames.AddDefaulted_GetRef();
			Frame.EntryIndex = EntryIndex;
			Frame.Diagnostic = TEXT("Flipbook has no frames.");
			continue;
		}

		TArray<FSpriteMaxExtentFrame> ExtentFrames;
		TArray<int32> ExtentToReportIndex;
		ExtentFrames.Reserve(NumFrames);
		ExtentToReportIndex.Reserve(NumFrames);

		for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
		{
			FProfileSpriteBoundsFrameReport& FrameReport = FlipbookReport.Frames.AddDefaulted_GetRef();
			FrameReport.EntryIndex = EntryIndex;
			FrameReport.FrameIndex = FrameIndex;

			const FPaperFlipbookKeyFrame& KeyFrame = Flipbook->GetKeyFrameChecked(FrameIndex);
			UPaperSprite* Sprite = KeyFrame.Sprite;
			FrameReport.Sprite = Sprite;
			if (!Sprite)
			{
				FrameReport.Diagnostic = TEXT("Frame has no sprite.");
				continue;
			}

			UTexture2D* Texture = Cast<UTexture2D>(Sprite->GetSourceTexture());
			FrameReport.Texture = Texture;
			if (!Texture)
			{
				FrameReport.Diagnostic = TEXT("Sprite source is not a Texture2D.");
				continue;
			}

			FTexturePixels& TexturePixels = TextureCache.FindOrAdd(Texture);
			if (!TexturePixels.bLoadAttempted)
			{
				TexturePixels.bLoadAttempted = true;
				TexturePixels.bLoaded = FSpriteExtractionUtils::LoadTextureData(
					Texture, TexturePixels.Pixels, TexturePixels.Width, TexturePixels.Height);
			}
			if (!TexturePixels.bLoaded)
			{
				FrameReport.Diagnostic = TEXT("Texture source pixels are unavailable.");
				continue;
			}

			FrameReport.CurrentBounds = GetSourceBounds(*Sprite);
			if (FrameReport.CurrentBounds.Width() <= 0 || FrameReport.CurrentBounds.Height() <= 0
				|| FrameReport.CurrentBounds.Min.X < 0
				|| FrameReport.CurrentBounds.Min.Y < 0
				|| FrameReport.CurrentBounds.Max.X > TexturePixels.Width
				|| FrameReport.CurrentBounds.Max.Y > TexturePixels.Height)
			{
				FrameReport.Diagnostic = TEXT("Sprite source region is outside its texture.");
				continue;
			}

			const FSpriteExtractionInfo* Info = Entry.CombatData.FrameExtractionInfo.IsValidIndex(FrameIndex)
				? &Entry.CombatData.FrameExtractionInfo[FrameIndex]
				: nullptr;
			const int32 AlphaThreshold = FMath::Clamp(
				Info ? Info->AlphaThreshold : Entry.AlignmentData.AlphaThreshold, 1, 255);
			FrameReport.TightBounds = FSpriteExtractionUtils::FindTightContentBounds(
				TexturePixels.Pixels,
				TexturePixels.Width,
				TexturePixels.Height,
				FrameReport.CurrentBounds,
				AlphaThreshold);

			const FVector2D Pivot = Sprite->GetPivotPosition();
			const FIntPoint ExistingTrim = Info ? Info->TrimOffset : FIntPoint::ZeroValue;
			FrameReport.Anchor = FIntPoint(
				FMath::FloorToInt(Pivot.X) - ExistingTrim.X,
				FMath::FloorToInt(Pivot.Y) - ExistingTrim.Y);

			FSpriteMaxExtentFrame& ExtentFrame = ExtentFrames.AddDefaulted_GetRef();
			ExtentFrame.ContainerBounds = FrameReport.CurrentBounds;
			ExtentFrame.TightBounds = FrameReport.TightBounds;
			ExtentFrame.Anchor = FrameReport.Anchor;
			ExtentToReportIndex.Add(FlipbookReport.Frames.Num() - 1);
		}

		const FSpriteMaxExtentResult ExtentResult =
			FSpriteExtractionUtils::ComputeMaxExtentTargets(ExtentFrames);
		FlipbookReport.TargetSize = ExtentResult.GetUniformSize();

		// LOOP-INVARIANT: whether ANY non-empty frame could establish uniform bounds is a property of
		// the whole animation, not of an individual frame, so it is decided once here instead of being
		// re-tested on every iteration below.
		const bool bExtentResultValid = ExtentResult.IsValid();

		for (int32 ExtentIndex = 0; ExtentIndex < ExtentFrames.Num(); ++ExtentIndex)
		{
			FProfileSpriteBoundsFrameReport& FrameReport =
				FlipbookReport.Frames[ExtentToReportIndex[ExtentIndex]];
			const FSpriteMaxExtentFrame& ExtentFrame = ExtentFrames[ExtentIndex];

			if (!bExtentResultValid)
			{
				// TRUTHFULNESS FIX (R12): this branch used to fall through with Kind left at
				// Content/Empty and bNeedsRepair false, which Recount reads as HEALTHY — so an
				// animation whose bounds could not be established at all reported every frame as fine
				// and its explanation never reached a surface that lists problems. The frame is
				// unsupported, and it carries the reason.
				FrameReport.Kind = EProfileSpriteBoundsFrameKind::Unsupported;
				FrameReport.Diagnostic = TEXT("No non-empty frame is available to establish uniform bounds.");
				continue;
			}

			FrameReport.Kind = ExtentFrame.bEmpty
				? EProfileSpriteBoundsFrameKind::Empty
				: EProfileSpriteBoundsFrameKind::Content;

			if (!ExtentFrame.bTargetFits)
			{
				// Genuinely NOT repairable in place — repair cannot grow a sprite's source region, so
				// bNeedsRepair stays false on purpose and Repair All is correctly unavailable for it.
				// What was missing is that "unsupported" was invisible to any surface gating on
				// HasRepairs(): the frame now lands in AttentionFrames, so the count the designer sees
				// matches the instruction this diagnostic gives them.
				FrameReport.Kind = EProfileSpriteBoundsFrameKind::Unsupported;
				FrameReport.Diagnostic = TEXT("The uniform target exceeds this sprite's current source region; re-extract it first.");
				continue;
			}

			FrameReport.TargetBounds = ExtentFrame.TargetBounds;
			FrameReport.HitboxDelta = FrameReport.CurrentBounds.Min - FrameReport.TargetBounds.Min;

			const FSpriteExtractionInfo* Info = Entry.CombatData.FrameExtractionInfo.IsValidIndex(FrameReport.FrameIndex)
				? &Entry.CombatData.FrameExtractionInfo[FrameReport.FrameIndex]
				: nullptr;
			UPaperSprite* Sprite = FrameReport.Sprite.Get();
			// SourceOffset is deliberately NOT part of the health check: fresh bulk extractions record
			// the pre-pack source-sheet origin, which cannot be reconstructed from the packed texture.
			// Repair still normalizes it to the texture-local tight origin when a real repair runs.
			FrameReport.bNeedsRepair = !Sprite
				|| FrameReport.CurrentBounds != FrameReport.TargetBounds
				|| !PivotSitsOnAnchor(*Sprite, FrameReport.Anchor)
				|| !Info
				|| Info->TrimOffset != FIntPoint::ZeroValue;
			FrameReport.Diagnostic = FrameReport.bNeedsRepair
				? TEXT("Source bounds or the shared baked anchor pivot need repair.")
				: (ExtentFrame.bEmpty ? TEXT("Empty frame is normalized and does not affect the maximum.") : TEXT("Bounds are healthy."));
		}
	}

	// A shared sprite may be repaired once only when every Profile use agrees on its source target.
	TMap<UPaperSprite*, FIntRect> SharedTargets;
	TSet<UPaperSprite*> ConflictingSprites;
	for (const FProfileSpriteBoundsFlipbookReport& Flipbook : Report.Flipbooks)
	{
		for (const FProfileSpriteBoundsFrameReport& Frame : Flipbook.Frames)
		{
			UPaperSprite* Sprite = Frame.Sprite.Get();
			if (!Sprite || Frame.Kind == EProfileSpriteBoundsFrameKind::Unsupported)
			{
				continue;
			}
			if (const FIntRect* Existing = SharedTargets.Find(Sprite))
			{
				if (*Existing != Frame.TargetBounds)
				{
					ConflictingSprites.Add(Sprite);
				}
			}
			else
			{
				SharedTargets.Add(Sprite, Frame.TargetBounds);
			}
		}
	}
	if (ConflictingSprites.Num() > 0)
	{
		for (FProfileSpriteBoundsFlipbookReport& Flipbook : Report.Flipbooks)
		{
			for (FProfileSpriteBoundsFrameReport& Frame : Flipbook.Frames)
			{
				if (ConflictingSprites.Contains(Frame.Sprite.Get()))
				{
					Frame.Kind = EProfileSpriteBoundsFrameKind::Unsupported;
					Frame.bNeedsRepair = false;
					Frame.Diagnostic = TEXT("This sprite is shared by Profile frames that require different bounds.");
				}
			}
		}
	}

	// Repair All owns only this Profile's metadata transaction. Refuse to mutate a sprite that another
	// Character Profile also uses, because that other Profile's hitboxes/trim metadata would otherwise
	// remain in the old coordinate space.
	TSet<UPaperSprite*> ProfileSprites;
	TSet<UTexture2D*> ProfileTextures;
	for (const FProfileSpriteBoundsFlipbookReport& Flipbook : Report.Flipbooks)
	{
		for (const FProfileSpriteBoundsFrameReport& Frame : Flipbook.Frames)
		{
			if (UPaperSprite* Sprite = Frame.Sprite.Get())
			{
				ProfileSprites.Add(Sprite);
			}
			if (UTexture2D* Texture = Frame.Texture.Get())
			{
				ProfileTextures.Add(Texture);
			}
		}
	}

	TSet<UPaperSprite*> ExternallySharedSprites;
	if (Profile->GetOutermost() != GetTransientPackage())
	{
		for (UTexture2D* Texture : ProfileTextures)
		{
			for (UPaper2DPlusCharacterProfileAsset* OtherProfile :
				FSpriteExtractionUtils::FindProfilesReferencingTexture(Texture))
			{
				if (!OtherProfile || OtherProfile == Profile)
				{
					continue;
				}
				for (const FFlipbookProfileEntry& OtherEntry : OtherProfile->Flipbooks)
				{
					UPaperFlipbook* OtherFlipbook = OtherEntry.Identity.Flipbook.LoadSynchronous();
					if (!OtherFlipbook)
					{
						continue;
					}
					for (int32 OtherFrameIndex = 0; OtherFrameIndex < OtherFlipbook->GetNumKeyFrames(); ++OtherFrameIndex)
					{
						UPaperSprite* OtherSprite = OtherFlipbook->GetKeyFrameChecked(OtherFrameIndex).Sprite;
						if (ProfileSprites.Contains(OtherSprite))
						{
							ExternallySharedSprites.Add(OtherSprite);
						}
					}
				}
			}
		}
	}
	if (ExternallySharedSprites.Num() > 0)
	{
		for (FProfileSpriteBoundsFlipbookReport& Flipbook : Report.Flipbooks)
		{
			for (FProfileSpriteBoundsFrameReport& Frame : Flipbook.Frames)
			{
				if (ExternallySharedSprites.Contains(Frame.Sprite.Get()))
				{
					Frame.Kind = EProfileSpriteBoundsFrameKind::Unsupported;
					Frame.bNeedsRepair = false;
					Frame.Diagnostic = TEXT("This sprite is shared by another Character Profile; repair it through the shared source workflow.");
				}
			}
		}
	}

	Recount(Report);
	return Report;
}

FProfileSpriteBoundsRepairStats FProfileSpriteBoundsService::RepairProfile(
	UPaper2DPlusCharacterProfileAsset* Profile,
	FProfileSpriteBoundsReport& OutPostRepairReport,
	bool bCreateTransaction)
{
	using namespace ProfileSpriteBoundsServiceInternal;

	FProfileSpriteBoundsRepairStats Stats;
	FProfileSpriteBoundsReport Report = AnalyzeProfile(Profile);
	if (!Profile || !Report.HasRepairs())
	{
		OutPostRepairReport = MoveTemp(Report);
		return Stats;
	}

	TUniquePtr<FScopedTransaction> Transaction;
	if (bCreateTransaction)
	{
		Transaction = MakeUnique<FScopedTransaction>(LOCTEXT("RepairProfileSpriteBounds", "Repair Character Profile Sprite Bounds"));
	}
	Profile->SetFlags(RF_Transactional);
	Profile->Modify();

	TSet<UPaperSprite*> ModifiedSprites;
	for (const FProfileSpriteBoundsFlipbookReport& FlipbookReport : Report.Flipbooks)
	{
		if (!Profile->Flipbooks.IsValidIndex(FlipbookReport.EntryIndex))
		{
			continue;
		}
		FFlipbookProfileEntry& Entry = Profile->Flipbooks[FlipbookReport.EntryIndex];
		UPaperFlipbook* Flipbook = Entry.Identity.Flipbook.LoadSynchronous();
		if (!Flipbook)
		{
			continue;
		}
		if (Entry.CombatData.FrameExtractionInfo.Num() < Flipbook->GetNumKeyFrames())
		{
			Entry.CombatData.FrameExtractionInfo.SetNum(Flipbook->GetNumKeyFrames());
		}

		for (const FProfileSpriteBoundsFrameReport& FrameReport : FlipbookReport.Frames)
		{
			if (!FrameReport.bNeedsRepair
				|| FrameReport.Kind == EProfileSpriteBoundsFrameKind::Unsupported
				|| !Entry.CombatData.FrameExtractionInfo.IsValidIndex(FrameReport.FrameIndex)
				|| FrameReport.FrameIndex < 0
				|| FrameReport.FrameIndex >= Flipbook->GetNumKeyFrames())
			{
				continue;
			}

			UPaperSprite* Sprite = Flipbook->GetKeyFrameChecked(FrameReport.FrameIndex).Sprite;
			UTexture2D* Texture = FrameReport.Texture.Get();
			if (!Sprite || !Texture || Sprite != FrameReport.Sprite.Get())
			{
				continue;
			}

			if (!ModifiedSprites.Contains(Sprite))
			{
				Sprite->SetFlags(RF_Transactional);
				Sprite->Modify();
				FSpriteExtractionUtils::UpdateSpriteSourceRegion(
					Sprite, Texture, FrameReport.TargetBounds, FrameReport.HitboxDelta);
				// Bake the shared per-flipbook anchor into the sprite as a custom pivot (texture
				// space) — the same reference the bulk extractor's trim path bakes, so repaired
				// flipbooks align in any renderer with no Profile offset applied.
				Sprite->SetPivotMode(
					ESpritePivotMode::Custom,
					FVector2D(FrameReport.Anchor.X, FrameReport.Anchor.Y),
					true);
				Sprite->PostEditChange();
				Sprite->MarkPackageDirty();
				ModifiedSprites.Add(Sprite);
				++Stats.SpritesModified;
			}

			if (FrameReport.HitboxDelta != FIntPoint::ZeroValue
				&& Entry.CombatData.Frames.IsValidIndex(FrameReport.FrameIndex))
			{
				FFrameHitboxData& FrameData = Entry.CombatData.Frames[FrameReport.FrameIndex];
				for (FHitboxData& Hitbox : FrameData.Hitboxes)
				{
					Hitbox.X += FrameReport.HitboxDelta.X;
					Hitbox.Y += FrameReport.HitboxDelta.Y;
				}
				for (FSocketData& Socket : FrameData.Sockets)
				{
					Socket.X += FrameReport.HitboxDelta.X;
					Socket.Y += FrameReport.HitboxDelta.Y;
				}
				Stats.HitboxesRemapped += FrameData.Hitboxes.Num();
				Stats.SocketsRemapped += FrameData.Sockets.Num();
			}

			FSpriteExtractionInfo& Info = Entry.CombatData.FrameExtractionInfo[FrameReport.FrameIndex];
			Info.SourceOffset = FrameReport.Kind == EProfileSpriteBoundsFrameKind::Empty
				? FrameReport.TargetBounds.Min
				: FrameReport.TightBounds.Min;
			// Alignment lives in the baked pivot; nothing re-applies TrimOffset at render time.
			Info.TrimOffset = FIntPoint::ZeroValue;
			Info.ExtractionTime = FDateTime::Now();
			Info.CachedPivotLocal = Sprite->GetPivotPosition() - Sprite->GetSourceUV();
			++Stats.FramesRepaired;
		}
	}

	Profile->MarkPackageDirty();
	Transaction.Reset();
	OutPostRepairReport = AnalyzeProfile(Profile);
	return Stats;
}

#undef LOCTEXT_NAMESPACE
