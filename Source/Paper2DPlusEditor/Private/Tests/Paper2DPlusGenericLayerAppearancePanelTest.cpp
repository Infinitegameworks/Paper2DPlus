// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "LayerAppearancePanel.h"
#include "Paper2DPlusAppearanceResolver.h"
#include "Paper2DPlusCharacterLayerAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusGenericLayerAppearancePanelTest,
	"Paper2DPlus.Editor.LayerWorkspace.GenericAppearanceAuthoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPaper2DPlusGenericLayerAppearancePanelTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusCharacterLayerAsset* Asset = NewObject<UPaper2DPlusCharacterLayerAsset>();
	Asset->UsageMode = ECharacterLayerUsageMode::RuntimeCustomizable;
	Asset->LayerSchemaVersion = UPaper2DPlusCharacterLayerAsset::GenericLayerSchemaVersion;
	Asset->Layers.AddDefaulted_GetRef().LayerName = TEXT("Hat");
	Asset->Layers.AddDefaulted_GetRef().LayerName = TEXT("Hood");
	const FGuid HatId = Asset->Layers[0].LayerId;
	const FGuid HoodId = Asset->Layers[1].LayerId;

	TSharedRef<SLayerAppearancePanel> Panel = SNew(SLayerAppearancePanel).LayerAsset(Asset);
	TestEqual(TEXT("new asset begins without implicit presets"), Panel->GetPresetCountForTests(), 0);

	FGuid DefaultId;
	TestTrue(TEXT("designer can add a complete preset"), Panel->AddPreset(TEXT("Default"), DefaultId));
	TestTrue(TEXT("first preset becomes the required default"), Asset->DefaultAppearancePresetId == DefaultId);
	TestTrue(TEXT("new generic source validates"),
		Paper2DPlusAppearanceResolver::ValidateGenericAppearanceSource(Asset).IsEmpty());

	FGuid HeadwearId;
	TestTrue(TEXT("designer can add an optional Exclusive Group"),
		Panel->AddExclusiveGroup(TEXT("Headwear"), HeadwearId));
	TestTrue(TEXT("Hat can join the Exclusive Group"),
		Panel->AssignLayerToExclusiveGroup(HatId, HeadwearId));
	TestTrue(TEXT("Hood can join the Exclusive Group"),
		Panel->AssignLayerToExclusiveGroup(HoodId, HeadwearId));

	TestTrue(TEXT("Hat activates"), Panel->SetLayerActiveInPreset(DefaultId, HatId, true));
	TestTrue(TEXT("Hood activation replaces its peer"),
		Panel->SetLayerActiveInPreset(DefaultId, HoodId, true));
	const FCharacterLayerAppearancePreset* Default = Asset->GetAppearancePresetById(DefaultId);
	TestTrue(TEXT("later peer remains selected"), Default && Default->ActiveLayerIds.Contains(HoodId));
	TestFalse(TEXT("former peer is removed in the same edit"), Default && Default->ActiveLayerIds.Contains(HatId));
	const TArray<FGuid> DefaultSelection = Default ? Default->ActiveLayerIds : TArray<FGuid>();

	FGuid AlternateId;
	TestTrue(TEXT("adding a preset copies the complete selected snapshot"),
		Panel->AddPreset(TEXT("Alternate"), AlternateId));
	const FCharacterLayerAppearancePreset* Alternate = Asset->GetAppearancePresetById(AlternateId);
	TestTrue(TEXT("copied preset has the same complete selection"),
		Alternate && Alternate->ActiveLayerIds == DefaultSelection);
	TestTrue(TEXT("designer can designate the alternate default"), Panel->SetDefaultPreset(AlternateId));
	TestTrue(TEXT("default identity updates explicitly"), Asset->DefaultAppearancePresetId == AlternateId);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
