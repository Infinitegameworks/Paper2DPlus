// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusDirectionalAnimationLibrary.h"

#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"

namespace
{
	bool DirectionalAnimation_ReferenceMatches(
		const TSoftObjectPtr<UPaperFlipbook>& Reference,
		UPaperFlipbook* Flipbook,
		const FSoftObjectPath& FlipbookPath)
	{
		// The caller hoists FlipbookPath once per scan; building it per candidate was measurable
		// per-frame work on UE 5.0-5.5, where the FSoftObjectPath(UObject*) constructor stringizes.
		if (Reference.IsNull() || !Flipbook)
		{
			return false;
		}
		return Reference.Get() == Flipbook
			|| (!FlipbookPath.IsNull()
				&& Reference.ToSoftObjectPath() == FlipbookPath);
	}

	EPaper2DPlusDirectionalAnimationResult DirectionalAnimation_ResolveOwner(
		const UPaper2DPlusCharacterProfileAsset* Profile,
		UPaperFlipbook* Flipbook,
		const FFlipbookProfileEntry*& OutOwner,
		int32& OutOwnerIndex)
	{
		OutOwner = nullptr;
		OutOwnerIndex = INDEX_NONE;
		if (!Profile || !Flipbook)
		{
			return EPaper2DPlusDirectionalAnimationResult::InvalidRequest;
		}

		bool bPublishedOwnerAmbiguous = false;
		const FFlipbookProfileEntry* PublishedOwner =
			Profile->ResolveLogicalAnimationOwner(
			Flipbook,
			bPublishedOwnerAmbiguous,
			EPaper2DPlusLogicalOwnerDuplicatePolicy::PreserveBaseOnlyIterationWinner);

		// The general logical-owner resolver deliberately publishes only active, valid directional
		// aliases. Directional queries have one additional responsibility: a uniquely authored,
		// non-null stored variant must still reach its owner's structural gate so malformed data
		// reports InvalidProfileData instead of looking foreign. Keep this raw scan private to this
		// library so Animation Map and every other existing consumer retain their established aliasing.
		const FSoftObjectPath FlipbookPath(Flipbook);
		TArray<int32, TInlineAllocator<2>> StoredVariantOwnerIndices;
		TArray<int32, TInlineAllocator<2>> StoredBaseOwnerIndices;
		for (int32 OwnerIndex = 0; OwnerIndex < Profile->Flipbooks.Num(); ++OwnerIndex)
		{
			const FFlipbookProfileEntry& Entry = Profile->Flipbooks[OwnerIndex];
			if (DirectionalAnimation_ReferenceMatches(
				Entry.Identity.Flipbook, Flipbook, FlipbookPath))
			{
				StoredBaseOwnerIndices.Add(OwnerIndex);
			}
			for (const FPaper2DPlusDirectionalAnimationSlot& Slot :
				Entry.DirectionalAnimationData.Slots)
			{
				if (DirectionalAnimation_ReferenceMatches(
					Slot.Flipbook, Flipbook, FlipbookPath))
				{
					StoredVariantOwnerIndices.AddUnique(OwnerIndex);
					break;
				}
			}
		}

		if (bPublishedOwnerAmbiguous || StoredVariantOwnerIndices.Num() > 1)
		{
			return EPaper2DPlusDirectionalAnimationResult::AmbiguousFlipbook;
		}

		if (StoredVariantOwnerIndices.Num() == 1)
		{
			TArray<int32, TInlineAllocator<2>> AuthoredOwnerIndices =
				StoredVariantOwnerIndices;
			for (const int32 BaseOwnerIndex : StoredBaseOwnerIndices)
			{
				AuthoredOwnerIndices.AddUnique(BaseOwnerIndex);
			}
			if (AuthoredOwnerIndices.Num() > 1)
			{
				return EPaper2DPlusDirectionalAnimationResult::AmbiguousFlipbook;
			}

			const FFlipbookProfileEntry* StoredOwner =
				&Profile->Flipbooks[AuthoredOwnerIndices[0]];
			if (PublishedOwner && PublishedOwner != StoredOwner)
			{
				return EPaper2DPlusDirectionalAnimationResult::AmbiguousFlipbook;
			}
			OutOwner = StoredOwner;
		}
		else
		{
			OutOwner = PublishedOwner;
		}
		if (!OutOwner)
		{
			return EPaper2DPlusDirectionalAnimationResult::FlipbookNotInProfile;
		}

		OutOwnerIndex = Profile->Flipbooks.IndexOfByPredicate(
			[OutOwner](const FFlipbookProfileEntry& Candidate)
			{
				return &Candidate == OutOwner;
			});
		FPaper2DPlusDirectionalStructureResult StructureResult;
		if (OutOwnerIndex == INDEX_NONE
			|| !Profile->CheckDirectionalAnimationStructure(OutOwnerIndex, StructureResult))
		{
			OutOwner = nullptr;
			OutOwnerIndex = INDEX_NONE;
			return EPaper2DPlusDirectionalAnimationResult::InvalidProfileData;
		}

		return EPaper2DPlusDirectionalAnimationResult::Success;
	}

	bool DirectionalAnimation_HasExpectedOwner(
		const UPaper2DPlusCharacterProfileAsset* Profile,
		UPaperFlipbook* Flipbook,
		const FFlipbookProfileEntry* ExpectedOwner)
	{
		bool bAmbiguous = false;
		const FFlipbookProfileEntry* ResolvedOwner =
			Profile->ResolveLogicalAnimationOwner(
				Flipbook,
				bAmbiguous,
				EPaper2DPlusLogicalOwnerDuplicatePolicy::PreserveBaseOnlyIterationWinner);
		return !bAmbiguous && ResolvedOwner == ExpectedOwner;
	}
}

bool UPaper2DPlusDirectionalAnimationLibrary::HasMultiDirection(
	UPaper2DPlusCharacterProfileAsset* Profile,
	UPaperFlipbook* Flipbook)
{
	const FFlipbookProfileEntry* Owner = nullptr;
	int32 OwnerIndex = INDEX_NONE;
	return DirectionalAnimation_ResolveOwner(Profile, Flipbook, Owner, OwnerIndex)
			== EPaper2DPlusDirectionalAnimationResult::Success
		&& Profile->HasActiveDirectionalSlots(OwnerIndex);
}

EPaper2DPlusDirectionalAnimationResult
UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionalFlipbook(
	UPaper2DPlusCharacterProfileAsset* Profile,
	UPaperFlipbook* Flipbook,
	FVector2D Direction,
	UPaperFlipbook*& OutFlipbook,
	int32& OutSlotIndex,
	bool& bOutMirrorHorizontally)
{
	OutFlipbook = nullptr;
	OutSlotIndex = INDEX_NONE;
	bOutMirrorHorizontally = false;

	const FFlipbookProfileEntry* Owner = nullptr;
	int32 OwnerIndex = INDEX_NONE;
	const EPaper2DPlusDirectionalAnimationResult OwnerResult =
		DirectionalAnimation_ResolveOwner(Profile, Flipbook, Owner, OwnerIndex);
	if (OwnerResult != EPaper2DPlusDirectionalAnimationResult::Success)
	{
		return OwnerResult;
	}

	int32 DirectionCount = 0;
	float AngleOffsetDegrees = 0.0f;
	if (!Profile->GetEffectiveDirectionalSettings(
			OwnerIndex,
			DirectionCount,
			AngleOffsetDegrees))
	{
		return EPaper2DPlusDirectionalAnimationResult::InvalidProfileData;
	}

	int32 SelectedSlotIndex = INDEX_NONE;
	if (!UPaper2DPlusCharacterProfileAsset::ResolveDirectionalSlotIndex(
			Direction,
			DirectionCount,
			AngleOffsetDegrees,
			SelectedSlotIndex))
	{
		return EPaper2DPlusDirectionalAnimationResult::InvalidDirection;
	}

	if (!Profile->HasActiveDirectionalSlots(OwnerIndex))
	{
		UPaperFlipbook* CanonicalBase = Owner->Identity.Flipbook.LoadSynchronous();
		if (!CanonicalBase)
		{
			return EPaper2DPlusDirectionalAnimationResult::LoadFailed;
		}
		OutFlipbook = CanonicalBase;
		return EPaper2DPlusDirectionalAnimationResult::Success;
	}

	TSoftObjectPtr<UPaperFlipbook> SelectedFlipbook;
	bool bSelectedMirror = false;
	if (!Profile->GetDirectionalSlot(
			OwnerIndex,
			SelectedSlotIndex,
			SelectedFlipbook,
			bSelectedMirror))
	{
		// The one failure a sparse set produces every frame keeps reporting WHICH sector was
		// empty, so a Blueprint can implement its own nearest/base/hold fallback. Art stays null.
		OutSlotIndex = SelectedSlotIndex;
		return EPaper2DPlusDirectionalAnimationResult::DirectionUnoccupied;
	}

	UPaperFlipbook* LoadedFlipbook = SelectedFlipbook.LoadSynchronous();
	if (!LoadedFlipbook)
	{
		return EPaper2DPlusDirectionalAnimationResult::LoadFailed;
	}
	if (!DirectionalAnimation_HasExpectedOwner(Profile, LoadedFlipbook, Owner))
	{
		return EPaper2DPlusDirectionalAnimationResult::AmbiguousFlipbook;
	}

	OutFlipbook = LoadedFlipbook;
	OutSlotIndex = SelectedSlotIndex;
	bOutMirrorHorizontally = bSelectedMirror;
	return EPaper2DPlusDirectionalAnimationResult::Success;
}

EPaper2DPlusDirectionalAnimationResult
UPaper2DPlusDirectionalAnimationLibrary::GetOccupiedDirectionSlots(
	UPaper2DPlusCharacterProfileAsset* Profile,
	UPaperFlipbook* Flipbook,
	TArray<FPaper2DPlusOccupiedDirectionSlot>& OutSlots)
{
	OutSlots.Reset();

	const FFlipbookProfileEntry* Owner = nullptr;
	int32 OwnerIndex = INDEX_NONE;
	const EPaper2DPlusDirectionalAnimationResult OwnerResult =
		DirectionalAnimation_ResolveOwner(Profile, Flipbook, Owner, OwnerIndex);
	if (OwnerResult != EPaper2DPlusDirectionalAnimationResult::Success)
	{
		return OwnerResult;
	}

	TArray<const FPaper2DPlusDirectionalAnimationSlot*> OccupiedSlots;
	for (const FPaper2DPlusDirectionalAnimationSlot& Slot :
		Owner->DirectionalAnimationData.Slots)
	{
		// The shared structural gate already proved every occupied slot is active and uniquely keyed.
		if (!Slot.Flipbook.IsNull())
		{
			OccupiedSlots.Add(&Slot);
		}
	}
	OccupiedSlots.Sort(
		[](const FPaper2DPlusDirectionalAnimationSlot& A,
			const FPaper2DPlusDirectionalAnimationSlot& B)
		{
			return A.SlotIndex < B.SlotIndex;
		});

	TArray<FPaper2DPlusOccupiedDirectionSlot> LoadedSlots;
	LoadedSlots.Reserve(OccupiedSlots.Num());
	for (const FPaper2DPlusDirectionalAnimationSlot* Slot : OccupiedSlots)
	{
		UPaperFlipbook* LoadedFlipbook = Slot->Flipbook.LoadSynchronous();
		if (!LoadedFlipbook)
		{
			return EPaper2DPlusDirectionalAnimationResult::LoadFailed;
		}
		if (!DirectionalAnimation_HasExpectedOwner(Profile, LoadedFlipbook, Owner))
		{
			return EPaper2DPlusDirectionalAnimationResult::AmbiguousFlipbook;
		}

		FPaper2DPlusOccupiedDirectionSlot& LoadedSlot =
			LoadedSlots.AddDefaulted_GetRef();
		LoadedSlot.SlotIndex = Slot->SlotIndex;
		LoadedSlot.Flipbook = LoadedFlipbook;
		LoadedSlot.bMirrorHorizontally = Slot->bMirrorHorizontally;
	}

	OutSlots = MoveTemp(LoadedSlots);
	return EPaper2DPlusDirectionalAnimationResult::Success;
}

EPaper2DPlusDirectionalAnimationResult
UPaper2DPlusDirectionalAnimationLibrary::GetDirectionSettings(
	UPaper2DPlusCharacterProfileAsset* Profile,
	UPaperFlipbook* Flipbook,
	int32& OutDirectionCount,
	float& OutAngleOffsetDegrees,
	bool& bOutHasDirectionalSet)
{
	OutDirectionCount = 0;
	OutAngleOffsetDegrees = 0.0f;
	bOutHasDirectionalSet = false;

	const FFlipbookProfileEntry* Owner = nullptr;
	int32 OwnerIndex = INDEX_NONE;
	const EPaper2DPlusDirectionalAnimationResult OwnerResult =
		DirectionalAnimation_ResolveOwner(Profile, Flipbook, Owner, OwnerIndex);
	if (OwnerResult != EPaper2DPlusDirectionalAnimationResult::Success)
	{
		return OwnerResult;
	}
	if (!Profile->GetEffectiveDirectionalSettings(
			OwnerIndex, OutDirectionCount, OutAngleOffsetDegrees))
	{
		OutDirectionCount = 0;
		OutAngleOffsetDegrees = 0.0f;
		return EPaper2DPlusDirectionalAnimationResult::InvalidProfileData;
	}
	bOutHasDirectionalSet = Profile->HasDirectionalSet(OwnerIndex);
	return EPaper2DPlusDirectionalAnimationResult::Success;
}

EPaper2DPlusDirectionalAnimationResult
UPaper2DPlusDirectionalAnimationLibrary::GetOccupiedDirectionSlotIndices(
	UPaper2DPlusCharacterProfileAsset* Profile,
	UPaperFlipbook* Flipbook,
	TArray<int32>& OutSlotIndices)
{
	OutSlotIndices.Reset();

	const FFlipbookProfileEntry* Owner = nullptr;
	int32 OwnerIndex = INDEX_NONE;
	const EPaper2DPlusDirectionalAnimationResult OwnerResult =
		DirectionalAnimation_ResolveOwner(Profile, Flipbook, Owner, OwnerIndex);
	if (OwnerResult != EPaper2DPlusDirectionalAnimationResult::Success)
	{
		return OwnerResult;
	}

	for (const FPaper2DPlusDirectionalAnimationSlot& Slot :
		Owner->DirectionalAnimationData.Slots)
	{
		// The shared structural gate already proved every occupied slot is active and uniquely keyed.
		if (!Slot.Flipbook.IsNull())
		{
			OutSlotIndices.Add(Slot.SlotIndex);
		}
	}
	OutSlotIndices.Sort();
	return EPaper2DPlusDirectionalAnimationResult::Success;
}

bool UPaper2DPlusDirectionalAnimationLibrary::ResolveDirectionSlotIndex(
	FVector2D Direction,
	int32 DirectionCount,
	float AngleOffsetDegrees,
	int32& OutSlotIndex)
{
	return UPaper2DPlusCharacterProfileAsset::ResolveDirectionalSlotIndex(
		Direction, DirectionCount, AngleOffsetDegrees, OutSlotIndex);
}

FVector2D UPaper2DPlusDirectionalAnimationLibrary::MakeDirectionFromBearing(
	float BearingDegrees)
{
	// Screen-space convention stated once: +Y up is zero degrees, clockwise positive.
	const float Radians = FMath::DegreesToRadians(BearingDegrees);
	return FVector2D(FMath::Sin(Radians), FMath::Cos(Radians));
}
