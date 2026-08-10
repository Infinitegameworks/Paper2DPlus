// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AnimationProfileSwitcher.h"
#include "CharacterLayerAssetEditorToolkit.h"
#include "CharacterProfileEditorModel.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Texture2D.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "LayerAuthoringWorkspace.h"
#include "LayerCompositeThumbnail.h"
#include "LayerOverviewPanel.h"
#include "LayerStructureTree.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "ProfileItemPicker.h"
#include "ProfileNavigatorPanel.h"
#include "UObject/Package.h"

namespace Paper2DPlusLayerWorkspaceTest
{
	struct FFixture
	{
		UPackage* Package = nullptr;
		UPaper2DPlusCharacterProfileAsset* Profile = nullptr;
		UPaper2DPlusCharacterLayerAsset* LayerAsset = nullptr;
		TSharedPtr<FCharacterProfileEditorModel> Model;
	};

	FFixture MakeFixture(int32 AnimationCount = 3, int32 LayerCount = 3, bool bPopulateLayerArt = false)
	{
		FFixture Fixture;
		Fixture.Package = CreatePackage(*FString::Printf(
			TEXT("/Temp/Paper2DPlusLayerWorkspace_%s"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		Fixture.Profile = NewObject<UPaper2DPlusCharacterProfileAsset>(
			Fixture.Package,
			TEXT("Profile"),
			RF_Transactional);
		TArray<TArray<UPaperSprite*>> SpritesByAnimation;
		SpritesByAnimation.SetNum(AnimationCount);
		UTexture2D* SharedTexture = nullptr;
		if (bPopulateLayerArt)
		{
			SharedTexture = UTexture2D::CreateTransient(16, 16, PF_B8G8R8A8);
			SharedTexture->Source.Init(16, 16, 1, 1, TSF_BGRA8);
		}
		for (int32 Index = 0; Index < AnimationCount; ++Index)
		{
			FFlipbookProfileEntry Entry;
			Entry.Identity.FlipbookName = FString::Printf(TEXT("Animation_%03d"), Index);
			Entry.CombatData.Frames.SetNum(4);
			if (bPopulateLayerArt)
			{
				UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(
					Fixture.Package,
					FName(*FString::Printf(TEXT("Flipbook_%03d"), Index)),
					RF_Transactional);
				FScopedFlipbookMutator Mutator(Flipbook);
				Mutator.FramesPerSecond = 10.0f;
				for (int32 FrameIndex = 0; FrameIndex < 4; ++FrameIndex)
				{
					UPaperSprite* Sprite = NewObject<UPaperSprite>(
						Fixture.Package,
						FName(*FString::Printf(TEXT("Sprite_%03d_%03d"), Index, FrameIndex)));
					FSpriteAssetInitParameters SpriteInit;
					SpriteInit.Texture = SharedTexture;
					SpriteInit.Offset = FIntPoint::ZeroValue;
					SpriteInit.Dimension = FIntPoint(16, 16);
					SpriteInit.SetPixelsPerUnrealUnit(1.0f);
					Sprite->InitializeSprite(SpriteInit);
					SpritesByAnimation[Index].Add(Sprite);

					FPaperFlipbookKeyFrame& KeyFrame = Mutator.KeyFrames.AddDefaulted_GetRef();
					KeyFrame.Sprite = Sprite;
					KeyFrame.FrameRun = 1;
				}
				Entry.Identity.Flipbook = Flipbook;
			}
			Fixture.Profile->Flipbooks.Add(MoveTemp(Entry));
		}

		Fixture.LayerAsset = NewObject<UPaper2DPlusCharacterLayerAsset>(
			Fixture.Package,
			TEXT("Layers"),
			RF_Transactional);
		Fixture.LayerAsset->BaseProfile = Fixture.Profile;
		for (int32 Index = 0; Index < LayerCount; ++Index)
		{
			FCharacterLayer& Layer = Fixture.LayerAsset->Layers.AddDefaulted_GetRef();
			Layer.LayerId = FGuid::NewGuid();
			Layer.LayerName = FString::Printf(TEXT("Layer_%03d"), Index);
			if (bPopulateLayerArt)
			{
				for (int32 AnimationIndex = 0; AnimationIndex < AnimationCount; ++AnimationIndex)
				{
					FCharacterLayerAnimationMapping& Mapping = Layer.AnimationSprites.AddDefaulted_GetRef();
					Mapping.AnimationName = Fixture.Profile->Flipbooks[AnimationIndex].Identity.FlipbookName;
					Mapping.Flipbook = Fixture.Profile->Flipbooks[AnimationIndex].Identity.Flipbook;
					for (UPaperSprite* Sprite : SpritesByAnimation[AnimationIndex])
					{
						Mapping.Sprites.Add(Sprite);
					}
				}
			}
		}
		FCharacterLayerAppearancePreset& Default = Fixture.LayerAsset->AppearancePresets.AddDefaulted_GetRef();
		Default.PresetId = FGuid::NewGuid();
		Default.DisplayName = TEXT("Default");
		for (const FCharacterLayer& Layer : Fixture.LayerAsset->Layers) Default.ActiveLayerIds.Add(Layer.LayerId);
		Fixture.LayerAsset->DefaultAppearancePresetId = Default.PresetId;

		Fixture.Model = MakeShared<FCharacterProfileEditorModel>();
		Fixture.Model->InitializeFromAsset(Fixture.Profile);
		Fixture.Model->SetSecondaryWatchedObject(Fixture.LayerAsset);
		Fixture.Model->SetSelectedFlipbook(AnimationCount > 0 ? 0 : INDEX_NONE);
		if (LayerCount > 0)
		{
			Fixture.Model->SetSelectedLayerById(Fixture.LayerAsset->Layers[0].LayerId);
		}
		Fixture.Package->SetDirtyFlag(false);
		return Fixture;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerWorkspaceArtPreviewAndPlaybackTest,
	"Paper2DPlus.LayerWorkspace.ArtPreviewCompositesVisibleLayersAndPlays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerWorkspaceArtPreviewAndPlaybackTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusLayerWorkspaceTest;
	FFixture Fixture = MakeFixture(1, 2, true);

	TSharedPtr<SLayerCompositeThumbnail> Composite = SNew(SLayerCompositeThumbnail)
		.LayerAsset(Fixture.LayerAsset)
		.Model(Fixture.Model)
		.FrameIndex(0)
		.DrawCheckerboard(true);
	TestEqual(TEXT("editor composite resolves every visible Layer for the selected frame"),
		Composite->GetCachedPaintItemCountForTests(),
		2);
	TestEqual(TEXT("resolved sprites remain strongly retained while the preview is live"),
		Composite->GetRetainedSpriteCountForTests(),
		2);

	TSharedPtr<SLayerOverviewPanel> Overview = SNew(SLayerOverviewPanel)
		.Model(Fixture.Model)
		.LayerAsset(Fixture.LayerAsset)
		.LayerFirstPresentation(true);
	TestEqual(TEXT("main Art canvas is backed by the populated composite"),
		Overview->GetPreviewPaintItemCountForTests(),
		2);
	TestEqual(TEXT("Art frame strip exposes every key frame"),
		Overview->GetFrameCellCountForTests(),
		4);
	TestTrue(TEXT("keyboard-driven playback has a visible focus indicator"),
		Overview->HasKeyboardFocusIndicatorForTests());

	TestTrue(TEXT("Space starts local Art preview playback"),
		Overview->OnKeyDown(
			FGeometry(),
			FKeyEvent(EKeys::SpaceBar, FModifierKeysState(), 0, false, 0, 0)).IsEventHandled());
	TestTrue(TEXT("local Art preview reports playing"), Overview->IsPlayingForTests());
	TestTrue(TEXT("playback remains scheduled after advancing"), Overview->AdvancePlaybackForTests(0.11f));
	TestEqual(TEXT("playback advances the shared selected frame"), Fixture.Model->GetSelectedFrameIndex(), 1);
	Overview->OnKeyDown(
		FGeometry(),
		FKeyEvent(EKeys::Left, FModifierKeysState(), 0, false, 0, 0));
	TestFalse(TEXT("manual frame scrub stops local playback"), Overview->IsPlayingForTests());
	TestEqual(TEXT("manual frame scrub updates the shared selected frame"),
		Fixture.Model->GetSelectedFrameIndex(),
		0);

	Fixture.Package->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerWorkspaceStableStructureTest,
	"Paper2DPlus.LayerWorkspace.StableSelectionGroupsPreviewAndOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerWorkspaceStableStructureTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusLayerWorkspaceTest;
	FFixture Fixture = MakeFixture();
	const FGuid FirstId = Fixture.LayerAsset->Layers[0].LayerId;
	const FGuid MiddleId = Fixture.LayerAsset->Layers[1].LayerId;
	const FGuid LastId = Fixture.LayerAsset->Layers[2].LayerId;
	Fixture.Model->SetSelectedLayerById(MiddleId);
	int32 SelectionBroadcasts = 0;
	Fixture.Model->OnLayerSelectionChanged.AddLambda([&SelectionBroadcasts](int32)
	{
		++SelectionBroadcasts;
	});

	TestTrue(TEXT("stable selected layer moves by identity"),
		FLayerStructureController::MoveLayerByDelta(
			*Fixture.LayerAsset,
			*Fixture.Model,
			MiddleId,
			-1));
	TestEqual(TEXT("one reorder emits one layer selection update"), SelectionBroadcasts, 1);
	TestEqual(TEXT("reorder preserves selected LayerId"), Fixture.Model->GetSelectedLayerId(), MiddleId);
	TestEqual(TEXT("cached index re-resolves after reorder"), Fixture.Model->GetSelectedLayerIndex(), 0);

	Fixture.LayerAsset->Layers[0].LayerName = TEXT("Renamed_Middle");
	Fixture.Model->NotifyAssetDataChanged();
	TestEqual(TEXT("display-name change preserves selected LayerId"), Fixture.Model->GetSelectedLayerId(), MiddleId);

	const FString DigestBeforeGroups = Fixture.LayerAsset->ComputeLayerSourceDigest();
	const FGuid GroupId = FLayerStructureController::AddGroup(*Fixture.LayerAsset, *Fixture.Model);
	TestTrue(TEXT("new organization group has a stable id"), GroupId.IsValid());
	TestTrue(TEXT("layer can move to organization group"),
		FLayerStructureController::MoveLayerToGroup(
			*Fixture.LayerAsset,
			*Fixture.Model,
			MiddleId,
			GroupId));
	TestTrue(TEXT("group can be renamed"),
		FLayerStructureController::RenameGroup(
			*Fixture.LayerAsset,
			*Fixture.Model,
			GroupId,
			FText::FromString(TEXT("Body"))));
	TestEqual(TEXT("organization changes never affect the source digest"),
		Fixture.LayerAsset->ComputeLayerSourceDigest(),
		DigestBeforeGroups);
	TestTrue(TEXT("deleting a group reparents layers to Ungrouped"),
		FLayerStructureController::DeleteGroup(*Fixture.LayerAsset, *Fixture.Model, GroupId));
	TestFalse(TEXT("reparented layer has no group id"), Fixture.LayerAsset->GetLayerById(MiddleId)->GroupId.IsValid());
	TestEqual(TEXT("group deletion also leaves source digest unchanged"),
		Fixture.LayerAsset->ComputeLayerSourceDigest(),
		DigestBeforeGroups);

	Fixture.Package->SetDirtyFlag(false);
	FLayerStructureController::SetLayerPreview(
		*Fixture.LayerAsset,
		*Fixture.Model,
		MiddleId,
		false);
	TestFalse(TEXT("Preview eye changes transient visibility"),
		Fixture.Model->IsLayerVisible(TEXT("Renamed_Middle")));
	TestFalse(TEXT("Preview eye does not dirty the Layer Asset package"), Fixture.Package->IsDirty());
	TestTrue(TEXT("deleting an unselected layer preserves selected identity"),
		FLayerStructureController::DeleteLayer(*Fixture.LayerAsset, *Fixture.Model, FirstId));
	TestEqual(TEXT("unselected delete preserves selected LayerId"), Fixture.Model->GetSelectedLayerId(), MiddleId);
	TestTrue(TEXT("deleting the selected layer succeeds"),
		FLayerStructureController::DeleteLayer(*Fixture.LayerAsset, *Fixture.Model, MiddleId));
	TestEqual(TEXT("selected delete leaves its stable identity explicit"), Fixture.Model->GetSelectedLayerId(), MiddleId);
	TestEqual(TEXT("selected delete resolves to no array neighbor"), Fixture.Model->GetSelectedLayerIndex(), INDEX_NONE);
	TestNotNull(TEXT("remaining layer is untouched"), Fixture.LayerAsset->GetLayerById(LastId));
	Fixture.Package->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerWorkspaceScaleAndResponsiveTest,
	"Paper2DPlus.LayerWorkspace.SharedNavigationScaleEmptyAndResponsiveStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerWorkspaceScaleAndResponsiveTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusLayerWorkspaceTest;
	const FLayerWorkspaceResponsiveState Wide = FLayerWorkspaceResponsiveState::Resolve(1280.0f, true);
	TestTrue(TEXT("wide workspace shows requested animation drawer"), Wide.bShowAnimationDrawer);
	TestTrue(TEXT("wide workspace shows structure column"), Wide.bShowStructureColumn);
	TestFalse(TEXT("wide workspace does not duplicate structure as overlay"), Wide.bShowStructureOverlayButton);
	const FLayerWorkspaceResponsiveState Medium = FLayerWorkspaceResponsiveState::Resolve(820.0f, true);
	TestFalse(TEXT("medium workspace collapses the optional animation drawer"), Medium.bShowAnimationDrawer);
	TestTrue(TEXT("medium workspace retains structure column"), Medium.bShowStructureColumn);
	const FLayerWorkspaceResponsiveState Narrow = FLayerWorkspaceResponsiveState::Resolve(560.0f, true);
	TestFalse(TEXT("narrow workspace keeps optional animation drawer collapsed"), Narrow.bShowAnimationDrawer);
	TestFalse(TEXT("narrow workspace collapses permanent structure column"), Narrow.bShowStructureColumn);
	TestTrue(TEXT("narrow workspace exposes structure overlay button"), Narrow.bShowStructureOverlayButton);

	FFixture LargeFixture = MakeFixture(125, 4);
	TSharedPtr<SLayerAuthoringWorkspace> Workspace = SNew(SLayerAuthoringWorkspace)
		.Model(LargeFixture.Model)
		.LayerAsset(LargeFixture.LayerAsset);
	TSharedPtr<IProfileItemPickerSource> Source = Workspace->GetAnimationSourceForTests();
	TArray<FProfilePickerItem> Items;
	Source->GetItems(Items);
	TestEqual(TEXT("shared animation source projects 100+ rows without a local picker implementation"), Items.Num(), 125);
	TestEqual(TEXT("structure tree contains every authored layer"),
		Workspace->GetStructureTreeForTests()->GetLayerRowCountForTests(),
		4);
	TestTrue(TEXT("tree selection routes through stable LayerId"),
		Workspace->GetStructureTreeForTests()->SelectLayerForTests(LargeFixture.LayerAsset->Layers[3].LayerId));
	TestEqual(TEXT("tree selection updates the shared model"),
		LargeFixture.Model->GetSelectedLayerId(),
		LargeFixture.LayerAsset->Layers[3].LayerId);
	Workspace->SetModeForTests(ELayerAuthoringMode::FrameCues);
	TestEqual(TEXT("mode switches without replacing layer/animation/frame state"),
		Workspace->GetModeForTests(),
		ELayerAuthoringMode::FrameCues);
	TestEqual(TEXT("mode switch preserves animation"), LargeFixture.Model->GetSelectedFlipbookIndex(), 0);
	TestEqual(TEXT("mode switch preserves selected layer"),
		LargeFixture.Model->GetSelectedLayerId(),
		LargeFixture.LayerAsset->Layers[3].LayerId);
	TSharedPtr<SAnimationProfileSwitcher> AnimationSwitcher =
		Workspace->GetAnimationSwitcherForTests();
	TestTrue(TEXT("Layer Workspace constructs the shared visual animation rail"),
		AnimationSwitcher.IsValid());
	if (AnimationSwitcher.IsValid())
	{
		TestEqual(TEXT("Layer rail starts on the model's selected animation"),
			AnimationSwitcher->GetCurrentIdentityForTests().FallbackKey,
			FString(TEXT("Animation_000")));
		TestEqual(TEXT("Layer rail previews the next canonical animation"),
			AnimationSwitcher->GetNextIdentityForTests().FallbackKey,
			FString(TEXT("Animation_001")));
		TestTrue(TEXT("focused Down routes through the shared Layer model"),
			AnimationSwitcher->OnKeyDown(
				FGeometry(),
				FKeyEvent(EKeys::Down, FModifierKeysState(), 0, false, 0, 0)).IsEventHandled());
		TestEqual(TEXT("focused Down selects exactly the adjacent Layer animation"),
			LargeFixture.Model->GetSelectedFlipbookIndex(), 1);
		AnimationSwitcher->OnKeyDown(
			FGeometry(),
			FKeyEvent(EKeys::Up, FModifierKeysState(), 0, false, 0, 0));
		TestEqual(TEXT("focused Up restores the preceding Layer animation"),
			LargeFixture.Model->GetSelectedFlipbookIndex(), 0);
	}
	TSharedPtr<SProfileNavigatorPanel> AnimationDrawer =
		Workspace->GetAnimationDrawerForTests();
	TestTrue(TEXT("Layer Workspace retains its optional searchable animation drawer"),
		AnimationDrawer.IsValid());
	if (AnimationDrawer.IsValid())
	{
		AnimationDrawer->GenerateRowsForViewportForTests(FVector2D(280.0f, 180.0f));
		TestTrue(TEXT("Layer drawer rows receive virtualized animation previews"),
			AnimationDrawer->GetConstructedPreviewCountForTests() > 0);
	}

	FFixture EmptyFixture = MakeFixture(0, 0);
	TSharedPtr<SLayerAuthoringWorkspace> EmptyWorkspace = SNew(SLayerAuthoringWorkspace)
		.Model(EmptyFixture.Model)
		.LayerAsset(EmptyFixture.LayerAsset);
	TArray<FProfilePickerItem> EmptyItems;
	EmptyWorkspace->GetAnimationSourceForTests()->GetItems(EmptyItems);
	TestTrue(TEXT("empty profile produces an empty, valid shared picker source"), EmptyItems.IsEmpty());
	TestEqual(TEXT("empty Layer Asset produces an empty structure tree"),
		EmptyWorkspace->GetStructureTreeForTests()->GetLayerRowCountForTests(),
		0);

	UPaper2DPlusCharacterLayerAsset* MissingProfileAsset =
		NewObject<UPaper2DPlusCharacterLayerAsset>(
			EmptyFixture.Package,
			TEXT("MissingProfileLayers"),
			RF_Transactional);
	TSharedPtr<FCharacterProfileEditorModel> MissingProfileModel =
		MakeShared<FCharacterProfileEditorModel>();
	MissingProfileModel->SetSecondaryWatchedObject(MissingProfileAsset);
	TSharedPtr<SLayerAuthoringWorkspace> MissingProfileWorkspace =
		SNew(SLayerAuthoringWorkspace)
		.Model(MissingProfileModel)
		.LayerAsset(MissingProfileAsset);
	TArray<FProfilePickerItem> MissingProfileItems;
	MissingProfileWorkspace->GetAnimationSourceForTests()->GetItems(MissingProfileItems);
	TestTrue(TEXT("missing Base Profile is a valid empty picker state"), MissingProfileItems.IsEmpty());

	LargeFixture.Package->SetDirtyFlag(false);
	EmptyFixture.Package->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusLayerDockedToolLayoutTest,
	"Paper2DPlus.LayerWorkspace.LayoutKeepsStructureVisibleAndNarrowsRightRail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusLayerDockedToolLayoutTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FTabManager::FLayout> Layout =
		FCharacterLayerAssetEditorToolkit::BuildDefaultLayout();
	const TSharedRef<FJsonObject> Root = Layout->ToJson();
	TestEqual(TEXT("Layer layout uses the persistent-Structure dock contract"),
		Root->GetStringField(TEXT("Name")),
		FCharacterLayerAssetEditorToolkit::WorkspaceLayoutId.ToString());
	const TArray<TSharedPtr<FJsonValue>>& Areas = Root->GetArrayField(TEXT("Areas"));
	if (!TestEqual(TEXT("Layer layout has one primary area"), Areas.Num(), 1))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Area = Areas[0]->AsObject();
	if (!TestNotNull(TEXT("Layer primary area serializes"), Area.Get()))
	{
		return false;
	}
	TestEqual(TEXT("Layer primary area is horizontal"),
		Area->GetStringField(TEXT("Orientation")), FString(TEXT("Orient_Horizontal")));
	const TArray<TSharedPtr<FJsonValue>>& Columns = Area->GetArrayField(TEXT("Nodes"));
	if (!TestEqual(TEXT("Layer layout has Navigator, Structure, tools, and right rail"), Columns.Num(), 4))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> NavigatorStack = Columns[0]->AsObject();
	const TSharedPtr<FJsonObject> StructureStack = Columns[1]->AsObject();
	const TSharedPtr<FJsonObject> ToolStack = Columns[2]->AsObject();
	const TSharedPtr<FJsonObject> RightRail = Columns[3]->AsObject();
	if (!TestNotNull(TEXT("Navigator stack serializes"), NavigatorStack.Get())
		|| !TestNotNull(TEXT("Structure stack serializes"), StructureStack.Get())
		|| !TestNotNull(TEXT("Tool stack serializes"), ToolStack.Get())
		|| !TestNotNull(TEXT("Right rail serializes"), RightRail.Get()))
	{
		return false;
	}

	auto StackContainsTab = [](const TSharedPtr<FJsonObject>& Stack, FName TabId) -> bool
	{
		if (!Stack.IsValid()) return false;
		for (const TSharedPtr<FJsonValue>& TabValue : Stack->GetArrayField(TEXT("Tabs")))
		{
			const TSharedPtr<FJsonObject> Tab = TabValue->AsObject();
			if (Tab.IsValid() && Tab->GetStringField(TEXT("TabId")) == TabId.ToString())
			{
				return true;
			}
		}
		return false;
	};
	auto GetTabState = [](const TSharedPtr<FJsonObject>& Stack, FName TabId) -> FString
	{
		if (!Stack.IsValid()) return FString();
		for (const TSharedPtr<FJsonValue>& TabValue : Stack->GetArrayField(TEXT("Tabs")))
		{
			const TSharedPtr<FJsonObject> Tab = TabValue->AsObject();
			if (Tab.IsValid() && Tab->GetStringField(TEXT("TabId")) == TabId.ToString())
			{
				return Tab->GetStringField(TEXT("TabState"));
			}
		}
		return FString();
	};

	TestTrue(TEXT("Navigator remains optional and separate"),
		StackContainsTab(NavigatorStack, FCharacterLayerAssetEditorToolkit::FlipbookListTabId));
	TestEqual(TEXT("Navigator starts closed"),
		GetTabState(NavigatorStack, FCharacterLayerAssetEditorToolkit::FlipbookListTabId),
		FString(TEXT("ClosedTab")));
	TestTrue(TEXT("Structure owns a dedicated persistent side stack"),
		StackContainsTab(StructureStack, FCharacterLayerAssetEditorToolkit::StructureTabId));
	TestEqual(TEXT("Structure starts open"),
		GetTabState(StructureStack, FCharacterLayerAssetEditorToolkit::StructureTabId),
		FString(TEXT("OpenedTab")));
	TestFalse(TEXT("Structure does not disappear behind the active tool"),
		StackContainsTab(ToolStack, FCharacterLayerAssetEditorToolkit::StructureTabId));
	TestTrue(TEXT("Art is a real tool tab"),
		StackContainsTab(ToolStack, FCharacterLayerAssetEditorToolkit::ArtTabId));
	TestTrue(TEXT("Hitboxes is a real tool tab"),
		StackContainsTab(ToolStack, FCharacterLayerAssetEditorToolkit::HitboxesTabId));
	TestTrue(TEXT("Frame Cues is a real tool tab"),
		StackContainsTab(ToolStack, FCharacterLayerAssetEditorToolkit::FrameCuesTabId));
	TestTrue(TEXT("Appearance is a real tool tab"),
		StackContainsTab(ToolStack, FCharacterLayerAssetEditorToolkit::AppearanceTabId));
	for (const FName ToolId : {
		FCharacterLayerAssetEditorToolkit::ArtTabId,
		FCharacterLayerAssetEditorToolkit::HitboxesTabId,
		FCharacterLayerAssetEditorToolkit::FrameCuesTabId,
		FCharacterLayerAssetEditorToolkit::AppearanceTabId })
	{
		TestEqual(
			*FString::Printf(TEXT("%s opens in the visible central tool strip"), *ToolId.ToString()),
			GetTabState(ToolStack, ToolId),
			FString(TEXT("OpenedTab")));
	}
	TestEqual(TEXT("Art is the default foreground tool"),
		ToolStack->GetStringField(TEXT("ForegroundTab")),
		FCharacterLayerAssetEditorToolkit::ArtTabId.ToString());
	TestTrue(TEXT("Structure side rail keeps its compact authored width"),
		FMath::IsNearlyEqual(
			static_cast<float>(StructureStack->GetNumberField(TEXT("SizeCoefficient"))),
			0.16f));
	TestTrue(TEXT("Central tools retain the dominant authored width"),
		FMath::IsNearlyEqual(
			static_cast<float>(ToolStack->GetNumberField(TEXT("SizeCoefficient"))),
			0.55f));
	TestTrue(TEXT("Layer right rail is narrower than the previous Character-style rail"),
		FMath::IsNearlyEqual(
			static_cast<float>(RightRail->GetNumberField(TEXT("SizeCoefficient"))),
			0.17f));

	TestEqual(TEXT("Layer right rail is vertically split"),
		RightRail->GetStringField(TEXT("Orientation")), FString(TEXT("Orient_Vertical")));
	const TArray<TSharedPtr<FJsonValue>>& RightStacks = RightRail->GetArrayField(TEXT("Nodes"));
	if (!TestEqual(TEXT("Layer right rail has Tool Panels and completion stacks"), RightStacks.Num(), 2))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> ToolPanelsStack = RightStacks[0]->AsObject();
	const TSharedPtr<FJsonObject> CompletionStack = RightStacks[1]->AsObject();
	TestTrue(TEXT("Tool Panels occupy the upper right stack"),
		StackContainsTab(ToolPanelsStack, FCharacterLayerAssetEditorToolkit::ContextHostTabId));
	TestTrue(TEXT("Completion occupies the lower right stack"),
		StackContainsTab(CompletionStack, FCharacterLayerAssetEditorToolkit::CompletionTabId));
	TestTrue(TEXT("Related Profiles shares the lower right stack"),
		StackContainsTab(CompletionStack, FCharacterLayerAssetEditorToolkit::RelatedProfilesTabId));
	TestTrue(TEXT("Playback Queue shares the lower right stack"),
		StackContainsTab(CompletionStack, FCharacterLayerAssetEditorToolkit::PlaybackQueueTabId));
	return true;
}

#endif // WITH_EDITOR
