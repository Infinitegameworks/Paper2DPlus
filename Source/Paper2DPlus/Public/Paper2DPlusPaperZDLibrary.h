// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Paper2DPlusTypes.h"
#include "GameplayTagContainer.h"
#include "AnimSequences/PaperZDAnimSequence.h"
#include "Paper2DPlusPaperZDLibrary.generated.h"

class UPaper2DPlusCharacterProfileAsset;
class UPaperFlipbook;
class AActor;

/**
 * Blueprint library with TYPED PaperZD accessors.
 *
 * Exists because the generic getters on UPaper2DPlusCharacterProfileAsset and
 * UPaper2DPlusBlueprintLibrary return UObject* — those paths use reflection
 * to touch PaperZD internals and predate the hard PaperZD dependency this
 * plugin now has. UObject* output is painful in Blueprints (every call
 * requires a Cast-to-UPaperZDAnimSequence node before any PaperZD API is
 * usable), so this library wraps every PaperZD-returning getter with a
 * typed variant that outputs UPaperZDAnimSequence* directly. Internally
 * each function just calls the generic getter and casts once.
 *
 * All functions are safe with null asset / null actor inputs; they return
 * nullptr without warnings.
 *
 * If you ever fork Paper2DPlus for a distribution target that doesn't have
 * PaperZD available, delete this header + cpp and drop the PaperZD entry
 * from Paper2DPlus.Build.cs — the rest of the plugin keeps working via the
 * reflection-based UObject* paths.
 */
UCLASS()
class PAPER2DPLUS_API UPaper2DPlusPaperZDLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ─── PaperZD sequence per flipbook (asset lookup) ──────────────────────

	/** Typed version of UPaper2DPlusCharacterProfileAsset::FindPaperZDSequenceForFlipbook.
	 *  Searches the asset registry for the PaperZDAnimSequence that references
	 *  the given flipbook under the character profile's AnimSource. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|PaperZD")
	static UPaperZDAnimSequence* FindPaperZDSequenceForFlipbook(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		UPaperFlipbook* Flipbook);

	/** Returns the cached PaperZDSequence stored on a flipbook profile entry
	 *  (auto-populated by AutoPopulatePaperZDSequences or set manually in the
	 *  character profile editor). No asset registry query — cheap. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|PaperZD")
	static UPaperZDAnimSequence* GetCachedPaperZDSequenceForFlipbook(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& FlipbookName);

	// ─── PaperZD sequence per phase in a phase group ───────────────────────

	/** Typed version of GetPaperZDSequenceForPhaseInGroup.
	 *  Built-in slots only (Startup/Active/Recovery). For custom slots use
	 *  GetPhaseGroupCustomPaperZDSequence. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|PaperZD")
	static UPaperZDAnimSequence* GetPaperZDSequenceForPhaseInGroup(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& GroupName,
		EAnimationPhase Phase);

	/** Typed version of UPaper2DPlusBlueprintLibrary::GetPhaseGroupCustomSequence.
	 *  Custom phase slots only — returns nullptr for Startup/Active/Recovery. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|PaperZD")
	static UPaperZDAnimSequence* GetPhaseGroupCustomPaperZDSequence(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		const FString& GroupName,
		const FString& CustomSlotName);

	// ─── PaperZD sequence per tag mapping ──────────────────────────────────

	/** Typed version of GetPaperZDSequenceForTag.
	 *  Returns the PaperZD sequence for the flipbook at ComboIndex in the
	 *  tag mapping for Group. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|PaperZD")
	static UPaperZDAnimSequence* GetPaperZDSequenceForTag(
		const UPaper2DPlusCharacterProfileAsset* Asset,
		FGameplayTag Group,
		int32 ComboIndex = 0);

	// ─── Actor-current convenience getters ─────────────────────────────────

	/** Returns the PaperZD sequence for the actor's currently-playing flipbook,
	 *  using the cached Identity.PaperZDSequence on the flipbook's profile
	 *  entry. Requires the actor to have a Paper2DPlusCharacterProfileComponent
	 *  with a CharacterProfile assigned. Falls back to an asset registry
	 *  lookup if the cache is empty. */
	UFUNCTION(BlueprintPure, Category = "Paper2DPlus|PaperZD")
	static UPaperZDAnimSequence* GetActorCurrentPaperZDSequence(AActor* Actor);
};
