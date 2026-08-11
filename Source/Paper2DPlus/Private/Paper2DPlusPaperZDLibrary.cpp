// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusPaperZDLibrary.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterProfileComponent.h"
#include "PaperFlipbookComponent.h"
#include "GameFramework/Actor.h"

UObject* UPaper2DPlusPaperZDLibrary::FindPaperZDSequenceForFlipbook(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	UPaperFlipbook* Flipbook)
{
	if (!Asset || !Flipbook) return nullptr;
	return Asset->FindPaperZDSequenceForFlipbook(Flipbook);
}

UObject* UPaper2DPlusPaperZDLibrary::GetCachedPaperZDSequenceForFlipbook(
	const UPaper2DPlusCharacterProfileAsset* Asset,
	UPaperFlipbook* Flipbook)
{
	if (!Asset || !Flipbook) return nullptr;
	const FFlipbookProfileEntry* Data = Asset->FindByFlipbookPtr(Flipbook);
	if (!Data) return nullptr;
	return Data->Identity.PaperZDSequence.Get();
}

UObject* UPaper2DPlusPaperZDLibrary::GetActorCurrentPaperZDSequence(AActor* Actor)
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

	if (UObject* Cached = AnimData->Identity.PaperZDSequence.Get())
	{
		return Cached;
	}
	return ProfileComp->CharacterProfile->FindPaperZDSequenceForFlipbook(FlipbookComp->GetFlipbook());
}
