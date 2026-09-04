// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Paper2DPlusAppearanceLibrary.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusLayerRenderComponent.h"

/**
 * Runtime character customization discovery + swapping (UPaper2DPlusAppearanceLibrary).
 *
 * Worldless, following the net-lifecycle rig idiom: a component with no world resolves to the Standalone
 * net context, so every authority gate passes and the committed mutators run for real. These tests drive
 * the public library only — they never reach into the resolver — so they fail if the library stops
 * routing through the component's one commit chokepoint.
 *
 * Helpers are AppCustom_-prefixed and live in an ANONYMOUS namespace per the unity-build
 * file-unique-name rule.
 */

namespace
{
	/**
	 * One RuntimeCustomizable asset shaped like a real customizable character:
	 *   Body   — independent, always worn (a toggle, not a slot)
	 *   Hat    — Head Exclusive Group
	 *   Hood   — Head Exclusive Group (the peer Hat swaps with)
	 *   Badge  — points at an Exclusive Group the asset never declares (the orphan path)
	 * Plus a declared-but-empty "Cape" group, and two presets: Default {Body, Hat}, Armor {Body, Hood}.
	 */
	struct FAppCustom_Asset
	{
		UPaper2DPlusCharacterLayerAsset* Asset = nullptr;
		FGuid BodyLayerId;
		FGuid HatLayerId;
		FGuid HoodLayerId;
		FGuid BadgeLayerId;
		FGuid HeadGroupId;
		FGuid CapeGroupId;   // declared, no members
		FGuid OrphanGroupId; // referenced by Badge, never declared
		FGuid DefaultPresetId;
		FGuid ArmorPresetId;
	};

	FAppCustom_Asset AppCustom_MakeAsset()
	{
		FAppCustom_Asset Out;
		Out.Asset = NewObject<UPaper2DPlusCharacterLayerAsset>();
		Out.Asset->UsageMode = ECharacterLayerUsageMode::RuntimeCustomizable;
		Out.OrphanGroupId = FGuid::NewGuid();

		FCharacterLayerExclusiveGroup& Head = Out.Asset->ExclusiveGroups.AddDefaulted_GetRef();
		Head.DisplayName = TEXT("Head");
		Out.HeadGroupId = Head.GroupId;

		FCharacterLayerExclusiveGroup& Cape = Out.Asset->ExclusiveGroups.AddDefaulted_GetRef();
		Cape.DisplayName = TEXT("Cape");
		Out.CapeGroupId = Cape.GroupId;

		FCharacterLayer& Body = Out.Asset->Layers.AddDefaulted_GetRef();
		Body.LayerName = TEXT("Body");
		Out.BodyLayerId = Body.LayerId;

		FCharacterLayer& Hat = Out.Asset->Layers.AddDefaulted_GetRef();
		Hat.LayerName = TEXT("Hat");
		Hat.ExclusiveGroupId = Out.HeadGroupId;
		Out.HatLayerId = Hat.LayerId;

		FCharacterLayer& Hood = Out.Asset->Layers.AddDefaulted_GetRef();
		Hood.LayerName = TEXT("Hood");
		Hood.ExclusiveGroupId = Out.HeadGroupId;
		Out.HoodLayerId = Hood.LayerId;

		FCharacterLayer& Badge = Out.Asset->Layers.AddDefaulted_GetRef();
		Badge.LayerName = TEXT("Badge");
		Badge.ExclusiveGroupId = Out.OrphanGroupId;
		Out.BadgeLayerId = Badge.LayerId;

		FCharacterLayerAppearancePreset& Default = Out.Asset->AppearancePresets.AddDefaulted_GetRef();
		Default.DisplayName = TEXT("Default");
		Default.ActiveLayerIds = { Out.BodyLayerId, Out.HatLayerId };
		Out.DefaultPresetId = Default.PresetId;
		Out.Asset->DefaultAppearancePresetId = Default.PresetId;

		FCharacterLayerAppearancePreset& Armor = Out.Asset->AppearancePresets.AddDefaulted_GetRef();
		Armor.DisplayName = TEXT("Armor");
		Armor.ActiveLayerIds = { Out.BodyLayerId, Out.HoodLayerId };
		Out.ArmorPresetId = Armor.PresetId;

		return Out;
	}

	struct FAppCustom_Rig
	{
		FAppCustom_Asset Data;
		UPaper2DPlusLayerRenderComponent* Comp = nullptr;
	};

	/** A component already dressed in the asset's Default Appearance, exactly as BeginPlay would leave it. */
	FAppCustom_Rig AppCustom_MakeDressedRig()
	{
		FAppCustom_Rig Rig;
		Rig.Data = AppCustom_MakeAsset();
		Rig.Comp = NewObject<UPaper2DPlusLayerRenderComponent>();
		Rig.Comp->CharacterLayerAsset = Rig.Data.Asset;
		Rig.Comp->ResetToDefaultAppearance();
		return Rig;
	}

	const FPaper2DPlusAppearanceSlot* AppCustom_FindSlot(
		const TArray<FPaper2DPlusAppearanceSlot>& Slots,
		const FGuid& SlotId)
	{
		return Slots.FindByPredicate([&SlotId](const FPaper2DPlusAppearanceSlot& Slot)
		{
			return Slot.SlotId == SlotId;
		});
	}

	/** The Head slot's currently worn option name, or "<empty>" — the one value every swap assertion reads. */
	FString AppCustom_WornInHead(const FAppCustom_Rig& Rig)
	{
		const TArray<FPaper2DPlusAppearanceSlot> Slots =
			UPaper2DPlusAppearanceLibrary::DescribeAppearanceSlots(Rig.Comp);
		const FPaper2DPlusAppearanceSlot* Head = AppCustom_FindSlot(Slots, Rig.Data.HeadGroupId);
		if (!Head || Head->ActiveOptionIndex == INDEX_NONE)
		{
			return TEXT("<empty>");
		}
		return Head->Options[Head->ActiveOptionIndex].LayerName;
	}
} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// (a) DESCRIPTION: a designer can discover every Layer, every slot, and every preset from Blueprint
// without holding a single GUID up front.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAppearanceCustomizationDescribesAssetTest,
	"Paper2DPlus.Appearance.Customization.DescribesLayersSlotsAndPresets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAppearanceCustomizationDescribesAssetTest::RunTest(const FString& Parameters)
{
	const FAppCustom_Asset Data = AppCustom_MakeAsset();
	const TArray<FGuid> Worn = { Data.BodyLayerId, Data.HatLayerId };

	// --- Layers ---
	const TArray<FPaper2DPlusLayerOption> Options =
		UPaper2DPlusAppearanceLibrary::GetLayerOptions(Data.Asset, Worn);
	TestEqual(TEXT("every authored Layer is described"), Options.Num(), 4);
	TestEqual(TEXT("description follows global paint order"), Options[0].LayerName, FString(TEXT("Body")));
	TestEqual(TEXT("paint order is reported as an index"), Options[2].LayerIndex, 2);
	TestEqual(TEXT("the Layer's own identity is carried through"), Options[1].LayerId, Data.HatLayerId);

	TestTrue(TEXT("a worn Layer reads as active"), Options[1].bActive);
	TestFalse(TEXT("an unworn Layer reads as inactive"), Options[2].bActive);

	TestFalse(TEXT("an independent Layer has no slot"), Options[0].SlotId.IsValid());
	TestEqual(TEXT("an independent Layer has no slot label"), Options[0].SlotName, FString());
	TestEqual(TEXT("a grouped Layer reports its slot"), Options[1].SlotId, Data.HeadGroupId);
	TestEqual(TEXT("a grouped Layer reports its slot label"), Options[1].SlotName, FString(TEXT("Head")));
	TestEqual(TEXT("an undeclared group yields no label"), Options[3].SlotName, FString());

	// --- Slots ---
	const TArray<FPaper2DPlusAppearanceSlot> Slots =
		UPaper2DPlusAppearanceLibrary::GetAppearanceSlots(Data.Asset, Worn);
	TestEqual(TEXT("only groups with members are slots"), Slots.Num(), 2);
	TestNull(TEXT("a declared group with no member Layers is not a slot"),
		AppCustom_FindSlot(Slots, Data.CapeGroupId));

	const FPaper2DPlusAppearanceSlot* Head = AppCustom_FindSlot(Slots, Data.HeadGroupId);
	if (!Head)
	{
		AddError(TEXT("the Head slot is missing"));
		return false;
	}
	TestEqual(TEXT("authored slots come first"), Slots[0].SlotId, Data.HeadGroupId);
	TestEqual(TEXT("the slot carries its label"), Head->SlotName, FString(TEXT("Head")));
	TestEqual(TEXT("both peers are offered as options"), Head->Options.Num(), 2);
	TestEqual(TEXT("options follow global paint order"), Head->Options[0].LayerName, FString(TEXT("Hat")));
	TestEqual(TEXT("the worn option is identified"), Head->ActiveOptionIndex, 0);

	TestEqual(TEXT("an undeclared group is still reported as a slot"), Slots[1].SlotId, Data.OrphanGroupId);
	TestEqual(TEXT("an empty slot reports no worn option"), Slots[1].ActiveOptionIndex, INDEX_NONE);

	// --- Presets ---
	const TArray<FPaper2DPlusAppearancePresetOption> Presets =
		UPaper2DPlusAppearanceLibrary::GetAppearancePresetOptions(Data.Asset, Worn);
	TestEqual(TEXT("every authored preset is described"), Presets.Num(), 2);
	TestEqual(TEXT("the preset carries its label"), Presets[0].DisplayName, FString(TEXT("Default")));
	TestTrue(TEXT("the Default Appearance is flagged"), Presets[0].bIsDefault);
	TestFalse(TEXT("a non-default preset is not flagged as default"), Presets[1].bIsDefault);
	TestTrue(TEXT("the preset matching the worn selection is flagged current"), Presets[0].bIsCurrent);
	TestFalse(TEXT("a preset not matching the worn selection is not current"), Presets[1].bIsCurrent);

	// Presets are selection sets, not ordered lists: the same Layers in another order are the same look.
	const TArray<FPaper2DPlusAppearancePresetOption> Reordered =
		UPaper2DPlusAppearanceLibrary::GetAppearancePresetOptions(
			Data.Asset, { Data.HatLayerId, Data.BodyLayerId });
	TestTrue(TEXT("current-preset matching is independent of selection order"), Reordered[0].bIsCurrent);

	// --- Nothing crashes or invents data without an asset ---
	TestEqual(TEXT("a null asset describes no Layers"),
		UPaper2DPlusAppearanceLibrary::GetLayerOptions(nullptr, Worn).Num(), 0);
	TestEqual(TEXT("a null asset describes no slots"),
		UPaper2DPlusAppearanceLibrary::GetAppearanceSlots(nullptr, Worn).Num(), 0);
	TestEqual(TEXT("a null asset describes no presets"),
		UPaper2DPlusAppearanceLibrary::GetAppearancePresetOptions(nullptr, Worn).Num(), 0);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// (b) SWAPPING: one call per button press moves the worn option, and the Exclusive Group replacement the
// renderer depends on happens on every step — a swap must never leave both peers worn.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAppearanceCustomizationCyclesSlotTest,
	"Paper2DPlus.Appearance.Customization.CycleSlotSwapsExclusivePeers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAppearanceCustomizationCyclesSlotTest::RunTest(const FString& Parameters)
{
	FAppCustom_Rig Rig = AppCustom_MakeDressedRig();
	TestEqual(TEXT("the rig starts in the Default Appearance"), AppCustom_WornInHead(Rig), FString(TEXT("Hat")));

	// One button press forward.
	TestTrue(TEXT("cycling the Head slot succeeds"),
		UPaper2DPlusAppearanceLibrary::CycleAppearanceSlot(Rig.Comp, Rig.Data.HeadGroupId, 1));
	TestEqual(TEXT("the next option is worn"), AppCustom_WornInHead(Rig), FString(TEXT("Hood")));

	// The property the composite renderer depends on: the peer is GONE, not merely drawn under.
	TestFalse(TEXT("the former peer is no longer worn"), Rig.Comp->IsLayerActive(Rig.Data.HatLayerId));
	TestTrue(TEXT("the new option is worn"), Rig.Comp->IsLayerActive(Rig.Data.HoodLayerId));
	TestTrue(TEXT("an independent Layer is untouched by a slot swap"),
		Rig.Comp->IsLayerActive(Rig.Data.BodyLayerId));
	TestEqual(TEXT("the committed selection stays in global Layer order"),
		Rig.Comp->GetActiveLayerIds(), TArray<FGuid>({ Rig.Data.BodyLayerId, Rig.Data.HoodLayerId }));

	// Forward off the end wraps.
	TestTrue(TEXT("cycling past the last option succeeds"),
		UPaper2DPlusAppearanceLibrary::CycleAppearanceSlot(Rig.Comp, Rig.Data.HeadGroupId, 1));
	TestEqual(TEXT("cycling past the last option wraps to the first"),
		AppCustom_WornInHead(Rig), FString(TEXT("Hat")));

	// Backwards off the front wraps the other way.
	TestTrue(TEXT("cycling backwards succeeds"),
		UPaper2DPlusAppearanceLibrary::CycleAppearanceSlot(Rig.Comp, Rig.Data.HeadGroupId, -1));
	TestEqual(TEXT("cycling before the first option wraps to the last"),
		AppCustom_WornInHead(Rig), FString(TEXT("Hood")));

	// Addressing the same slot by its label is the same operation.
	TestTrue(TEXT("cycling by slot name succeeds"),
		UPaper2DPlusAppearanceLibrary::CycleAppearanceSlotByName(Rig.Comp, TEXT("head"), 1));
	TestEqual(TEXT("slot names resolve case-insensitively"), AppCustom_WornInHead(Rig), FString(TEXT("Hat")));

	// Requests that cannot be served change nothing and say so.
	TestFalse(TEXT("an unknown slot is refused"),
		UPaper2DPlusAppearanceLibrary::CycleAppearanceSlot(Rig.Comp, FGuid::NewGuid(), 1));
	TestFalse(TEXT("an unknown slot name is refused"),
		UPaper2DPlusAppearanceLibrary::CycleAppearanceSlotByName(Rig.Comp, TEXT("Boots"), 1));
	TestFalse(TEXT("a zero step is refused"),
		UPaper2DPlusAppearanceLibrary::CycleAppearanceSlot(Rig.Comp, Rig.Data.HeadGroupId, 0));
	TestFalse(TEXT("a null component is refused"),
		UPaper2DPlusAppearanceLibrary::CycleAppearanceSlot(nullptr, Rig.Data.HeadGroupId, 1));
	TestEqual(TEXT("refused requests leave the worn option alone"),
		AppCustom_WornInHead(Rig), FString(TEXT("Hat")));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// (c) THE EMPTY POSITION: a slot that offers "wear nothing" reaches it by cycling, and leaves it again.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAppearanceCustomizationEmptySlotTest,
	"Paper2DPlus.Appearance.Customization.CycleReachesAndLeavesTheEmptySlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAppearanceCustomizationEmptySlotTest::RunTest(const FString& Parameters)
{
	FAppCustom_Rig Rig = AppCustom_MakeDressedRig();

	// Hat -> Hood -> nothing -> Hat: two options plus one bare position.
	TestTrue(TEXT("step 1 succeeds"), UPaper2DPlusAppearanceLibrary::CycleAppearanceSlot(
		Rig.Comp, Rig.Data.HeadGroupId, 1, /*bAllowEmptySlot=*/true));
	TestEqual(TEXT("step 1 wears the peer"), AppCustom_WornInHead(Rig), FString(TEXT("Hood")));

	TestTrue(TEXT("step 2 succeeds"), UPaper2DPlusAppearanceLibrary::CycleAppearanceSlot(
		Rig.Comp, Rig.Data.HeadGroupId, 1, /*bAllowEmptySlot=*/true));
	TestEqual(TEXT("step 2 empties the slot"), AppCustom_WornInHead(Rig), FString(TEXT("<empty>")));
	TestFalse(TEXT("neither peer is worn"), Rig.Comp->IsLayerActive(Rig.Data.HoodLayerId));
	TestTrue(TEXT("an empty slot does not disturb independent Layers"),
		Rig.Comp->IsLayerActive(Rig.Data.BodyLayerId));

	TestTrue(TEXT("step 3 succeeds"), UPaper2DPlusAppearanceLibrary::CycleAppearanceSlot(
		Rig.Comp, Rig.Data.HeadGroupId, 1, /*bAllowEmptySlot=*/true));
	TestEqual(TEXT("step 3 leaves the empty position for the first option"),
		AppCustom_WornInHead(Rig), FString(TEXT("Hat")));

	// Without the bare position the cycle never empties the slot.
	TestTrue(TEXT("a two-step forward cycle succeeds"),
		UPaper2DPlusAppearanceLibrary::CycleAppearanceSlot(Rig.Comp, Rig.Data.HeadGroupId, 2));
	TestEqual(TEXT("a full lap without the empty position returns to the same option"),
		AppCustom_WornInHead(Rig), FString(TEXT("Hat")));

	// Direct selection: index, then clear.
	TestTrue(TEXT("selecting an option by index succeeds"),
		UPaper2DPlusAppearanceLibrary::SelectSlotOption(Rig.Comp, Rig.Data.HeadGroupId, 1));
	TestEqual(TEXT("the indexed option is worn"), AppCustom_WornInHead(Rig), FString(TEXT("Hood")));
	TestFalse(TEXT("an out-of-range option index is refused"),
		UPaper2DPlusAppearanceLibrary::SelectSlotOption(Rig.Comp, Rig.Data.HeadGroupId, 5));
	TestEqual(TEXT("a refused index leaves the worn option alone"),
		AppCustom_WornInHead(Rig), FString(TEXT("Hood")));
	TestTrue(TEXT("clearing a slot succeeds"),
		UPaper2DPlusAppearanceLibrary::SelectSlotOption(Rig.Comp, Rig.Data.HeadGroupId, -1));
	TestEqual(TEXT("a cleared slot wears nothing"), AppCustom_WornInHead(Rig), FString(TEXT("<empty>")));
	TestTrue(TEXT("clearing an already-empty slot is idempotent"),
		UPaper2DPlusAppearanceLibrary::SelectSlotOption(Rig.Comp, Rig.Data.HeadGroupId, -1));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// (d) TOGGLES AND PRESETS: the two swap shapes that need no Exclusive Group authoring at all.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAppearanceCustomizationTogglesAndPresetsTest,
	"Paper2DPlus.Appearance.Customization.TogglesLayersAndCyclesPresets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAppearanceCustomizationTogglesAndPresetsTest::RunTest(const FString& Parameters)
{
	FAppCustom_Rig Rig = AppCustom_MakeDressedRig();

	// --- Independent toggles ---
	TestTrue(TEXT("toggling a worn Layer off succeeds"),
		UPaper2DPlusAppearanceLibrary::ToggleLayer(Rig.Comp, Rig.Data.BodyLayerId));
	TestFalse(TEXT("the Layer is no longer worn"), Rig.Comp->IsLayerActive(Rig.Data.BodyLayerId));
	TestTrue(TEXT("toggling it back on succeeds"),
		UPaper2DPlusAppearanceLibrary::ToggleLayer(Rig.Comp, Rig.Data.BodyLayerId));
	TestTrue(TEXT("the Layer is worn again"), Rig.Comp->IsLayerActive(Rig.Data.BodyLayerId));

	TestTrue(TEXT("toggling by name succeeds"),
		UPaper2DPlusAppearanceLibrary::ToggleLayerByName(Rig.Comp, TEXT("body")));
	TestFalse(TEXT("Layer names resolve case-insensitively"),
		Rig.Comp->IsLayerActive(Rig.Data.BodyLayerId));
	TestFalse(TEXT("an unknown Layer name is refused"),
		UPaper2DPlusAppearanceLibrary::ToggleLayerByName(Rig.Comp, TEXT("Antlers")));
	TestFalse(TEXT("an unknown Layer ID is refused"),
		UPaper2DPlusAppearanceLibrary::ToggleLayer(Rig.Comp, FGuid::NewGuid()));

	// --- Whole-appearance presets ---
	FAppCustom_Rig PresetRig = AppCustom_MakeDressedRig();
	TestTrue(TEXT("cycling to the next preset succeeds"),
		UPaper2DPlusAppearanceLibrary::CycleAppearancePreset(PresetRig.Comp, 1));
	TestEqual(TEXT("the next preset's whole selection is worn"),
		PresetRig.Comp->GetActiveLayerIds(),
		TArray<FGuid>({ PresetRig.Data.BodyLayerId, PresetRig.Data.HoodLayerId }));

	TestTrue(TEXT("cycling past the last preset succeeds"),
		UPaper2DPlusAppearanceLibrary::CycleAppearancePreset(PresetRig.Comp, 1));
	TestEqual(TEXT("cycling past the last preset wraps to the first"),
		PresetRig.Comp->GetActiveLayerIds(),
		TArray<FGuid>({ PresetRig.Data.BodyLayerId, PresetRig.Data.HatLayerId }));

	// A hand-mixed look belongs to no preset; the next step must still land somewhere sensible.
	TestTrue(TEXT("emptying a slot succeeds"),
		UPaper2DPlusAppearanceLibrary::SelectSlotOption(PresetRig.Comp, PresetRig.Data.HeadGroupId, -1));
	TestFalse(TEXT("the mixed look matches no preset"),
		UPaper2DPlusAppearanceLibrary::DescribeAppearancePresetOptions(PresetRig.Comp)[0].bIsCurrent);
	TestTrue(TEXT("cycling from a mixed look succeeds"),
		UPaper2DPlusAppearanceLibrary::CycleAppearancePreset(PresetRig.Comp, 1));
	TestEqual(TEXT("a mixed look steps forward onto the first preset"),
		PresetRig.Comp->GetActiveLayerIds(),
		TArray<FGuid>({ PresetRig.Data.BodyLayerId, PresetRig.Data.HatLayerId }));

	TestFalse(TEXT("a null component cycles no preset"),
		UPaper2DPlusAppearanceLibrary::CycleAppearancePreset(nullptr, 1));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// (e) DELIVERY MODE: a Fixed/Baked asset renders compiled output, so it must refuse every swap while
// still describing itself for read-only display.
// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAppearanceCustomizationFixedBakedTest,
	"Paper2DPlus.Appearance.Customization.FixedBakedAssetsRefuseSwapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAppearanceCustomizationFixedBakedTest::RunTest(const FString& Parameters)
{
	FAppCustom_Rig Rig = AppCustom_MakeDressedRig();
	Rig.Comp->CharacterLayerAsset->UsageMode = ECharacterLayerUsageMode::FixedBaked;

	TestFalse(TEXT("a Fixed/Baked asset refuses a slot cycle"),
		UPaper2DPlusAppearanceLibrary::CycleAppearanceSlot(Rig.Comp, Rig.Data.HeadGroupId, 1));
	TestFalse(TEXT("a Fixed/Baked asset refuses a direct option selection"),
		UPaper2DPlusAppearanceLibrary::SelectSlotOption(Rig.Comp, Rig.Data.HeadGroupId, 1));
	TestFalse(TEXT("a Fixed/Baked asset refuses a Layer toggle"),
		UPaper2DPlusAppearanceLibrary::ToggleLayer(Rig.Comp, Rig.Data.BodyLayerId));
	TestFalse(TEXT("a Fixed/Baked asset refuses a preset cycle"),
		UPaper2DPlusAppearanceLibrary::CycleAppearancePreset(Rig.Comp, 1));

	TestEqual(TEXT("the refused swaps changed nothing"), AppCustom_WornInHead(Rig), FString(TEXT("Hat")));
	TestEqual(TEXT("a Fixed/Baked asset still describes its Layers"),
		UPaper2DPlusAppearanceLibrary::DescribeLayers(Rig.Comp).Num(), 4);
	TestEqual(TEXT("a Fixed/Baked asset still describes its slots"),
		UPaper2DPlusAppearanceLibrary::DescribeAppearanceSlots(Rig.Comp).Num(), 2);
	return true;
}

#endif // WITH_EDITOR
