// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Paper2DPlusAppearanceTypes.h"
#include "Paper2DPlusAppearanceLibrary.generated.h"

class UPaper2DPlusCharacterLayerAsset;
class UPaper2DPlusLayerRenderComponent;

/**
 * One selectable Layer, described for a customization UI.
 *
 * Selection identity is always LayerId; LayerName is a display label that may change without breaking
 * a saved appearance.
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusLayerOption
{
	GENERATED_BODY()

	/** Stable selection identity. Pass this to Toggle Layer or Set Layer Active. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2D+|Customization")
	FGuid LayerId;

	/** Designer-facing label only. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2D+|Customization")
	FString LayerName;

	/** Owning Exclusive Group. Invalid means this Layer stacks independently, so it toggles rather than swaps. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2D+|Customization")
	FGuid SlotId;

	/** Exclusive Group label. Empty for an independent Layer, or for a group with no authored name. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2D+|Customization")
	FString SlotName;

	/** True when this Layer belongs to the selection this description was built from. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2D+|Customization")
	bool bActive = false;

	/** Position in the asset's one global Layer order, which is also paint order. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2D+|Customization")
	int32 LayerIndex = INDEX_NONE;
};

/**
 * One Exclusive Group presented as a swappable slot: at most one option is worn at a time, and an
 * empty slot is always valid.
 */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusAppearanceSlot
{
	GENERATED_BODY()

	/** Exclusive Group identity. Pass this to Cycle Appearance Slot. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2D+|Customization")
	FGuid SlotId;

	/** Exclusive Group label. Empty when the group is referenced by Layers but carries no authored name. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2D+|Customization")
	FString SlotName;

	/** Member Layers in the asset's global Layer order. Never empty: slots with no members are not reported. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2D+|Customization")
	TArray<FPaper2DPlusLayerOption> Options;

	/** Index into Options, or -1 when nothing is currently worn in this slot. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2D+|Customization")
	int32 ActiveOptionIndex = INDEX_NONE;
};

/** One authored whole-appearance snapshot, described for a preset picker. */
USTRUCT(BlueprintType)
struct PAPER2DPLUS_API FPaper2DPlusAppearancePresetOption
{
	GENERATED_BODY()

	/** Stable preset identity. Pass this to Apply Appearance Preset. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2D+|Customization")
	FGuid PresetId;

	/** Designer-facing label only. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2D+|Customization")
	FString DisplayName;

	/** True for the asset's required Default Appearance. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2D+|Customization")
	bool bIsDefault = false;

	/** True when this preset's Layer selection equals the selection this description was built from. */
	UPROPERTY(BlueprintReadOnly, Category = "Paper2D+|Customization")
	bool bIsCurrent = false;
};

/**
 * Discovery and one-call swapping for runtime character customization.
 *
 * The Layer Render Component already owns the authoritative mutators (Set Layer Active, Apply Appearance
 * Preset), but they take stable GUIDs a designer has no Blueprint-visible way to obtain. This library is
 * the missing half: it enumerates what a Character Layer Asset offers — named Layers, Exclusive Groups
 * presented as slots, authored presets — and turns "next option in this slot" into a single node you can
 * bind straight to a button.
 *
 * Every mutation routes through the component's existing commit chokepoint, so authority gating,
 * normalization, Exclusive Group replacement, gameplay recomposition, and replication behave exactly as
 * they do for a direct Set Layer Active call. Nothing here is a parallel appearance path.
 *
 * Swapping requires a Runtime Customizable asset; Fixed/Baked assets reject every mutator and the
 * describe functions still report their Layers for read-only display.
 */
UCLASS()
class PAPER2DPLUS_API UPaper2DPlusAppearanceLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// --- Description: what does this character offer? ---

	/**
	 * Every Layer on the asset, in global paint order, flagged against the supplied selection.
	 *
	 * Asset-only, so a character creator can build its UI before any actor exists. Pass the selection you
	 * are previewing; pass an empty array to describe the asset with nothing worn.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2D+|Customization")
	static TArray<FPaper2DPlusLayerOption> GetLayerOptions(
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const TArray<FGuid>& ActiveLayerIds);

	/**
	 * Every Exclusive Group with at least one member Layer, as a swappable slot.
	 *
	 * Authored groups come first in authoring order, then any group referenced by a Layer but never
	 * declared on the asset. Layers with no Exclusive Group are absent: they are independent toggles,
	 * so read them from Get Layer Options instead.
	 */
	UFUNCTION(BlueprintPure, Category = "Paper2D+|Customization")
	static TArray<FPaper2DPlusAppearanceSlot> GetAppearanceSlots(
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const TArray<FGuid>& ActiveLayerIds);

	/** Every authored Appearance Preset, flagged for default and for equality with the supplied selection. */
	UFUNCTION(BlueprintPure, Category = "Paper2D+|Customization")
	static TArray<FPaper2DPlusAppearancePresetOption> GetAppearancePresetOptions(
		const UPaper2DPlusCharacterLayerAsset* LayerAsset,
		const TArray<FGuid>& ActiveLayerIds);

	/** Get Layer Options against a live component's committed selection. */
	UFUNCTION(BlueprintPure, Category = "Paper2D+|Customization")
	static TArray<FPaper2DPlusLayerOption> DescribeLayers(
		const UPaper2DPlusLayerRenderComponent* Component);

	/** Get Appearance Slots against a live component's committed selection. */
	UFUNCTION(BlueprintPure, Category = "Paper2D+|Customization")
	static TArray<FPaper2DPlusAppearanceSlot> DescribeAppearanceSlots(
		const UPaper2DPlusLayerRenderComponent* Component);

	/** Get Appearance Preset Options against a live component's committed selection. */
	UFUNCTION(BlueprintPure, Category = "Paper2D+|Customization")
	static TArray<FPaper2DPlusAppearancePresetOption> DescribeAppearancePresetOptions(
		const UPaper2DPlusLayerRenderComponent* Component);

	// --- Swapping: bind these to a button ---

	/**
	 * Step this slot's worn option by Delta, wrapping at both ends.
	 *
	 * With Allow Empty Slot the cycle also visits a bare position that wears nothing, which is how a
	 * "no hat" choice is offered. Without it every step lands on a real Layer.
	 *
	 * @return false when the component, its asset, or the slot cannot serve the request, or when the
	 *         asset is not Runtime Customizable. Delta 0 changes nothing and reports false.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Customization")
	static bool CycleAppearanceSlot(
		UPaper2DPlusLayerRenderComponent* Component,
		FGuid SlotId,
		int32 Delta = 1,
		bool bAllowEmptySlot = false);

	/** Cycle Appearance Slot addressed by the Exclusive Group's label instead of its GUID. Case-insensitive. */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Customization")
	static bool CycleAppearanceSlotByName(
		UPaper2DPlusLayerRenderComponent* Component,
		const FString& SlotName,
		int32 Delta = 1,
		bool bAllowEmptySlot = false);

	/**
	 * Wear one specific option of a slot.
	 *
	 * @param OptionIndex Index into the slot's Options, or any negative value to empty the slot.
	 * @return false when the slot is unknown or Option Index is past the end.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Customization")
	static bool SelectSlotOption(
		UPaper2DPlusLayerRenderComponent* Component,
		FGuid SlotId,
		int32 OptionIndex);

	/**
	 * Flip one Layer's activation.
	 *
	 * Activating a Layer that belongs to an Exclusive Group removes that group's former member, so this
	 * is also a valid way to swap a slot when you already hold the destination Layer's identity.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Customization")
	static bool ToggleLayer(
		UPaper2DPlusLayerRenderComponent* Component,
		FGuid LayerId);

	/** Toggle Layer addressed by the Layer's label instead of its GUID. Case-insensitive. */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Customization")
	static bool ToggleLayerByName(
		UPaper2DPlusLayerRenderComponent* Component,
		const FString& LayerName);

	/**
	 * Step the whole worn appearance to another authored Appearance Preset, wrapping at both ends.
	 *
	 * The cheapest one-button demonstration of runtime customization: it needs no Exclusive Groups, only
	 * two or more presets on the asset. A selection matching no preset counts as before the first one.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paper2D+|Customization")
	static bool CycleAppearancePreset(
		UPaper2DPlusLayerRenderComponent* Component,
		int32 Delta = 1);
};
