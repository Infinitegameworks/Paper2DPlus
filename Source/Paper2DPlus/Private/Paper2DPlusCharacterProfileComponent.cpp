// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusCharacterProfileComponent.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusModule.h"
#include "PaperFlipbookComponent.h"

UPaper2DPlusCharacterProfileComponent::UPaper2DPlusCharacterProfileComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPaper2DPlusCharacterProfileComponent::SetCharacterProfile(UPaper2DPlusCharacterProfileAsset* NewCharacterProfile)
{
	CharacterProfile = NewCharacterProfile;

	// Force the new asset to rebuild its flipbook lookup cache so
	// FindByFlipbookPtr resolves soft references immediately
	if (CharacterProfile)
	{
		CharacterProfile->InvalidateFlipbookLookupCache();
	}
}

void UPaper2DPlusCharacterProfileComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!FlipbookComponent)
	{
		AActor* Owner = GetOwner();
		if (Owner)
		{
			FlipbookComponent = Owner->FindComponentByClass<UPaperFlipbookComponent>();
			if (FlipbookComponent)
			{
				UE_LOG(LogPaper2DPlus, Verbose, TEXT("CharacterProfileComponent: Auto-found FlipbookComponent on %s"), *Owner->GetName());
			}
		}
	}
}

UPaperFlipbookComponent* UPaper2DPlusCharacterProfileComponent::GetResolvedFlipbookComponent() const
{
	if (FlipbookComponent)
	{
		return FlipbookComponent;
	}

	// Fallback: try to find at runtime if BeginPlay hasn't run yet or component was cleared
	AActor* Owner = GetOwner();
	if (Owner)
	{
		return Owner->FindComponentByClass<UPaperFlipbookComponent>();
	}
	return nullptr;
}
