// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Paper2DPlusDirectionalAnimationLibrary.generated.h"

class UPaper2DPlusCharacterProfileAsset;
class UPaperFlipbook;

/** Deterministic result from a cooked directional-animation query. */
UENUM(BlueprintType)
enum class EPaper2DPlusDirectionalAnimationResult : uint8
{
	Success UMETA(DisplayName = "Success"),
	InvalidRequest UMETA(DisplayName = "Invalid Request"),
	FlipbookNotInProfile UMETA(DisplayName = "Flipbook Not In Profile"),
	AmbiguousFlipbook UMETA(DisplayName = "Ambiguous Flipbook"),
	InvalidProfileData UMETA(DisplayName = "Invalid Profile Data"),
	InvalidDirection UMETA(DisplayName = "Invalid Direction"),
	DirectionUnoccupied UMETA(DisplayName = "Direction Unoccupied"),
	LoadFailed UMETA(DisplayName = "Load Failed")
};

/** One loaded occupied slot projected from a logical animation's active authored topology. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusOccupiedDirectionSlot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Directional Animation")
	int32 SlotIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Directional Animation")
	TObjectPtr<UPaperFlipbook> Flipbook = nullptr;

	/** Present this slot's art horizontally mirrored; applying the flip stays project-owned. */
	UPROPERTY(BlueprintReadOnly, Category = "Directional Animation")
	bool bMirrorHorizontally = false;
};

/** Focused cooked queries for Profile-owned directional animation art. */
UCLASS()
class PAPER2DPLUS_API UPaper2DPlusDirectionalAnimationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** True only when the base or active variant has a structurally valid owner with occupied slots. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Directional Animation",
		meta = (DisplayName = "Has Multi Direction"))
	static bool HasMultiDirection(
		UPaper2DPlusCharacterProfileAsset* Profile,
		UPaperFlipbook* Flipbook);

	/**
	 * Resolve the exact authored sector. A valid absent or configured-empty set returns the canonical
	 * base with OutSlotIndex = INDEX_NONE. A populated occupied sector returns that slot, its art,
	 * and its mirror presentation flag (author five facings, mirror three — the game applies the
	 * flip, typically via actor scale). Direction Unoccupied keeps OutSlotIndex reporting the
	 * resolved empty sector (art stays null) so a caller can implement its own fallback; every
	 * other failure clears the outputs. No nearest-direction or base fallback is selected.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Directional Animation",
		meta = (DisplayName = "Resolve Directional Flipbook",
			ToolTip = "Resolves one exact directional sector. Valid absent or configured-empty data returns the canonical base with Slot Index -1; a populated occupied sector returns its exact slot, art, and mirror flag (your game applies the horizontal flip). Direction Unoccupied reports the resolved empty sector in Slot Index with no art so the caller can implement its own fallback; every other failure clears the outputs."))
	static EPaper2DPlusDirectionalAnimationResult ResolveDirectionalFlipbook(
		UPaper2DPlusCharacterProfileAsset* Profile,
		UPaperFlipbook* Flipbook,
		FVector2D Direction,
		UPaperFlipbook*& OutFlipbook,
		int32& OutSlotIndex,
		bool& bOutMirrorHorizontally);

	/**
	 * Load every active occupied slot atomically and return records in ascending slot-index order.
	 * Valid absent or configured-empty data returns Success with an empty array. This broad enumeration
	 * is a synchronous load boundary intended for setup/inspection, not per-frame animation selection;
	 * it is deliberately impure so the load cost is visible as an execution step. It doubles as the
	 * warm/preload step before a directional actor becomes visible.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paper2DPlus|Directional Animation",
		meta = (DisplayName = "Get Occupied Direction Slots",
			ToolTip = "Synchronously loads every occupied directional slot; also the warm/preload step for a directional actor. Valid absent or configured-empty data returns Success with an empty array. Unsuitable for per-frame use."))
	static EPaper2DPlusDirectionalAnimationResult GetOccupiedDirectionSlots(
		UPaper2DPlusCharacterProfileAsset* Profile,
		UPaperFlipbook* Flipbook,
		TArray<FPaper2DPlusOccupiedDirectionSlot>& OutSlots);

	/**
	 * Read the animation's effective directional topology without loading anything. With Resolve
	 * Direction Slot Index this is enough for a Blueprint to implement any nearest/base/hold
	 * fallback policy of its own when an exact sector is empty.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Directional Animation",
		meta = (DisplayName = "Get Direction Settings",
			ToolTip = "Reads the effective direction count, angle offset, and set presence for the animation. Loads nothing."))
	static EPaper2DPlusDirectionalAnimationResult GetDirectionSettings(
		UPaper2DPlusCharacterProfileAsset* Profile,
		UPaperFlipbook* Flipbook,
		int32& OutDirectionCount,
		float& OutAngleOffsetDegrees,
		bool& bOutHasDirectionalSet);

	/** Occupied slot indices in ascending order, without loading any variant art. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Directional Animation",
		meta = (DisplayName = "Get Occupied Direction Slot Indices",
			ToolTip = "Returns the occupied slot indices in ascending order. Loads nothing, so it is safe wherever occupancy alone answers the question."))
	static EPaper2DPlusDirectionalAnimationResult GetOccupiedDirectionSlotIndices(
		UPaper2DPlusCharacterProfileAsset* Profile,
		UPaperFlipbook* Flipbook,
		TArray<int32>& OutSlotIndices);

	/**
	 * Pure angular selection: the same PaperZD-2.2.4-compatible math the resolver uses (+Y is
	 * zero, clockwise positive, offset before half-sector rounding). False for invalid settings
	 * or a zero/non-finite vector.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Directional Animation",
		meta = (DisplayName = "Resolve Direction Slot Index",
			ToolTip = "Converts a facing vector to its slot index for the given count and offset, with the resolver's exact angular math. Loads nothing."))
	static bool ResolveDirectionSlotIndex(
		FVector2D Direction,
		int32 DirectionCount,
		float AngleOffsetDegrees,
		int32& OutSlotIndex);

	/**
	 * Build a direction vector from a clockwise-from-up bearing in degrees — the one place the
	 * library's screen-space convention (+Y up at 0 degrees, clockwise positive) is stated as
	 * code, so a project maps its own world facing through a single node.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|Directional Animation",
		meta = (DisplayName = "Make Direction From Bearing"))
	static FVector2D MakeDirectionFromBearing(float BearingDegrees);
};
