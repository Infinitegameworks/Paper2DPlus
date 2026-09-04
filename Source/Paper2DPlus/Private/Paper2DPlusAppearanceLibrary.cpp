// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Paper2DPlusAppearanceLibrary.h"

#include "Paper2DPlusAppearanceResolver.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusLayerRenderComponent.h"

namespace
{
	/** The asset a live component customizes, or null when the component or its assignment is missing. */
	const UPaper2DPlusCharacterLayerAsset* GetComponentAsset(const UPaper2DPlusLayerRenderComponent* Component)
	{
		return Component ? Component->CharacterLayerAsset : nullptr;
	}

	/**
	 * Wrap Index into [0, Count). Written for a possibly negative Index because a backwards cycle is an
	 * ordinary request, and C++ remainder keeps the dividend's sign.
	 */
	int32 WrapIndex(int32 Index, int32 Count)
	{
		check(Count > 0);
		return ((Index % Count) + Count) % Count;
	}

	/**
	 * Step one slot and commit through the component's own mutator, so Exclusive Group replacement,
	 * authority gating, and replication stay on the single existing path.
	 */
	bool StepSlot(
		UPaper2DPlusLayerRenderComponent* Component,
		const FPaper2DPlusAppearanceSlot& Slot,
		int32 Delta,
		bool bAllowEmptySlot)
	{
		const int32 OptionCount = Slot.Options.Num();
		if (!Component || OptionCount == 0 || Delta == 0)
		{
			return false;
		}

		const int32 Current = Slot.ActiveOptionIndex;
		int32 Next = INDEX_NONE;

		if (bAllowEmptySlot)
		{
			// One virtual position past the last option wears nothing, so "no hat" is reachable by cycling.
			const int32 Positions = OptionCount + 1;
			const int32 EmptyPosition = OptionCount;
			const int32 CurrentPosition = (Current == INDEX_NONE) ? EmptyPosition : Current;
			Next = WrapIndex(CurrentPosition + Delta, Positions);
			if (Next == EmptyPosition)
			{
				return Current != INDEX_NONE
					&& Component->SetLayerActive(Slot.Options[Current].LayerId, false);
			}
		}
		else
		{
			// An empty slot has no index to step from, so enter the ring at whichever end the direction implies.
			const int32 Start = (Current != INDEX_NONE) ? Current : (Delta > 0 ? -1 : 0);
			Next = WrapIndex(Start + Delta, OptionCount);
		}

		// A single-option slot can step onto itself. The selection is already correct, so report success
		// rather than committing a descriptor the component would recognize as a no-op anyway.
		return Next == Current
			|| Component->SetLayerActive(Slot.Options[Next].LayerId, true);
	}
}

TArray<FPaper2DPlusLayerOption> UPaper2DPlusAppearanceLibrary::GetLayerOptions(
	const UPaper2DPlusCharacterLayerAsset* LayerAsset,
	const TArray<FGuid>& ActiveLayerIds)
{
	TArray<FPaper2DPlusLayerOption> Options;
	if (!LayerAsset)
	{
		return Options;
	}

	Options.Reserve(LayerAsset->Layers.Num());
	for (int32 LayerIndex = 0; LayerIndex < LayerAsset->Layers.Num(); ++LayerIndex)
	{
		const FCharacterLayer& Layer = LayerAsset->Layers[LayerIndex];

		FPaper2DPlusLayerOption& Option = Options.AddDefaulted_GetRef();
		Option.LayerId = Layer.LayerId;
		Option.LayerName = Layer.LayerName;
		Option.SlotId = Layer.ExclusiveGroupId;
		Option.bActive = ActiveLayerIds.Contains(Layer.LayerId);
		Option.LayerIndex = LayerIndex;

		// Null for an independent Layer and for a group a Layer references without the asset declaring it;
		// both leave the label empty rather than inventing one.
		if (const FCharacterLayerExclusiveGroup* Group = LayerAsset->GetExclusiveGroupById(Layer.ExclusiveGroupId))
		{
			Option.SlotName = Group->DisplayName;
		}
	}
	return Options;
}

TArray<FPaper2DPlusAppearanceSlot> UPaper2DPlusAppearanceLibrary::GetAppearanceSlots(
	const UPaper2DPlusCharacterLayerAsset* LayerAsset,
	const TArray<FGuid>& ActiveLayerIds)
{
	TArray<FPaper2DPlusAppearanceSlot> Slots;
	if (!LayerAsset)
	{
		return Slots;
	}

	const TArray<FPaper2DPlusLayerOption> Options = GetLayerOptions(LayerAsset, ActiveLayerIds);

	// Authored groups first, in authoring order, so slot order is a designer decision rather than an
	// accident of paint order.
	TMap<FGuid, int32> SlotIndexByGroupId;
	SlotIndexByGroupId.Reserve(LayerAsset->ExclusiveGroups.Num());
	for (const FCharacterLayerExclusiveGroup& Group : LayerAsset->ExclusiveGroups)
	{
		if (!Group.GroupId.IsValid() || SlotIndexByGroupId.Contains(Group.GroupId))
		{
			continue;
		}
		FPaper2DPlusAppearanceSlot& Slot = Slots.AddDefaulted_GetRef();
		Slot.SlotId = Group.GroupId;
		Slot.SlotName = Group.DisplayName;
		SlotIndexByGroupId.Add(Group.GroupId, Slots.Num() - 1);
	}

	for (const FPaper2DPlusLayerOption& Option : Options)
	{
		if (!Option.SlotId.IsValid())
		{
			continue;
		}

		int32* ExistingSlotIndex = SlotIndexByGroupId.Find(Option.SlotId);
		if (!ExistingSlotIndex)
		{
			// A Layer can point at a group the asset never declared. That is an authoring fault the asset
			// validator already reports, and normalization refuses to activate such a Layer — so the slot
			// is reported for visibility, but every attempt to wear it will fail rather than silently
			// dropping the exclusivity.
			FPaper2DPlusAppearanceSlot& Orphan = Slots.AddDefaulted_GetRef();
			Orphan.SlotId = Option.SlotId;
			ExistingSlotIndex = &SlotIndexByGroupId.Add(Option.SlotId, Slots.Num() - 1);
		}

		FPaper2DPlusAppearanceSlot& Slot = Slots[*ExistingSlotIndex];
		if (Option.bActive)
		{
			Slot.ActiveOptionIndex = Slot.Options.Num();
		}
		Slot.Options.Add(Option);
	}

	// A declared group with no member Layers is not a slot a player can operate.
	Slots.RemoveAll([](const FPaper2DPlusAppearanceSlot& Slot)
	{
		return Slot.Options.Num() == 0;
	});
	return Slots;
}

TArray<FPaper2DPlusAppearancePresetOption> UPaper2DPlusAppearanceLibrary::GetAppearancePresetOptions(
	const UPaper2DPlusCharacterLayerAsset* LayerAsset,
	const TArray<FGuid>& ActiveLayerIds)
{
	TArray<FPaper2DPlusAppearancePresetOption> Presets;
	if (!LayerAsset)
	{
		return Presets;
	}

	// Compare in the asset's global order so a preset authored in a different order still matches the
	// selection it produces.
	TArray<FGuid> NormalizedSelection;
	const bool bSelectionComparable = Paper2DPlusAppearanceResolver::NormalizeLayerSelection(
		LayerAsset, ActiveLayerIds, NormalizedSelection, nullptr);

	Presets.Reserve(LayerAsset->AppearancePresets.Num());
	for (const FCharacterLayerAppearancePreset& Preset : LayerAsset->AppearancePresets)
	{
		FPaper2DPlusAppearancePresetOption& Option = Presets.AddDefaulted_GetRef();
		Option.PresetId = Preset.PresetId;
		Option.DisplayName = Preset.DisplayName;
		Option.bIsDefault = Preset.PresetId.IsValid()
			&& Preset.PresetId == LayerAsset->DefaultAppearancePresetId;

		TArray<FGuid> NormalizedPreset;
		Option.bIsCurrent = bSelectionComparable
			&& Paper2DPlusAppearanceResolver::NormalizeLayerSelection(
				LayerAsset, Preset.ActiveLayerIds, NormalizedPreset, nullptr)
			&& NormalizedPreset == NormalizedSelection;
	}
	return Presets;
}

TArray<FPaper2DPlusLayerOption> UPaper2DPlusAppearanceLibrary::DescribeLayers(
	const UPaper2DPlusLayerRenderComponent* Component)
{
	return Component
		? GetLayerOptions(GetComponentAsset(Component), Component->GetActiveLayerIds())
		: TArray<FPaper2DPlusLayerOption>();
}

TArray<FPaper2DPlusAppearanceSlot> UPaper2DPlusAppearanceLibrary::DescribeAppearanceSlots(
	const UPaper2DPlusLayerRenderComponent* Component)
{
	return Component
		? GetAppearanceSlots(GetComponentAsset(Component), Component->GetActiveLayerIds())
		: TArray<FPaper2DPlusAppearanceSlot>();
}

TArray<FPaper2DPlusAppearancePresetOption> UPaper2DPlusAppearanceLibrary::DescribeAppearancePresetOptions(
	const UPaper2DPlusLayerRenderComponent* Component)
{
	return Component
		? GetAppearancePresetOptions(GetComponentAsset(Component), Component->GetActiveLayerIds())
		: TArray<FPaper2DPlusAppearancePresetOption>();
}

bool UPaper2DPlusAppearanceLibrary::CycleAppearanceSlot(
	UPaper2DPlusLayerRenderComponent* Component,
	FGuid SlotId,
	int32 Delta,
	bool bAllowEmptySlot)
{
	if (!SlotId.IsValid())
	{
		return false;
	}

	const TArray<FPaper2DPlusAppearanceSlot> Slots = DescribeAppearanceSlots(Component);
	const FPaper2DPlusAppearanceSlot* Slot = Slots.FindByPredicate(
		[&SlotId](const FPaper2DPlusAppearanceSlot& Candidate)
		{
			return Candidate.SlotId == SlotId;
		});
	return Slot && StepSlot(Component, *Slot, Delta, bAllowEmptySlot);
}

bool UPaper2DPlusAppearanceLibrary::CycleAppearanceSlotByName(
	UPaper2DPlusLayerRenderComponent* Component,
	const FString& SlotName,
	int32 Delta,
	bool bAllowEmptySlot)
{
	if (SlotName.IsEmpty())
	{
		return false;
	}

	const TArray<FPaper2DPlusAppearanceSlot> Slots = DescribeAppearanceSlots(Component);
	const FPaper2DPlusAppearanceSlot* Slot = Slots.FindByPredicate(
		[&SlotName](const FPaper2DPlusAppearanceSlot& Candidate)
		{
			return Candidate.SlotName.Equals(SlotName, ESearchCase::IgnoreCase);
		});
	return Slot && StepSlot(Component, *Slot, Delta, bAllowEmptySlot);
}

bool UPaper2DPlusAppearanceLibrary::SelectSlotOption(
	UPaper2DPlusLayerRenderComponent* Component,
	FGuid SlotId,
	int32 OptionIndex)
{
	if (!Component || !SlotId.IsValid())
	{
		return false;
	}

	const TArray<FPaper2DPlusAppearanceSlot> Slots = DescribeAppearanceSlots(Component);
	const FPaper2DPlusAppearanceSlot* Slot = Slots.FindByPredicate(
		[&SlotId](const FPaper2DPlusAppearanceSlot& Candidate)
		{
			return Candidate.SlotId == SlotId;
		});
	if (!Slot)
	{
		return false;
	}

	if (OptionIndex < 0)
	{
		// Emptying an already-empty slot is the state the caller asked for, so it succeeds without a commit.
		return Slot->ActiveOptionIndex == INDEX_NONE
			|| Component->SetLayerActive(Slot->Options[Slot->ActiveOptionIndex].LayerId, false);
	}
	return Slot->Options.IsValidIndex(OptionIndex)
		&& Component->SetLayerActive(Slot->Options[OptionIndex].LayerId, true);
}

bool UPaper2DPlusAppearanceLibrary::ToggleLayer(
	UPaper2DPlusLayerRenderComponent* Component,
	FGuid LayerId)
{
	return Component && Component->SetLayerActive(LayerId, !Component->IsLayerActive(LayerId));
}

bool UPaper2DPlusAppearanceLibrary::ToggleLayerByName(
	UPaper2DPlusLayerRenderComponent* Component,
	const FString& LayerName)
{
	const UPaper2DPlusCharacterLayerAsset* LayerAsset = GetComponentAsset(Component);
	const FCharacterLayer* Layer = LayerAsset ? LayerAsset->GetLayerByName(LayerName) : nullptr;
	return Layer && ToggleLayer(Component, Layer->LayerId);
}

bool UPaper2DPlusAppearanceLibrary::CycleAppearancePreset(
	UPaper2DPlusLayerRenderComponent* Component,
	int32 Delta)
{
	if (!Component)
	{
		return false;
	}

	const TArray<FPaper2DPlusAppearancePresetOption> Presets = DescribeAppearancePresetOptions(Component);
	if (Presets.Num() == 0)
	{
		return false;
	}

	const int32 Current = Presets.IndexOfByPredicate(
		[](const FPaper2DPlusAppearancePresetOption& Preset)
		{
			return Preset.bIsCurrent;
		});
	// A selection matching no preset sits before the first one, so a forward step lands on index 0.
	const int32 Start = (Current != INDEX_NONE) ? Current : (Delta > 0 ? -1 : 0);
	const int32 Next = WrapIndex(Start + Delta, Presets.Num());
	return Component->ApplyAppearancePreset(Presets[Next].PresetId);
}
