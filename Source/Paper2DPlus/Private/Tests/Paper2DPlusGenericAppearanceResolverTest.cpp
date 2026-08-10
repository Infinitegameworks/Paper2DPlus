// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Paper2DPlusAppearanceResolver.h"
#include "Paper2DPlusCharacterLayerAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FGuid Guid(const TCHAR* Value)
	{
		FGuid Result;
		FGuid::Parse(Value, Result);
		return Result;
	}

	FCharacterLayer GenericLayer(const TCHAR* Name, const FGuid& LayerId, const FGuid& GroupId = FGuid())
	{
		FCharacterLayer Layer;
		Layer.LayerName = Name;
		Layer.LayerId = LayerId;
		Layer.ExclusiveGroupId = GroupId;
		return Layer;
	}

	UPaper2DPlusCharacterLayerAsset* MakeAsset()
	{
		const FGuid Body = Guid(TEXT("11111111-1111-1111-1111-111111111111"));
		const FGuid Hat = Guid(TEXT("22222222-2222-2222-2222-222222222222"));
		const FGuid Hood = Guid(TEXT("33333333-3333-3333-3333-333333333333"));
		const FGuid Headwear = Guid(TEXT("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"));

		UPaper2DPlusCharacterLayerAsset* Asset = NewObject<UPaper2DPlusCharacterLayerAsset>();
		Asset->UsageMode = ECharacterLayerUsageMode::RuntimeCustomizable;
		Asset->Layers = {
			GenericLayer(TEXT("Body"), Body),
			GenericLayer(TEXT("Hat"), Hat, Headwear),
			GenericLayer(TEXT("Hood"), Hood, Headwear)
		};
		FCharacterLayerExclusiveGroup Group;
		Group.GroupId = Headwear;
		Group.DisplayName = TEXT("Headwear");
		Asset->ExclusiveGroups.Add(Group);

		FCharacterLayerAppearancePreset Default;
		Default.PresetId = Guid(TEXT("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"));
		Default.DisplayName = TEXT("Default");
		Default.ActiveLayerIds = { Body, Hat };
		Asset->AppearancePresets.Add(Default);
		Asset->DefaultAppearancePresetId = Default.PresetId;
		return Asset;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusGenericAppearanceNormalizationTest,
	"Paper2DPlus.Appearance.Generic.NormalizeAndExclusiveGroups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusGenericAppearanceNormalizationTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterLayerAsset* Asset = MakeAsset();
	const FGuid Body = Asset->Layers[0].LayerId;
	const FGuid Hat = Asset->Layers[1].LayerId;
	const FGuid Hood = Asset->Layers[2].LayerId;

	TArray<FGuid> Normalized;
	FString Reason;
	TestTrue(TEXT("valid selection normalizes"),
		Paper2DPlusAppearanceResolver::NormalizeLayerSelection(Asset, { Hat, Body }, Normalized, &Reason));
	TestEqual(TEXT("normalization uses global asset order"), Normalized, TArray<FGuid>({ Body, Hat }));

	TestFalse(TEXT("multiple members of one Exclusive Group are rejected"),
		Paper2DPlusAppearanceResolver::NormalizeLayerSelection(Asset, { Hat, Hood }, Normalized, &Reason));
	TestTrue(TEXT("group conflict reason is explicit"), Reason.Contains(TEXT("Exclusive Group")));

	TestFalse(TEXT("duplicate Layer IDs are rejected"),
		Paper2DPlusAppearanceResolver::NormalizeLayerSelection(Asset, { Body, Body }, Normalized, &Reason));
	TestFalse(TEXT("unknown Layer IDs are rejected"),
		Paper2DPlusAppearanceResolver::NormalizeLayerSelection(
			Asset, { Guid(TEXT("dddddddd-dddd-dddd-dddd-dddddddddddd")) }, Normalized, &Reason));

	TestTrue(TEXT("activating a peer replaces the previous member"),
		Paper2DPlusAppearanceResolver::ApplyLayerActivation(Asset, { Body, Hat }, Hood, true, Normalized, &Reason));
	TestEqual(TEXT("replacement remains in global order"), Normalized, TArray<FGuid>({ Body, Hood }));
	TestTrue(TEXT("deactivation permits an empty Exclusive Group"),
		Paper2DPlusAppearanceResolver::ApplyLayerActivation(Asset, Normalized, Hood, false, Normalized, &Reason));
	TestEqual(TEXT("only the independent body remains"), Normalized, TArray<FGuid>({ Body }));
	TestTrue(TEXT("valid authored source reports no issues"),
		Paper2DPlusAppearanceResolver::ValidateGenericAppearanceSource(Asset).IsEmpty());

	Asset->AppearancePresets[0].ActiveLayerIds = { Hat, Hood };
	TestTrue(TEXT("authored preset conflict is reported without mutation"),
		Paper2DPlusAppearanceResolver::ValidateGenericAppearanceSource(Asset).ContainsByPredicate([](const FString& Issue)
		{
			return Issue.Contains(TEXT("Exclusive Group"));
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusGenericAppearanceDefaultAndAnimationTest,
	"Paper2DPlus.Appearance.Generic.DefaultPresetAndAnimationEligibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusGenericAppearanceDefaultAndAnimationTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterLayerAsset* Asset = MakeAsset();
	const FGuid Body = Asset->Layers[0].LayerId;
	const FGuid Hat = Asset->Layers[1].LayerId;

	FCharacterLayerAnimationMapping BodyIdle;
	BodyIdle.AnimationName = TEXT("Idle");
	Asset->Layers[0].AnimationSprites.Add(BodyIdle);
	FCharacterLayerAnimationMapping HatAttack;
	HatAttack.AnimationName = TEXT("Attack");
	Asset->Layers[1].AnimationSprites.Add(HatAttack);
	FCharacterLayerAuthoredAnimationData HatIdleGameplay;
	HatIdleGameplay.LegacyAnimationName = TEXT("Idle");
	Asset->Layers[1].CookedGameplayAnimations.Add(HatIdleGameplay);

	FPaper2DPlusAppearanceDescriptor Descriptor;
	FString Reason;
	TestTrue(TEXT("default builds a generic committed descriptor"),
		Paper2DPlusAppearanceResolver::BuildDefaultDescriptor(Asset, Descriptor, &Reason));
	TestEqual(TEXT("default preset is normalized into asset order"), Descriptor.ActiveLayerIds, TArray<FGuid>({ Body, Hat }));

	TestEqual(TEXT("Idle art includes only Body"),
		Paper2DPlusAppearanceResolver::ResolveContributingArtLayerIds(Asset, Descriptor, TEXT("Idle")),
		TArray<FGuid>({ Body }));
	TestEqual(TEXT("Attack art includes only Hat"),
		Paper2DPlusAppearanceResolver::ResolveContributingArtLayerIds(Asset, Descriptor, TEXT("Attack")),
		TArray<FGuid>({ Hat }));
	TestEqual(TEXT("Idle gameplay includes only Hat"),
		Paper2DPlusAppearanceResolver::ResolveContributingGameplayLayerIds(Asset, Descriptor, TEXT("Idle")),
		TArray<FGuid>({ Hat }));
	TestTrue(TEXT("Attack gameplay has no contribution"),
		Paper2DPlusAppearanceResolver::ResolveContributingGameplayLayerIds(Asset, Descriptor, TEXT("Attack")).IsEmpty());

	Asset->DefaultAppearancePresetId.Invalidate();
	TestFalse(TEXT("missing default is rejected"),
		Paper2DPlusAppearanceResolver::BuildDefaultDescriptor(Asset, Descriptor, &Reason));
	TestTrue(TEXT("missing default reason is explicit"), Reason.Contains(TEXT("Default Appearance")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
