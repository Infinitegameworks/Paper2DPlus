// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusCharacterDataComponent.h"
#include "Paper2DPlusModule.h"
#include "PaperFlipbookComponent.h"

UPaper2DPlusCharacterDataComponent::UPaper2DPlusCharacterDataComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPaper2DPlusCharacterDataComponent::BeginPlay()
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
				UE_LOG(LogPaper2DPlus, Verbose, TEXT("CharacterDataComponent: Auto-found FlipbookComponent on %s"), *Owner->GetName());
			}
		}
	}
}

UPaperFlipbookComponent* UPaper2DPlusCharacterDataComponent::GetResolvedFlipbookComponent() const
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
