// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusPaperZDLibrary.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusBlueprintLibrary.h"
#include "PaperFlipbookComponent.h"
#include "GameFramework/Actor.h"

/** UPaper2DPlusPaperZDLibrary — Typed Blueprint accessors for PaperZD sequences, avoiding UObject* casts in Blueprints. */

// All implementations below are thin wrappers that call the existing
// UObject*-returning getters and cast the result to UPaperZDAnimSequence*.
// Keeping the cast centralized here means the pattern appears in exactly
// one place and the reflection-based loaders on the asset remain the one
// source of truth for how the plugin discovers PaperZD sequences.

UPaperZDAnimSequence* UPaper2DPlusPaperZDLibrary::FindPaperZDSequenceForFlipbook(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	UPaperFlipbook* Flipbook)
{
	if (!Asset || !Flipbook) return nullptr;
	return Cast<UPaperZDAnimSequence>(Asset->FindPaperZDSequenceForFlipbook(Flipbook));
}

UPaperZDAnimSequence* UPaper2DPlusPaperZDLibrary::GetCachedPaperZDSequenceForFlipbook(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	const FString& FlipbookName)
{
	if (!Asset) return nullptr;
	const FFlipbookProfileEntry* Data = Asset->FindFlipbookDataPtr(FlipbookName);
	if (!Data) return nullptr;
	return Cast<UPaperZDAnimSequence>(Data->Identity.PaperZDSequence);
}

UPaperZDAnimSequence* UPaper2DPlusPaperZDLibrary::GetPaperZDSequenceForPhaseInGroup(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	const FString& GroupName,
	EAnimationPhase Phase)
{
	if (!Asset) return nullptr;
	return Cast<UPaperZDAnimSequence>(Asset->GetPaperZDSequenceForPhaseInGroup(GroupName, Phase));
}

UPaperZDAnimSequence* UPaper2DPlusPaperZDLibrary::GetPhaseGroupCustomPaperZDSequence(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	const FString& GroupName,
	const FString& CustomSlotName)
{
	return Cast<UPaperZDAnimSequence>(
		UPaper2DPlusBlueprintLibrary::GetPhaseGroupCustomSequence(Asset, GroupName, CustomSlotName));
}

UPaperZDAnimSequence* UPaper2DPlusPaperZDLibrary::GetPaperZDSequenceForTag(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	FGameplayTag Group,
	int32 ComboIndex)
{
	if (!Asset) return nullptr;
	return Cast<UPaperZDAnimSequence>(Asset->GetPaperZDSequenceForTag(Group, ComboIndex));
}

UPaperZDAnimSequence* UPaper2DPlusPaperZDLibrary::GetActorCurrentPaperZDSequence(AActor* Actor)
{
	if (!Actor) return nullptr;

	UPaper2DPlusCharacterProfileComponent* ProfileComp =
		Actor->FindComponentByClass<UPaper2DPlusCharacterProfileComponent>();
	if (!ProfileComp || !ProfileComp->CharacterProfile) return nullptr;

	UPaperFlipbookComponent* FlipbookComp = Actor->FindComponentByClass<UPaperFlipbookComponent>();
	if (!FlipbookComp || !FlipbookComp->GetFlipbook()) return nullptr;

	const FFlipbookProfileEntry* AnimData =
		ProfileComp->CharacterProfile->FindByFlipbookPtr(FlipbookComp->GetFlipbook());
	if (!AnimData) return nullptr;

	// Prefer the cached sequence on the profile entry (auto-populated). Fall
	// back to a registry search if the cache is empty so BP callers don't
	// have to manually call AutoPopulatePaperZDSequences first.
	if (UPaperZDAnimSequence* Cached = Cast<UPaperZDAnimSequence>(AnimData->Identity.PaperZDSequence))
	{
		return Cached;
	}
	return Cast<UPaperZDAnimSequence>(
		ProfileComp->CharacterProfile->FindPaperZDSequenceForFlipbook(FlipbookComp->GetFlipbook()));
}
