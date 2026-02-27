// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Paper2DPlusCharacterDataComponent.generated.h"

class UPaper2DPlusCharacterDataAsset;
class UPaperFlipbookComponent;

/**
 * Component that provides Paper2DPlus character data context for an actor.
 * Add to any actor with a PaperFlipbookComponent so that actor-based hitbox
 * functions (CheckAttackCollision, QuickHitCheck, etc.) can auto-resolve context.
 *
 * Set CharacterData to your character's data asset. The FlipbookComponent is
 * auto-found at BeginPlay if not explicitly assigned.
 */
UCLASS(ClassGroup=(Paper2DPlus), meta=(BlueprintSpawnableComponent, DisplayName="Paper2DPlus Character Data"))
class PAPER2DPLUS_API UPaper2DPlusCharacterDataComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPaper2DPlusCharacterDataComponent();

	/** The character data asset for this actor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus")
	TObjectPtr<UPaper2DPlusCharacterDataAsset> CharacterData;

	/** The flipbook component used for frame resolution. Auto-found if not set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus")
	TObjectPtr<UPaperFlipbookComponent> FlipbookComponent;

	/** Get the resolved flipbook component (auto-finds if needed). */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus")
	UPaperFlipbookComponent* GetResolvedFlipbookComponent() const;

protected:
	virtual void BeginPlay() override;
};
