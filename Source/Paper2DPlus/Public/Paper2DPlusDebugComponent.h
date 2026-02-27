// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Paper2DPlusCharacterDataAsset.h"
#include "Paper2DPlusDebugComponent.generated.h"

class UPaperFlipbookComponent;

/**
 * Component that automatically draws debug hitboxes for an actor.
 * Add to any actor with a PaperFlipbookComponent to visualize hitboxes in-game.
 */
UCLASS(ClassGroup=(Paper2DPlus), meta=(BlueprintSpawnableComponent, DisplayName="Paper2DPlus Debug Draw"))
class PAPER2DPLUS_API UPaper2DPlusDebugComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPaper2DPlusDebugComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ==========================================
	// CONFIGURATION
	// ==========================================

	/** The character data asset to visualize */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus Debug")
	UPaper2DPlusCharacterDataAsset* CharacterData;

	/** Reference to the flipbook component (auto-found if not set) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus Debug")
	UPaperFlipbookComponent* FlipbookComponent;

	/** Enable/disable debug drawing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus Debug")
	bool bEnableDebugDraw = true;

	/** Whether the character is facing left (flips hitboxes) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus Debug")
	bool bFlipX = false;

	/** Scale multiplier for hitboxes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus Debug")
	float Scale = 1.0f;

	// ==========================================
	// DRAW OPTIONS
	// ==========================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus Debug|Options")
	bool bDrawAttackHitboxes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus Debug|Options")
	bool bDrawHurtboxes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus Debug|Options")
	bool bDrawCollisionBoxes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus Debug|Options")
	bool bDrawSockets = true;

	// ==========================================
	// COLORS
	// ==========================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus Debug|Colors")
	FColor AttackColor = FColor::Red;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus Debug|Colors")
	FColor HurtboxColor = FColor::Green;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus Debug|Colors")
	FColor CollisionColor = FColor::Blue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus Debug|Colors")
	FColor SocketColor = FColor::Yellow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paper2DPlus Debug|Colors", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float LineThickness = 2.0f;

	// ==========================================
	// BLUEPRINT FUNCTIONS
	// ==========================================

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus Debug")
	void SetDebugDrawEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus Debug")
	void SetFlipX(bool bNewFlipX);

	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus Debug")
	void DrawHitboxesNow(float Duration = 0.0f);

protected:
	virtual void BeginPlay() override;

private:
	void AutoFindFlipbookComponent();
	void DrawHitbox(const FHitboxData& Hitbox, const FVector& WorldPosition, float Duration);
	void DrawSocket(const FSocketData& Socket, const FVector& WorldPosition, float Duration);
	FColor GetColorForType(EHitboxType Type) const;
	bool ResolveFrameData(UPaperFlipbook* Flipbook, FFrameHitboxData& OutFrameData) const;

	/** True when owner has a UPaper2DPlusCharacterDataComponent — FlipX/Scale auto-derived from actor scale */
	bool bOwnerHasDataComponent = false;
};
