// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AnimationMapPanel.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_ChainEnd.h"
#include "AnimationMap/Paper2DPlusAnimationMapNode_ChainStart.h"
#include "AnimationsPanel.h"
#include "CharacterCompletionPanel.h"
#include "CharacterLayerAssetEditorToolkit.h"
#include "CharacterProfileAssetEditor.h"
#include "CharacterProfileEditorModel.h"
#include "CharacterProfileJsonInteraction.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Framework/Docking/LayoutService.h"
#include "GameplayTagContainer.h"
#include "HAL/FileManager.h"
#include "HitboxEditorPanel.h"
#include "SpriteEditorPanel.h"
#include "FrameTimingEditor.h"
#include "FrameEventEditor.h"
#include "Paper2DPlusEditorTestFrameCueTypes.h"
#include "FrameCues/Paper2DPlusFrameCuePreviewContext.h"
#include "FlipbookBrowserPanel.h"
#include "FlipbookListPanel.h"
#include "RootMotionEditor.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "Paper2DPlusAnimationMapLibrary.h"
#include "Paper2DPlusAnimationTagQuery.h"
#include "Paper2DPlusCombatProfileAsset.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusMoveTransition.h"
#include "PaperFlipbook.h"
#include "Types/SlateAttributeMetaData.h"
#include "PlaybackQueuePanel.h"
#include "ProfileCompletionPanel.h"
#include "ProfileDetailsPanel.h"
#include "ProfileToolPanelHost.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"

namespace Paper2DPlusProfileWorkspaceTest
{
	struct FFixture
	{
		UPaper2DPlusCharacterProfileAsset* Profile = nullptr;
		TSharedPtr<FCharacterProfileEditorModel> Model;
	};

	FFixture MakeFixture()
	{
		FFixture Fixture;
		Fixture.Profile = NewObject<UPaper2DPlusCharacterProfileAsset>(
			GetTransientPackage(), NAME_None, RF_Transactional);
		for (const TCHAR* Name : { TEXT("Idle"), TEXT("Attack") })
		{
			FFlipbookProfileEntry Entry;
			Entry.Identity.FlipbookName = Name;
			Entry.CombatData.Frames.SetNum(6);
			Fixture.Profile->Flipbooks.Add(MoveTemp(Entry));
		}
		Fixture.Model = MakeShared<FCharacterProfileEditorModel>();
		Fixture.Model->InitializeFromAsset(Fixture.Profile);
		Fixture.Model->SetSelectedFlipbook(0);
		Fixture.Model->SetSelectedFrame(3);
		return Fixture;
	}

	class FWorkspaceProvider final : public IProfileToolPanelProvider
	{
	public:
		explicit FWorkspaceProvider(TArray<FName> InPanelIds)
			: PanelIds(MoveTemp(InPanelIds))
		{
		}

		virtual FProfileToolPanelHostContract GetHostContract() const override
		{
			return FProfileToolPanelHostContract::External();
		}

		virtual void GetContextualPanels(TArray<FProfileToolPanelDescriptor>& OutPanels) const override
		{
			++DescriptorRequests;
			for (const FName PanelId : PanelIds)
			{
				FProfileToolPanelDescriptor Descriptor;
				Descriptor.PanelId = PanelId;
				Descriptor.Label = FText::FromName(PanelId);
				Descriptor.IconName = TEXT("Icons.Info");
				Descriptor.ToolTip = FText::FromString(TEXT("Workspace acceptance-test panel"));
				Descriptor.WidgetFactory = [PanelId]() -> TSharedRef<SWidget>
				{
					return SNew(STextBlock).Text(FText::FromName(PanelId));
				};
				OutPanels.Add(MoveTemp(Descriptor));
			}
		}

		mutable int32 DescriptorRequests = 0;

	private:
		TArray<FName> PanelIds;
	};

	FString MakeConfigPath(const TCHAR* Suffix)
	{
		return FPaths::Combine(
			FPaths::ProjectSavedDir(),
			FString::Printf(TEXT("Paper2DPlusWorkspace_%s_%s.ini"),
				Suffix,
				*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	}

	void CleanupConfig(const FString& ConfigPath)
	{
		if (GConfig)
		{
			GConfig->UnloadFile(ConfigPath);
		}
		IFileManager::Get().Delete(*ConfigPath, false, true);
	}

	TSharedPtr<SProfileToolPanelHost> MakeHost(
		const TSharedRef<SDockTab>& OwnerTab,
		const FString& ConfigPath,
		FName Scope)
	{
		TSharedPtr<SProfileToolPanelHost> Host = SNew(SProfileToolPanelHost)
			.OwnerTab(OwnerTab)
			.LayoutScope(Scope)
			.LayoutConfigPath(ConfigPath);
		OwnerTab->SetContent(Host.ToSharedRef());
		return Host;
	}

	// ─────────────────────────────────────────────────────────────────────────────────────────────
	// TASK-151 U1/U3 — constructed-widget probes.
	//
	// The Animations header View control and the Map canvas rail are pure Slate composition and
	// neither panel exposes a per-control test seam for them, so these tests read the CONSTRUCTED
	// widget tree: every STextBlock label plus whether it is actually on screen. That distinction is
	// the whole point of the U3 state matrix — the rail expresses "contextual", "inactive filter" and
	// "no attention needed" as Collapsed visibility on widgets it always builds, so a probe that only
	// asked "does this label exist" would pass no matter what state the panel is in.
	//
	// Same type-name walk SProfileToolPanelHost::CountSectionDescendantWidgetsForTests already uses.
	// ─────────────────────────────────────────────────────────────────────────────────────────────
	void Workspace_CollectWidgetTexts(
		const TSharedRef<SWidget>& Widget,
		bool bAncestorsVisible,
		TArray<FString>& OutVisibleTexts,
		TArray<FString>& OutAllTexts)
	{
		// A `.Visibility_Lambda` binding is a TSlateAttribute, and `SWidget::GetVisibility()` returns the
		// CACHED value: binding does not evaluate the getter (UE_SLATE_WITH_ATTRIBUTE_INITIALIZATION_ON_BIND
		// is 0) and only the prepass/paint attribute-update pass re-runs it. A headless panel is constructed
		// but never painted, so every Visibility_Lambda control would otherwise read as its construction-time
		// default — Visible — which is exactly the state the Map rail expresses "contextual", "inactive
		// filter" and "no attention needed" through, and a switcher's WidgetIndex_Lambda would likewise
		// report the wrong active slot. (`STextBlock::GetText()` is NOT affected: it calls UpdateNow on its
		// own bound text.) Running the same update SWidget::SlatePrepass runs, on each widget as the walk
		// reaches it, makes the probe observe what a painted panel would show; the walk is top-down, so a
		// parent is refreshed before its children are read. Same signature on UE 5.0-5.8.
		FSlateAttributeMetaData::UpdateAllAttributes(
			Widget.Get(),
			FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidationIfConstructed);

		// A Collapsed/Hidden ancestor hides its whole subtree, so visibility has to accumulate down.
		const bool bVisible = bAncestorsVisible && Widget->GetVisibility().IsVisible();
		if (Widget->GetType() == FName(TEXT("STextBlock")))
		{
			const FString Text = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
			OutAllTexts.Add(Text);
			if (bVisible)
			{
				OutVisibleTexts.Add(Text);
			}
		}
		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 Index = 0; Index < Children->Num(); ++Index)
			{
				Workspace_CollectWidgetTexts(
					Children->GetChildAt(Index),
					bVisible,
					OutVisibleTexts,
					OutAllTexts);
			}
		}
	}

	int32 Workspace_CountWidgetType(const TSharedRef<SWidget>& Widget, FName WidgetType)
	{
		int32 Count = Widget->GetType() == WidgetType ? 1 : 0;
		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 ChildIndex = 0; ChildIndex < Children->Num(); ++ChildIndex)
			{
				Count += Workspace_CountWidgetType(Children->GetChildAt(ChildIndex), WidgetType);
			}
		}
		return Count;
	}

	struct FWorkspaceWidgetTexts
	{
		/** Labels a designer can actually read right now. */
		TArray<FString> Visible;
		/** Every label the surface CONSTRUCTED, visible or collapsed. "Built but collapsed" is how a
		 *  contextual action differs from a command that moved into an overflow menu entirely. */
		TArray<FString> All;

		bool HasVisible(const TCHAR* Exact) const
		{
			return Visible.Contains(FString(Exact));
		}
		bool HasAny(const TCHAR* Exact) const
		{
			return All.Contains(FString(Exact));
		}
		bool HasVisibleContaining(const TCHAR* Substring) const
		{
			return Visible.ContainsByPredicate(
				[Substring](const FString& Text) { return Text.Contains(Substring); });
		}
		bool HasAnyContaining(const TCHAR* Substring) const
		{
			return All.ContainsByPredicate(
				[Substring](const FString& Text) { return Text.Contains(Substring); });
		}
	};

	FWorkspaceWidgetTexts Workspace_ReadWidgetTexts(const TSharedRef<SWidget>& Root)
	{
		FWorkspaceWidgetTexts Texts;
		Workspace_CollectWidgetTexts(Root, /*bAncestorsVisible=*/true, Texts.Visible, Texts.All);
		return Texts;
	}

	/** Mirrors SAnimationsPanel's per-asset view-mode key recipe so the persistence assertions can look
	 *  for the exact entry the panel would have written. */
	FString Workspace_AnimationsViewModeKey(const UObject* Asset)
	{
		FString AssetKey = Asset ? Asset->GetPathName() : TEXT("NoAsset");
		AssetKey.ReplaceInline(TEXT("/"), TEXT("_"));
		AssetKey.ReplaceInline(TEXT("."), TEXT("_"));
		AssetKey.ReplaceInline(TEXT(":"), TEXT("_"));
		AssetKey.ReplaceInline(TEXT(" "), TEXT("_"));
		return FString::Printf(TEXT("ViewMode_%s"), *AssetKey);
	}

	const TCHAR* Workspace_AnimationsViewModeSection = TEXT("Paper2DPlus.AnimationsTab");
	/** The Map panel's own GEditorPerProjectIni section/key for the persisted tag filter. */
	const TCHAR* Workspace_AnimationMapSection = TEXT("CharacterProfileEditor");
	const TCHAR* Workspace_AnimationMapFilterKey = TEXT("AnimationMapFilterTag");

	/** Scoped override of the Map's persisted filter tag, which SAnimationMapPanel restores in Construct
	 *  (SetMapFilterTag is panel-private, so this is the only route a test can drive the ACTIVE filter
	 *  state through today). Restoring the editor's own value on scope exit keeps these tests from
	 *  leaving a filter behind — and pins the filter state a test that must not depend on it. */
	struct FWorkspaceScopedMapFilterConfig
	{
		explicit FWorkspaceScopedMapFilterConfig(const FString& InTagName)
		{
			if (!GConfig)
			{
				return;
			}
			bHadPrevious = GConfig->GetString(
				Workspace_AnimationMapSection,
				Workspace_AnimationMapFilterKey,
				Previous,
				GEditorPerProjectIni);
			Apply(InTagName, /*bHasValue=*/!InTagName.IsEmpty());
		}

		~FWorkspaceScopedMapFilterConfig()
		{
			if (GConfig)
			{
				Apply(Previous, bHadPrevious);
			}
		}

	private:
		static void Apply(const FString& Value, bool bHasValue)
		{
			if (bHasValue)
			{
				GConfig->SetString(
					Workspace_AnimationMapSection,
					Workspace_AnimationMapFilterKey,
					*Value,
					GEditorPerProjectIni);
			}
			else
			{
				GConfig->RemoveKey(
					Workspace_AnimationMapSection,
					Workspace_AnimationMapFilterKey,
					GEditorPerProjectIni);
			}
		}

		FString Previous;
		bool bHadPrevious = false;
	};

	struct FMapFixture
	{
		UPaper2DPlusCharacterProfileAsset* Profile = nullptr;
		TSharedPtr<FCharacterProfileEditorModel> Model;
		FGameplayTag GroupTag;
	};

	/** One exact Animation Group holding a three-move chain (Jab -> Cross -> Uppercut) opened by a
	 *  flagged Chain Start — the smallest fixture that gives the Map a real group to scope to and a
	 *  real chain for phase derivation. Group membership resolves only for entries whose profile row
	 *  carries a non-null flipbook reference, so every move gets one. */
	FMapFixture Workspace_MakeChainFixture()
	{
		FMapFixture Fixture;
		Fixture.GroupTag = FGameplayTag::RequestGameplayTag(
			FName(TEXT("Paper2DPlus.Animation.Combat.Combo")), false);
		Fixture.Profile = NewObject<UPaper2DPlusCharacterProfileAsset>(
			GetTransientPackage(), NAME_None, RF_Transactional);
		for (const TCHAR* Name : { TEXT("Jab"), TEXT("Cross"), TEXT("Uppercut") })
		{
			FFlipbookProfileEntry& Entry = Fixture.Profile->Flipbooks.AddDefaulted_GetRef();
			Entry.Identity.FlipbookName = Name;
			Entry.Identity.Flipbook = NewObject<UPaperFlipbook>(
				Fixture.Profile,
				MakeUniqueObjectName(Fixture.Profile, UPaperFlipbook::StaticClass(), FName(Name)));
		}
		Fixture.Profile->Flipbooks[0].TransitionData.Transitions.Add(
			FPaper2DPlusMoveTransition(TEXT("Cross")));
		Fixture.Profile->Flipbooks[1].TransitionData.Transitions.Add(
			FPaper2DPlusMoveTransition(TEXT("Uppercut")));
		if (Fixture.GroupTag.IsValid())
		{
			FFlipbookTagMapping& Mapping = Fixture.Profile->TagMappings.FindOrAdd(Fixture.GroupTag);
			for (const TCHAR* Name : { TEXT("Jab"), TEXT("Cross"), TEXT("Uppercut") })
			{
				Mapping.Entries.Add(FFlipbookTagMappingEntry(Name));
			}
			Mapping.Entries[0].bIsChainStart = true;
		}
		Fixture.Model = MakeShared<FCharacterProfileEditorModel>();
		Fixture.Model->InitializeFromAsset(Fixture.Profile);
		return Fixture;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceQueueNavigationTest,
	"Paper2DPlus.ProfileWorkspace.QueueNavigationWrapsAndSurvivesOffQueueSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceQueueNavigationTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;

	UPaper2DPlusCharacterProfileAsset* Profile = NewObject<UPaper2DPlusCharacterProfileAsset>(
		GetTransientPackage(), NAME_None, RF_Transactional);
	// Distinct key-frame counts prove the frame cursor lands using the DESTINATION animation's length.
	const int32 FrameCounts[] = { 2, 5, 3, 4 };
	int32 NameIndex = 0;
	for (const TCHAR* Name : { TEXT("A"), TEXT("B"), TEXT("C"), TEXT("Offlist") })
	{
		FFlipbookProfileEntry& Entry = Profile->Flipbooks.AddDefaulted_GetRef();
		Entry.Identity.FlipbookName = Name;
		Entry.Identity.Flipbook = NewObject<UPaperFlipbook>(Profile, FName(Name));
		{
			FScopedFlipbookMutator Mutator(Entry.Identity.Flipbook.Get());
			Mutator.FramesPerSecond = 10.0f;
			Mutator.KeyFrames.SetNum(FrameCounts[NameIndex]);
			for (FPaperFlipbookKeyFrame& KeyFrame : Mutator.KeyFrames)
			{
				KeyFrame.FrameRun = 1;
			}
		}
		Entry.CombatData.Frames.SetNum(FrameCounts[NameIndex]);
		++NameIndex;
	}

	const TSharedRef<FCharacterProfileEditorModel> Model = MakeShared<FCharacterProfileEditorModel>();
	Model->InitializeFromAsset(Profile);
	for (const int32 Index : { 0, 1, 2 })
	{
		Model->AddToQueue(Index);
	}

	// --- Forward stepping wraps past the last entry. The old per-panel copies clamped Up/Down at the
	// ends, so the queue parked on its final entry with no way back to the front.
	Model->SetSelectedFlipbook(2);
	TestEqual(TEXT("cursor reconciles onto the selected last entry"),
		Model->GetPlaybackQueueIndex(), 2);
	TestEqual(TEXT("stepping forward from the last entry wraps to the first"),
		Model->StepQueue(1), 0);
	TestEqual(TEXT("wrapped step selects the first queued animation"),
		Model->GetSelectedFlipbookIndex(), 0);
	TestEqual(TEXT("a forward step lands on frame 0"), Model->GetSelectedFrameIndex(), 0);

	// --- Backward stepping wraps and lands on the DESTINATION animation's last frame (C has 3).
	TestEqual(TEXT("stepping back from the first entry wraps to the last"),
		Model->StepQueue(-1, /*bLandOnLastFrame=*/true), 2);
	TestEqual(TEXT("backward wrap lands on the destination's final frame"),
		Model->GetSelectedFrameIndex(), 2);

	// --- The desync that made arrow keys stick: selecting from a browser/navigator never touched the
	// cursor, so the next step was computed from an unrelated slot. The cursor now reconciles.
	Model->SetSelectedFlipbook(1);
	TestEqual(TEXT("an out-of-band selection reconciles the cursor"),
		Model->GetPlaybackQueueIndex(), 1);
	TestEqual(TEXT("stepping continues from the reconciled position"),
		Model->StepQueue(1), 2);

	// --- Selecting an animation that is NOT queued must not teleport into the queue. StepQueue reports
	// INDEX_NONE so each panel falls back to wrapping within the current flipbook.
	Model->SetSelectedFlipbook(3);
	TestEqual(TEXT("an off-queue selection has no queue position"),
		Model->FindQueuePositionForSelection(), INDEX_NONE);
	TestEqual(TEXT("stepping forward off-queue is refused"), Model->StepQueue(1), INDEX_NONE);
	TestEqual(TEXT("stepping back off-queue is refused"), Model->StepQueue(-1), INDEX_NONE);
	TestEqual(TEXT("a refused step leaves the selection alone"),
		Model->GetSelectedFlipbookIndex(), 3);
	TestEqual(TEXT("adjacent lookup is empty off-queue so onion skin draws nothing"),
		Model->GetQueueAdjacentFlipbookIndex(1), INDEX_NONE);

	// --- A single-entry queue has no neighbor: stepping must be refused rather than re-selecting the
	// animation already on screen (which read as "stuck" because the frame never moved).
	Model->ClearQueue();
	Model->AddToQueue(1);
	Model->SetSelectedFlipbook(1);
	TestEqual(TEXT("single-entry queue refuses a forward step"), Model->StepQueue(1), INDEX_NONE);
	TestEqual(TEXT("single-entry queue refuses a backward step"), Model->StepQueue(-1), INDEX_NONE);
	TestEqual(TEXT("single-entry queue exposes no adjacent animation"),
		Model->GetQueueAdjacentFlipbookIndex(-1), INDEX_NONE);

	// --- A duplicated entry must keep the slot the caller stepped to, or the queue can never be
	// walked past that duplicate.
	Model->ClearQueue();
	for (const int32 Index : { 0, 1, 0, 2 })
	{
		Model->AddToQueue(Index);
	}
	Model->SetPlaybackQueueIndex(2);
	Model->SetSelectedFlipbook(0);
	Model->SetPlaybackQueueIndex(2);
	TestEqual(TEXT("cursor stays on the duplicate slot the caller chose"),
		Model->FindQueuePositionForSelection(), 2);
	TestEqual(TEXT("stepping past a duplicate reaches the following entry"),
		Model->StepQueue(1), 2);
	TestEqual(TEXT("cursor advanced to the entry after the duplicate"),
		Model->GetPlaybackQueueIndex(), 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceQueuePanelTest,
	"Paper2DPlus.ProfileWorkspace.PlaybackQueuePanelRendersQueueRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceQueuePanelTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	FFixture Fixture = MakeFixture();
	for (int32 Index = 0; Index < Fixture.Profile->Flipbooks.Num(); ++Index)
	{
		Fixture.Profile->Flipbooks[Index].Identity.Flipbook =
			NewObject<UPaperFlipbook>(Fixture.Profile);
	}

	const TSharedRef<SPlaybackQueuePanel> Panel = SNew(SPlaybackQueuePanel)
		.Model(Fixture.Model);
	TestEqual(TEXT("an empty queue shows only the drop zone"),
		Panel->GetQueueRowCountForTests(), 0);

	Fixture.Model->AddToQueue(0);
	Fixture.Model->AddToQueue(1);
	TestEqual(TEXT("the panel renders one row per queued animation"),
		Panel->GetQueueRowCountForTests(), 2);

	Fixture.Model->RemoveFromQueue(0);
	TestEqual(TEXT("removing an entry drops its row"),
		Panel->GetQueueRowCountForTests(), 1);

	Fixture.Model->ClearQueue();
	TestEqual(TEXT("clearing returns the panel to the empty state"),
		Panel->GetQueueRowCountForTests(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceFlipbookAddTest,
	"Paper2DPlus.ProfileWorkspace.FlipbookBrowserAddsExistingFlipbooksWithoutDuplicates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceFlipbookAddTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	FFixture Fixture = MakeFixture();
	UPaperFlipbook* ExistingIdle = NewObject<UPaperFlipbook>(
		Fixture.Profile, TEXT("ExistingIdleAsset"));
	UPaperFlipbook* CollidingAttack = NewObject<UPaperFlipbook>(
		Fixture.Profile, TEXT("Attack"));
	UPaperFlipbook* Run = NewObject<UPaperFlipbook>(
		Fixture.Profile, TEXT("Run"));
	Fixture.Profile->Flipbooks[0].Identity.Flipbook = ExistingIdle;
	Fixture.Model->SetSelectedFlipbook(0);

	TSharedPtr<SFlipbookBrowserPanel> Browser = SNew(SFlipbookBrowserPanel)
		.Model(Fixture.Model);
	// TASK-151 U2 (R4): Add Flipbooks is the browse bar's ONLY persistent collection mutation.
	TestEqual(TEXT("Add Flipbooks is the only persistent collection mutation"),
		Browser->GetPersistentCollectionActionCountForTests(), 1);

	const int32 Added = Browser->AddFlipbooksForTests({
		ExistingIdle,
		CollidingAttack,
		Run });
	TestEqual(TEXT("duplicate asset is skipped while two new flipbooks are added"), Added, 2);
	TestEqual(TEXT("profile contains the two original and two accepted entries"),
		Fixture.Profile->Flipbooks.Num(), 4);
	TestEqual(TEXT("a colliding animation name receives a deterministic suffix"),
		Fixture.Profile->Flipbooks[2].Identity.FlipbookName, FString(TEXT("Attack (2)")));
	TestEqual(TEXT("a free animation name uses the Paper Flipbook asset name"),
		Fixture.Profile->Flipbooks[3].Identity.FlipbookName, FString(TEXT("Run")));
	TestEqual(TEXT("accepted entry stores the selected Paper Flipbook"),
		Fixture.Profile->Flipbooks[3].Identity.Flipbook.ToSoftObjectPath(),
		FSoftObjectPath(Run));
	TestEqual(TEXT("explicit assignment synchronizes authored frame rows"),
		Fixture.Profile->Flipbooks[3].CombatData.Frames.Num(), Run->GetNumKeyFrames());
	TestEqual(TEXT("the last accepted flipbook becomes the shared selection"),
		Fixture.Model->GetSelectedFlipbookIndex(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceFlipbookDeleteTest,
	"Paper2DPlus.ProfileWorkspace.FlipbookBrowserDeletesSelectedEntryAndRebasesReferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceFlipbookDeleteTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	FFixture Fixture;
	Fixture.Profile = NewObject<UPaper2DPlusCharacterProfileAsset>(
		GetTransientPackage(), NAME_None, RF_Transactional);
	TArray<UPaperFlipbook*> Flipbooks;
	for (const TCHAR* Name : { TEXT("A"), TEXT("B"), TEXT("C") })
	{
		UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(Fixture.Profile, FName(Name));
		Flipbooks.Add(Flipbook);
		FFlipbookProfileEntry& Entry = Fixture.Profile->Flipbooks.AddDefaulted_GetRef();
		Entry.Identity.FlipbookName = Name;
		Entry.Identity.Flipbook = Flipbook;
	}
	Fixture.Profile->Flipbooks[0].TransitionData.Transitions.Add(
		FPaper2DPlusMoveTransition(TEXT("B")));
	Fixture.Profile->Flipbooks[1].TransitionData.Transitions.Add(
		FPaper2DPlusMoveTransition(TEXT("C")));
	Fixture.Profile->Flipbooks[2].TransitionData.Transitions.Add(
		FPaper2DPlusMoveTransition(TEXT("B")));
	Fixture.Profile->ThumbnailFlipbookName = TEXT("B");
#if WITH_EDITORONLY_DATA
	Fixture.Profile->AnimationMapNodePositions.Add(TEXT("b"), FVector2D(120.0, 40.0));
#endif

	const FGameplayTag MappingTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Paper2DPlus.Animation.Combat")), false);
	if (!TestTrue(TEXT("mapping tag is registered"), MappingTag.IsValid()))
	{
		return false;
	}
	Fixture.Profile->TagMappings.FindOrAdd(MappingTag).Entries.Add(
		FFlipbookTagMappingEntry(TEXT("B")));

	Fixture.Model = MakeShared<FCharacterProfileEditorModel>();
	Fixture.Model->InitializeFromAsset(Fixture.Profile);
	for (int32 Index = 0; Index < Fixture.Profile->Flipbooks.Num(); ++Index)
	{
		Fixture.Model->AddToQueue(Index);
	}
	Fixture.Model->SetSelectedFlipbook(1);
	TSharedPtr<SFlipbookBrowserPanel> Browser = SNew(SFlipbookBrowserPanel)
		.Model(Fixture.Model);

	// TASK-151 U2 (R6/AE3): Delete is contextual — absent without a deletable selection.
	TestTrue(TEXT("Delete is exposed for a deletable selection"),
		Browser->IsDeleteActionVisibleForTests());
	Fixture.Model->SetSelectedFlipbook(INDEX_NONE);
	TestFalse(TEXT("Delete is absent with no deletable selection"),
		Browser->IsDeleteActionVisibleForTests());
	Fixture.Model->SetSelectedFlipbook(1);
	TestTrue(TEXT("Delete reappears when a deletable animation is selected again"),
		Browser->IsDeleteActionVisibleForTests());

	// TASK-151 U2 (R19/KTD12) — assert the focus DECISION, not a bookkeeping echo.
	//
	// FocusStableBrowseControl() only calls FSlateApplication::SetKeyboardFocus when the chosen control
	// is hosted in a live window, and headless automation deliberately constructs this panel outside
	// any window (it must never push focus into the running editor). Real keyboard focus is therefore
	// unobservable here, so what is asserted is which stable control the panel CHOSE and that it
	// chooses one only when a Delete actually removed its own target. Those are asserted as a state
	// TRANSITION across three attempts, so a constant or unconditional implementation fails, and the
	// recorded name is cross-checked against the browse bar's registry of persistent controls so it
	// cannot name a control the bar never built.
	TestEqual(TEXT("no browse-focus hand-off is recorded before any Delete runs"),
		Browser->GetLastBrowseFocusTargetForTests(), FName(NAME_None));

	Fixture.Model->SetSelectedFlipbook(INDEX_NONE);
	TestFalse(TEXT("a Delete with no deletable selection is refused"),
		Browser->DeleteSelectedFlipbookFromBarForTests());
	TestEqual(TEXT("a refused Delete hands focus nowhere"),
		Browser->GetLastBrowseFocusTargetForTests(), FName(NAME_None));
	TestEqual(TEXT("a refused Delete removes nothing"), Fixture.Profile->Flipbooks.Num(), 3);

	// The bar-driven route runs the unchanged confirmation/mutation funnel, then hands focus back to a
	// stable browse control because the contextual action just removed its own target.
	Fixture.Model->SetSelectedFlipbook(1);
	TestTrue(TEXT("selected middle animation is deleted"),
		Browser->DeleteSelectedFlipbookFromBarForTests());
	TestEqual(TEXT("a successful Delete chooses the persistent Add control for focus"),
		Browser->GetLastBrowseFocusTargetForTests(), FName(TEXT("AddFlipbooks")));
	TestTrue(TEXT("the chosen focus target is a control the browse bar actually keeps persistent"),
		Browser->GetPersistentCollectionActionsForTests().Contains(
			Browser->GetLastBrowseFocusTargetForTests()));
	TestEqual(TEXT("only the selected profile entry is removed"), Fixture.Profile->Flipbooks.Num(), 2);
	TestEqual(TEXT("first surviving entry remains A"),
		Fixture.Profile->Flipbooks[0].Identity.FlipbookName, FString(TEXT("A")));
	TestEqual(TEXT("second surviving entry remains C"),
		Fixture.Profile->Flipbooks[1].Identity.FlipbookName, FString(TEXT("C")));
	const FFlipbookTagMapping* Mapping = Fixture.Profile->TagMappings.Find(MappingTag);
	TestTrue(TEXT("tag mapping no longer references the deleted animation"),
		Mapping && Mapping->Entries.IsEmpty());
	TestTrue(TEXT("incoming transition rows to the deleted animation are pruned"),
		Fixture.Profile->Flipbooks[0].TransitionData.Transitions.IsEmpty()
		&& Fixture.Profile->Flipbooks[1].TransitionData.Transitions.IsEmpty());
	TestTrue(TEXT("thumbnail override no longer names the deleted animation"),
		Fixture.Profile->ThumbnailFlipbookName.IsEmpty());
#if WITH_EDITORONLY_DATA
	TestFalse(TEXT("Animation Map placement is pruned"),
		Fixture.Profile->AnimationMapNodePositions.Contains(TEXT("b")));
#endif
	TestEqual(TEXT("queue keeps the surviving animations in their original order"),
		Fixture.Model->GetPlaybackQueue(), TArray<int32>({ 0, 1 }));
	TestEqual(TEXT("selection advances to the same surviving C object"),
		Fixture.Model->GetSelectedFlipbookIndex(), 1);
	TestTrue(TEXT("selected identity remains attached to C after array rebasing"),
		Fixture.Model->GetSelectedFlipbookObject().Get() == Flipbooks[2]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceActivationTest,
	"Paper2DPlus.ProfileWorkspace.ActivationChangesProviderOnceAndRetainsSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceActivationTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	FFixture Fixture = MakeFixture();
	Fixture.Profile->Flipbooks[0].EditorMeta.CompletionFlags = (1 << 0) | (1 << 5);
	const FString ConfigPath = MakeConfigPath(TEXT("Activation"));
	const TSharedRef<SDockTab> OwnerTab = SNew(SDockTab);
	TSharedPtr<SProfileToolPanelHost> Host = MakeHost(OwnerTab, ConfigPath, TEXT("Activation"));
	TSharedPtr<FWorkspaceProvider> HitboxProvider = MakeShared<FWorkspaceProvider>(
		TArray<FName>({ TEXT("Workspace.Hitbox.Details") }));
	TSharedPtr<FWorkspaceProvider> CueProvider = MakeShared<FWorkspaceProvider>(
		TArray<FName>({ TEXT("Workspace.Cues.Details") }));

	Host->RequestActiveTool(TEXT("Hitboxes"), HitboxProvider);
	TestTrue(TEXT("first activation applies"), Host->ApplyPendingSwitchForTests());
	TestEqual(TEXT("provider descriptors requested exactly once"), HitboxProvider->DescriptorRequests, 1);
	Host->RequestActiveTool(TEXT("Hitboxes"), HitboxProvider);
	TestFalse(TEXT("reactivating the same tool/provider is a no-op"), Host->ApplyPendingSwitchForTests());
	TestEqual(TEXT("same activation never rebuilds the provider"), HitboxProvider->DescriptorRequests, 1);

	Host->RequestActiveTool(TEXT("FrameCues"), CueProvider);
	TestTrue(TEXT("different tool activation applies once"), Host->ApplyPendingSwitchForTests());
	TestEqual(TEXT("second provider descriptors requested exactly once"), CueProvider->DescriptorRequests, 1);
	TestEqual(TEXT("shared flipbook selection survives the tool switch"), Fixture.Model->GetSelectedFlipbookIndex(), 0);
	TestEqual(TEXT("shared frame selection survives the tool switch"), Fixture.Model->GetSelectedFrameIndex(), 3);
	TestEqual(TEXT("Completion survives the tool switch"),
		Fixture.Profile->Flipbooks[0].EditorMeta.CompletionFlags, (1 << 0) | (1 << 5));
	TestTrue(TEXT("read-only diagnostic reports the active tool"),
		Host->BuildDiagnosticString().Contains(TEXT("Tool=FrameCues")));

	Host->Shutdown();
	CleanupConfig(ConfigPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceContextLayoutTest,
	"Paper2DPlus.ProfileWorkspace.DetailsCategoriesPersistExpansionPerToolAndFocusReveals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceContextLayoutTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	const FString ConfigPath = MakeConfigPath(TEXT("ContextLayout"));
	const FName Scope(TEXT("ContextLayout"));
	const FName ToolA(TEXT("Hitboxes"));
	const FName PanelOne(TEXT("Workspace.Hitbox.Details"));
	const FName PanelTwo(TEXT("Workspace.Hitbox.Preview"));
	const FName StateA(TEXT("Paper2DPlus.ProfileToolDetails.ContextLayout.Hitboxes.v1"));
	const FName StateB(TEXT("Paper2DPlus.ProfileToolDetails.ContextLayout.FrameCues.v1"));

	const TSharedRef<SDockTab> OwnerTab = SNew(SDockTab);
	TSharedPtr<SProfileToolPanelHost> Host = MakeHost(OwnerTab, ConfigPath, Scope);
	TSharedPtr<FWorkspaceProvider> ProviderA = MakeShared<FWorkspaceProvider>(
		TArray<FName>({ PanelOne, PanelTwo }));
	TSharedPtr<FWorkspaceProvider> ProviderB = MakeShared<FWorkspaceProvider>(
		TArray<FName>({ PanelTwo, TEXT("Workspace.Cues.Details") }));
	GConfig->SetBool(*StateB.ToString(), *PanelTwo.ToString(), false, ConfigPath);

	Host->RequestActiveTool(ToolA, ProviderA);
	Host->ApplyPendingSwitchForTests();
	TestEqual(TEXT("tool A owns its own expansion-state section"), Host->GetActiveStateSection(), StateA);
	TestEqual(TEXT("provider order becomes Details category order"),
		Host->GetRegisteredPanelIdsForTests(), TArray<FName>({ PanelOne, PanelTwo }));
	TestTrue(TEXT("first Details category starts expanded"), Host->IsSectionExpandedForTests(PanelOne));
	TestTrue(TEXT("second Details category starts expanded"), Host->IsSectionExpandedForTests(PanelTwo));

	TestTrue(TEXT("Details category can collapse"), Host->CollapseSectionForTests(PanelTwo));
	TestFalse(TEXT("collapsed category reports collapsed"), Host->IsSectionExpandedForTests(PanelTwo));
	TestTrue(TEXT("focus expands and reveals the requested category"), Host->ForegroundPanel(PanelTwo));
	TestTrue(TEXT("focused category is expanded"), Host->IsSectionExpandedForTests(PanelTwo));
	TestTrue(TEXT("category collapses again for persistence check"), Host->CollapseSectionForTests(PanelTwo));
	Host->PersistExpansionStateForTests();

	Host->RequestActiveTool(TEXT("FrameCues"), ProviderB);
	TestTrue(TEXT("focus request is queued for the incoming tool even when the old tool reuses the id"),
		Host->ForegroundPanel(PanelTwo));
	Host->ApplyPendingSwitchForTests();
	TestEqual(TEXT("each tool owns its expected expansion-state section"),
		Host->GetActiveStateSection(), StateB);
	TestTrue(TEXT("deferred focus expands the incoming tool's previously collapsed category"),
		Host->IsSectionExpandedForTests(PanelTwo));
	Host->RequestActiveTool(ToolA, ProviderA);
	Host->ApplyPendingSwitchForTests();
	TestEqual(TEXT("returning to tool A restores its state section"), Host->GetActiveStateSection(), StateA);
	TestTrue(TEXT("tool A's first category remains expanded"), Host->IsSectionExpandedForTests(PanelOne));
	TestFalse(TEXT("tool A's saved collapsed state survives another tool"),
		Host->IsSectionExpandedForTests(PanelTwo));
	TestTrue(TEXT("diagnostics report ordered category expansion"),
		Host->GetExpansionStateStringForTests().Contains(TEXT("Workspace.Hitbox.Preview:Collapsed")));

	Host->Shutdown();
	CleanupConfig(ConfigPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceCompletionTest,
	"Paper2DPlus.ProfileWorkspace.CompletionIsFiveSelectedAnimationTasksWithUndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceCompletionTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	if (!TestNotNull(TEXT("GEditor available"), GEditor))
	{
		return false;
	}
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusProfileWorkspaceTest", "ResetCompletion", "Workspace Completion Test Reset"));
	FFixture Fixture = MakeFixture();
	Fixture.Profile->AddToRoot();
	TSharedPtr<SCharacterCompletionPanel> Panel = SNew(SCharacterCompletionPanel).Model(Fixture.Model);

	TestEqual(TEXT("Completion exposes exactly five live tasks"), Panel->GetDisplayedTaskCountForTests(), 5);
	TestEqual(TEXT("Completion owns the expected three model subscriptions"), Panel->GetModelSubscriptionCountForTests(), 3);
	TestTrue(TEXT("All changes the selected animation"), Panel->SetAllCompletedForTests(true));
	TestEqual(TEXT("All writes exactly the five live bits"),
		Fixture.Profile->Flipbooks[0].EditorMeta.CompletionFlags,
		UPaper2DPlusCharacterProfileAsset::LiveTaskBits);
	TestEqual(TEXT("unselected animation remains untouched"),
		Fixture.Profile->Flipbooks[1].EditorMeta.CompletionFlags, 0);

	TestTrue(TEXT("one Undo restores the selected animation's prior flags"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("Undo clears the five bits"), Fixture.Profile->Flipbooks[0].EditorMeta.CompletionFlags, 0);
	TestTrue(TEXT("one Redo reapplies All"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores the live task mask"),
		Fixture.Profile->Flipbooks[0].EditorMeta.CompletionFlags,
		UPaper2DPlusCharacterProfileAsset::LiveTaskBits);

	Fixture.Model->SetSelectedFlipbook(1);
	TestTrue(TEXT("single task changes the newly selected animation"),
		Panel->SetTaskCompletedForTests(2, true));
	TestEqual(TEXT("Timing is the only bit on the second animation"),
		Fixture.Profile->Flipbooks[1].EditorMeta.CompletionFlags, 1 << 2);
	TestEqual(TEXT("first animation retains its completed mask"),
		Fixture.Profile->Flipbooks[0].EditorMeta.CompletionFlags,
		UPaper2DPlusCharacterProfileAsset::LiveTaskBits);
	TestTrue(TEXT("None clears the selected animation"), Panel->SetAllCompletedForTests(false));
	TestEqual(TEXT("None writes exactly zero"), Fixture.Profile->Flipbooks[1].EditorMeta.CompletionFlags, 0);

	Panel->Shutdown();
	TestEqual(TEXT("Completion shutdown removes every model subscription"),
		Panel->GetModelSubscriptionCountForTests(), 0);
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusProfileWorkspaceTest", "EndCompletion", "Workspace Completion Test End"));
	Fixture.Profile->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusAssetProfileCompletionTest,
	"Paper2DPlus.ProfileWorkspace.AssetCompletionCriteriaPersistWithUndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusAssetProfileCompletionTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("GEditor available"), GEditor))
	{
		return false;
	}
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusProfileWorkspaceTest", "ResetAssetCompletion", "Asset Completion Test Reset"));

	UPaper2DPlusCharacterLayerAsset* Layer = NewObject<UPaper2DPlusCharacterLayerAsset>(
		GetTransientPackage(), NAME_None, RF_Transactional);
	UPaper2DPlusEffectProfileAsset* Effect = NewObject<UPaper2DPlusEffectProfileAsset>(
		GetTransientPackage(), NAME_None, RF_Transactional);
	UPaper2DPlusCombatProfileAsset* Combat = NewObject<UPaper2DPlusCombatProfileAsset>(
		GetTransientPackage(), NAME_None, RF_Transactional);
	Layer->AddToRoot();
	Effect->AddToRoot();
	Combat->AddToRoot();

	TSharedPtr<SProfileCompletionPanel> LayerPanel = SNew(SProfileCompletionPanel)
		.Asset(Layer)
		.ProfileKind(EProfileCompletionKind::Layer);
	TSharedPtr<SProfileCompletionPanel> EffectPanel = SNew(SProfileCompletionPanel)
		.Asset(Effect)
		.ProfileKind(EProfileCompletionKind::Effect);
	TSharedPtr<SProfileCompletionPanel> CombatPanel = SNew(SProfileCompletionPanel)
		.Asset(Combat)
		.ProfileKind(EProfileCompletionKind::Combat);

	for (const TSharedPtr<SProfileCompletionPanel>& Panel : { LayerPanel, EffectPanel, CombatPanel })
	{
		TestEqual(TEXT("Every asset profile exposes five criteria"),
			Panel->GetDisplayedCriterionCountForTests(), 5);
	}
	TestEqual(TEXT("Layer criterion 0 is Structure"),
		LayerPanel->GetCriterionLabelForTests(0).ToString(), FString(TEXT("Structure")));
	TestEqual(TEXT("Layer criterion 4 is Publishing"),
		LayerPanel->GetCriterionLabelForTests(4).ToString(), FString(TEXT("Publishing")));
	TestEqual(TEXT("Effect criterion 1 is Classification"),
		EffectPanel->GetCriterionLabelForTests(1).ToString(), FString(TEXT("Classification")));
	TestEqual(TEXT("Effect criterion 3 is Placement"),
		EffectPanel->GetCriterionLabelForTests(3).ToString(), FString(TEXT("Placement")));
	TestEqual(TEXT("Combat criterion 1 is Attack Tuning"),
		CombatPanel->GetCriterionLabelForTests(1).ToString(), FString(TEXT("Attack Tuning")));
	TestEqual(TEXT("Combat criterion 4 is Scenario Testing"),
		CombatPanel->GetCriterionLabelForTests(4).ToString(), FString(TEXT("Scenario Testing")));

	TestTrue(TEXT("Layer All writes its complete mask"), LayerPanel->SetAllCompletedForTests(true));
	TestEqual(TEXT("Layer completion persists on the asset"),
		Layer->EditorCompletionFlags, SProfileCompletionPanel::AllCriteriaMask);
	TestTrue(TEXT("Undo clears Layer completion"), GEditor->UndoTransaction(true));
	TestEqual(TEXT("Layer Undo restores the previous flags"), Layer->EditorCompletionFlags, 0);
	TestTrue(TEXT("Redo restores Layer completion"), GEditor->RedoTransaction());
	TestEqual(TEXT("Layer Redo restores all five flags"),
		Layer->EditorCompletionFlags, SProfileCompletionPanel::AllCriteriaMask);

	TestTrue(TEXT("Effect Placement can be completed independently"),
		EffectPanel->SetCriterionCompletedForTests(3, true));
	TestEqual(TEXT("Effect stores only the Placement bit"), Effect->EditorCompletionFlags, 1 << 3);
	TestTrue(TEXT("Combat Scoring can be completed independently"),
		CombatPanel->SetCriterionCompletedForTests(3, true));
	TestEqual(TEXT("Combat stores only the Scoring bit"), Combat->EditorCompletionFlags, 1 << 3);

	LayerPanel->Shutdown();
	EffectPanel->Shutdown();
	CombatPanel->Shutdown();
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusProfileWorkspaceTest", "EndAssetCompletion", "Asset Completion Test End"));
	Layer->RemoveFromRoot();
	Effect->RemoveFromRoot();
	Combat->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceLayoutFallbackTest,
	"Paper2DPlus.ProfileWorkspace.StaleOrMalformedLayoutFallsBackToV15WithoutDeadTabs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceLayoutFallbackTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	const FString ConfigPath = MakeConfigPath(TEXT("LayoutFallback"));
	const TSharedRef<FTabManager::FLayout> DefaultLayout =
		FCharacterProfileAssetEditorToolkit::CreateDefaultWorkspaceLayout();
	const FString DefaultString = DefaultLayout->ToString();
	TestEqual(TEXT("workspace layout is the current v15 expected-tags layout"),
		DefaultLayout->GetLayoutName(), FName(TEXT("CharacterProfileAssetEditor_Layout_v15_ExpectedTags")));
	for (const FName RequiredTab : {
		FCharacterProfileAssetEditorToolkit::AnimationsTabId,
		FCharacterProfileAssetEditorToolkit::FlipbookListTabId,
		FCharacterProfileAssetEditorToolkit::HitboxEditorTabId,
		FCharacterProfileAssetEditorToolkit::SpriteEditorTabId,
		FCharacterProfileAssetEditorToolkit::FrameTimingTabId,
		FCharacterProfileAssetEditorToolkit::FrameEventsTabId,
		FCharacterProfileAssetEditorToolkit::RootMotionTabId,
		FCharacterProfileAssetEditorToolkit::ContextHostTabId,
		FCharacterProfileAssetEditorToolkit::ExpectedTagsTabId,
		FCharacterProfileAssetEditorToolkit::CompletionTabId,
		FCharacterProfileAssetEditorToolkit::RelatedProfilesTabId,
		FCharacterProfileAssetEditorToolkit::PlaybackQueueTabId })
	{
		TestTrue(*FString::Printf(TEXT("current layout contains %s"), *RequiredTab.ToString()),
			DefaultString.Contains(RequiredTab.ToString()));
	}
	TestFalse(TEXT("v15 contains no dedicated Validation tab"),
		DefaultString.Contains(TEXT("CharacterProfileEditor_Validation")));
	TestFalse(TEXT("v15 contains no retired Overview tab"),
		DefaultString.Contains(TEXT("CharacterProfileEditor_Overview")));
	TestFalse(TEXT("v15 contains no retired standalone Animation Map tab"),
		DefaultString.Contains(TEXT("CharacterProfileEditor_AnimationMap")));

	// Expected Tags is a permanent sibling of Details in the SAME upper-right stack. Details remains
	// foreground so adding the checklist does not steal focus from the active tool's contextual controls.
	{
		const TSharedRef<FJsonObject> Root = DefaultLayout->ToJson();
		const TArray<TSharedPtr<FJsonValue>>& Areas = Root->GetArrayField(TEXT("Areas"));
		if (TestEqual(TEXT("v15 layout has one primary area"), Areas.Num(), 1))
		{
			const TSharedPtr<FJsonObject> PrimaryArea = Areas[0]->AsObject();
			const TArray<TSharedPtr<FJsonValue>>& Columns = PrimaryArea->GetArrayField(TEXT("Nodes"));
			if (TestEqual(TEXT("v15 layout retains the three-column workspace"), Columns.Num(), 3))
			{
				const TSharedPtr<FJsonObject> RightSplitter = Columns[2]->AsObject();
				const TArray<TSharedPtr<FJsonValue>>& RightStacks =
					RightSplitter->GetArrayField(TEXT("Nodes"));
				if (TestEqual(TEXT("right rail retains upper and lower stacks"), RightStacks.Num(), 2))
				{
					const TSharedPtr<FJsonObject> UpperRightStack = RightStacks[0]->AsObject();
					const TArray<TSharedPtr<FJsonValue>>& UpperTabs =
						UpperRightStack->GetArrayField(TEXT("Tabs"));
					if (TestEqual(TEXT("upper-right stack contains only Details and Expected Tags"),
						UpperTabs.Num(), 2))
					{
						TestEqual(TEXT("Details remains first in its stack"),
							UpperTabs[0]->AsObject()->GetStringField(TEXT("TabId")),
							FCharacterProfileAssetEditorToolkit::ContextHostTabId.ToString());
						TestEqual(TEXT("Expected Tags is the Details sibling"),
							UpperTabs[1]->AsObject()->GetStringField(TEXT("TabId")),
							FCharacterProfileAssetEditorToolkit::ExpectedTagsTabId.ToString());
						TestEqual(TEXT("Details opens by default"),
							UpperTabs[0]->AsObject()->GetStringField(TEXT("TabState")),
							FString(TEXT("OpenedTab")));
						TestEqual(TEXT("Expected Tags opens by default"),
							UpperTabs[1]->AsObject()->GetStringField(TEXT("TabState")),
							FString(TEXT("OpenedTab")));
					}
					TestEqual(TEXT("Details stays foreground over Expected Tags"),
						UpperRightStack->GetStringField(TEXT("ForegroundTab")),
						FCharacterProfileAssetEditorToolkit::ContextHostTabId.ToString());
				}
			}
		}
	}

	// Playback Queue must be an OPEN sibling of Completion/Related Profiles in the lower-right stack —
	// the whole point of the move was that it stops hiding behind the closed Navigator.
	{
		const FString QueueTab = FCharacterProfileAssetEditorToolkit::PlaybackQueueTabId.ToString();
		const int32 QueuePosition = DefaultString.Find(QueueTab);
		const int32 RelatedPosition = DefaultString.Find(
			FCharacterProfileAssetEditorToolkit::RelatedProfilesTabId.ToString());
		const int32 CompletionPosition = DefaultString.Find(
			FCharacterProfileAssetEditorToolkit::CompletionTabId.ToString());
		TestTrue(TEXT("Playback Queue sits with Completion and Related Profiles"),
			QueuePosition != INDEX_NONE && RelatedPosition != INDEX_NONE
			&& CompletionPosition != INDEX_NONE && QueuePosition > CompletionPosition);
		const int32 QueueClosedPosition = DefaultString.Find(
			TEXT("\"TabState\": \"ClosedTab\""),
			ESearchCase::CaseSensitive,
			ESearchDir::FromStart,
			QueuePosition);
		const int32 QueueOpenPosition = DefaultString.Find(
			TEXT("\"TabState\": \"OpenedTab\""),
			ESearchCase::CaseSensitive,
			ESearchDir::FromStart,
			QueuePosition);
		TestTrue(TEXT("Playback Queue is opened, not closed, by default"),
			QueueOpenPosition != INDEX_NONE
			&& (QueueClosedPosition == INDEX_NONE || QueueOpenPosition < QueueClosedPosition));
	}
	const int32 NavigatorPosition = DefaultString.Find(
		FCharacterProfileAssetEditorToolkit::FlipbookListTabId.ToString());
	const int32 NavigatorClosedPosition = DefaultString.Find(
		TEXT("\"TabState\": \"ClosedTab\""),
		ESearchCase::CaseSensitive,
		ESearchDir::FromStart,
		NavigatorPosition);
	const int32 FollowingTabPosition = DefaultString.Find(
		TEXT("\"TabId\""),
		ESearchCase::CaseSensitive,
		ESearchDir::FromStart,
		NavigatorPosition + FCharacterProfileAssetEditorToolkit::FlipbookListTabId.ToString().Len());
	TestTrue(TEXT("v15 keeps the dockable Navigator closed by default"),
		NavigatorPosition != INDEX_NONE
		&& NavigatorClosedPosition != INDEX_NONE
		&& (FollowingTabPosition == INDEX_NONE || NavigatorClosedPosition < FollowingTabPosition));

	const TSharedRef<FTabManager::FLayout> StaleV10 =
		FTabManager::NewLayout("CharacterProfileAssetEditor_Layout_v10_Validation")
		->AddArea(FTabManager::NewPrimaryArea()->Split(
			FTabManager::NewStack()->AddTab(TEXT("CharacterProfileEditor_DeadTab"), ETabState::OpenedTab)));
	FLayoutSaveRestore::SaveToConfig(ConfigPath, StaleV10);
	TSharedRef<FTabManager::FLayout> Loaded = FLayoutSaveRestore::LoadFromConfig(ConfigPath, DefaultLayout);
	TestEqual(TEXT("stale v10 key cannot replace the v15 default"),
		Loaded->GetLayoutName(), DefaultLayout->GetLayoutName());
	TestFalse(TEXT("stale dead tab cannot enter v15"),
		Loaded->ToString().Contains(TEXT("CharacterProfileEditor_DeadTab")));

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
	FLayoutSaveRestore::SaveSectionToConfig(
		ConfigPath,
		DefaultLayout->GetLayoutName().ToString(),
		FString(TEXT("this-is-not-a-layout")));
#else
	FLayoutSaveRestore::SaveSectionToConfig(
		ConfigPath,
		DefaultLayout->GetLayoutName().ToString(),
		FText::FromString(TEXT("this-is-not-a-layout")));
#endif
	Loaded = FLayoutSaveRestore::LoadFromConfig(ConfigPath, DefaultLayout);
	TestEqual(TEXT("malformed v15 entry falls back to the canonical v15 default"),
		Loaded->GetLayoutName(), DefaultLayout->GetLayoutName());
	TestTrue(TEXT("malformed fallback still contains Completion"),
		Loaded->ToString().Contains(FCharacterProfileAssetEditorToolkit::CompletionTabId.ToString()));

	CleanupConfig(ConfigPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceNavigatorPreviewIntegrationTest,
	"Paper2DPlus.ProfileWorkspace.NavigatorUsesVirtualizedAnimationPreviews",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceNavigatorPreviewIntegrationTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	FFixture Fixture = MakeFixture();
	TSharedRef<SFlipbookListPanel> Navigator = SNew(SFlipbookListPanel)
		.Model(Fixture.Model);
	const int32 GeneratedRows = Navigator->GenerateNavigatorRowsForTests(
		FVector2D(280.0f, 180.0f));
	TestTrue(TEXT("docked Navigator virtualizes at least one projected animation row"),
		GeneratedRows > 0);
	TestTrue(TEXT("docked Navigator rows receive animation preview widgets"),
		Navigator->GetNavigatorPreviewCountForTests() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceLifecycleTest,
	"Paper2DPlus.ProfileWorkspace.ProviderDestructionAndEditorShutdownLeaveNoDelegatesOrDetailsSections",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	FFixture Fixture = MakeFixture();
	const FString ConfigPath = MakeConfigPath(TEXT("Lifecycle"));
	const TSharedRef<SDockTab> OwnerTab = SNew(SDockTab);
	TSharedPtr<SProfileToolPanelHost> Host = MakeHost(OwnerTab, ConfigPath, TEXT("Lifecycle"));
	TSharedPtr<FWorkspaceProvider> Provider = MakeShared<FWorkspaceProvider>(
		TArray<FName>({ TEXT("Workspace.Lifecycle.Details") }));
	Host->RequestActiveTool(TEXT("LifecycleTool"), Provider);
	Host->ApplyPendingSwitchForTests();
	TestTrue(TEXT("active provider owns one live Details surface"), Host->HasDetailsSectionsForTests());
	TestTrue(TEXT("provider category is expanded"),
		Host->IsSectionExpandedForTests(TEXT("Workspace.Lifecycle.Details")));

	Provider.Reset();
	TestTrue(TEXT("expired weak provider is detected"), Host->PollProviderLivenessForTests());
	TestEqual(TEXT("expired provider enters an explicit state"),
		static_cast<int32>(Host->GetEmptyState()),
		static_cast<int32>(EProfileToolPanelHostEmptyState::ProviderExpired));
	TestFalse(TEXT("provider expiry removes the Details surface"), Host->HasDetailsSectionsForTests());
	TestEqual(TEXT("provider expiry unregisters every Details category"),
		Host->GetRegisteredPanelIdsForTests().Num(), 0);

	TSharedPtr<SCharacterCompletionPanel> Completion = SNew(SCharacterCompletionPanel).Model(Fixture.Model);
	TestEqual(TEXT("Completion starts with three model delegates"),
		Completion->GetModelSubscriptionCountForTests(), 3);
	Completion->Shutdown();
	TestEqual(TEXT("editor shutdown removes Completion delegates"),
		Completion->GetModelSubscriptionCountForTests(), 0);
	Host->Shutdown();
	Host->Shutdown();
	TestFalse(TEXT("idempotent editor shutdown leaves no Details surface"), Host->HasDetailsSectionsForTests());
	TestEqual(TEXT("idempotent editor shutdown leaves no registered Details categories"),
		Host->GetRegisteredPanelIdsForTests().Num(), 0);

	CleanupConfig(ConfigPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceAnimationsSelectionTest,
	"Paper2DPlus.ProfileWorkspace.AnimationsListGridMapShareModelAndScopedPanels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceAnimationsSelectionTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	FFixture Fixture = MakeFixture();
	Fixture.Profile->Flipbooks[0].TransitionData.Transitions.Add(
		FPaper2DPlusMoveTransition(TEXT("Attack")));

	int32 ContextRequests = 0;
	FName LastContextRequest;
	TSharedRef<SAnimationsPanel> Animations = SNew(SAnimationsPanel)
		.Model(Fixture.Model)
		.PersistViewMode(false)
		.OnContextPanelRequested(SAnimationsPanel::FOnContextPanelRequested::CreateLambda(
			[&ContextRequests, &LastContextRequest](FName PanelId)
			{
				++ContextRequests;
				LastContextRequest = PanelId;
			}));
	TSharedRef<SProfileDetailsPanel> Details = SNew(SProfileDetailsPanel)
		.Model(Fixture.Model)
		.PaneMode(EProfileDetailsPaneMode::FlipbookDetails);
	TSharedRef<SProfileDetailsPanel> Transitions = SNew(SProfileDetailsPanel)
		.Model(Fixture.Model)
		.PaneMode(EProfileDetailsPaneMode::Transitions);
	TSharedRef<SProfileDetailsPanel> Tags = SNew(SProfileDetailsPanel)
		.Model(Fixture.Model)
		.PaneMode(EProfileDetailsPaneMode::Tags);

	auto SelectInCurrentView = [this, &Fixture, &Details, &Transitions, &Tags](int32 Index)
	{
		const int32 DetailsBefore = Details->GetSelectionRefreshCountForTests();
		const int32 TransitionsBefore = Transitions->GetSelectionRefreshCountForTests();
		const int32 TagsBefore = Tags->GetSelectionRefreshCountForTests();
		Fixture.Model->SetSelectedFlipbook(Index);
		TestEqual(TEXT("Details observes the shared move selection exactly once"),
			Details->GetSelectionRefreshCountForTests(), DetailsBefore + 1);
		TestEqual(TEXT("Transitions observes the shared move selection exactly once"),
			Transitions->GetSelectionRefreshCountForTests(), TransitionsBefore + 1);
		TestEqual(TEXT("Tags observes the shared move selection exactly once"),
			Tags->GetSelectionRefreshCountForTests(), TagsBefore + 1);
		TestEqual(TEXT("all views write the one model selection"),
			Fixture.Model->GetSelectedFlipbookIndex(), Index);
	};

	Animations->SetViewMode(EAnimationsViewMode::Grid);
	SelectInCurrentView(1);
	Animations->SetViewMode(EAnimationsViewMode::List);
	SelectInCurrentView(0);
	Animations->SetViewMode(EAnimationsViewMode::Map);
	SelectInCurrentView(1);
	TestEqual(TEXT("view switching itself never changes selection"),
		Fixture.Model->GetSelectedFlipbookIndex(), 1);

	// A Map edge uses the established ordering: select From through the same model, then set its
	// stable value key. Only the Transitions panel observes the edge channel.
	SelectInCurrentView(0);
	const int32 TransitionEdgeRefreshesBefore =
		Transitions->GetTransitionSelectionRefreshCountForTests();
	Fixture.Model->SetSelectedTransition(TEXT("Idle"), TEXT("Attack"));
	TestTrue(TEXT("Map edge key is shared model state"), Fixture.Model->HasSelectedTransition());
	TestEqual(TEXT("Transitions observes the edge exactly once"),
		Transitions->GetTransitionSelectionRefreshCountForTests(), TransitionEdgeRefreshesBefore + 1);
	TestEqual(TEXT("Details has no irrelevant edge subscription"),
		Details->GetTransitionSelectionRefreshCountForTests(), 0);
	TestEqual(TEXT("Tags has no irrelevant edge subscription"),
		Tags->GetTransitionSelectionRefreshCountForTests(), 0);
	TestEqual(TEXT("edge intent requests one contextual foreground"), ContextRequests, 1);
	TestEqual(TEXT("edge foreground targets Transitions"),
		LastContextRequest, FAnimationsContextPanelProvider::TransitionsPanelId);

	// Clearing the edge as part of ordinary move selection is not a request to steal the user's chosen
	// contextual category.
	SelectInCurrentView(1);
	TestEqual(TEXT("ordinary move selection never requests a new contextual foreground"),
		ContextRequests, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceAnimationsContextLifecycleTest,
	"Paper2DPlus.ProfileWorkspace.AnimationsDetailsCategoriesCollapseWithoutLosingCurrentState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceAnimationsContextLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	FFixture Fixture = MakeFixture();
	Fixture.Profile->Flipbooks[0].TransitionData.Transitions.Add(
		FPaper2DPlusMoveTransition(TEXT("Attack")));
	Fixture.Model->SetSelectedFlipbook(0);
	Fixture.Model->SetSelectedTransition(TEXT("Idle"), TEXT("Attack"));

	const FString ConfigPath = MakeConfigPath(TEXT("AnimationsContext"));
	const TSharedRef<SDockTab> OwnerTab = SNew(SDockTab);
	TSharedPtr<SProfileToolPanelHost> Host = MakeHost(
		OwnerTab, ConfigPath, TEXT("AnimationsContext"));
	TSharedPtr<FAnimationsContextPanelProvider> Provider =
		MakeShared<FAnimationsContextPanelProvider>(Fixture.Model);
	Host->RequestActiveTool(TEXT("Animations"), Provider);
	TestTrue(TEXT("Animations provider applies"), Host->ApplyPendingSwitchForTests());

	const TArray<FName> ExpectedPanelIds = {
		FAnimationsContextPanelProvider::DetailsPanelId,
		FAnimationsContextPanelProvider::TransitionsPanelId,
		FAnimationsContextPanelProvider::TagsPanelId };
	TestEqual(TEXT("Animations exposes exactly three focused contextual panels"),
		Host->GetRegisteredPanelIdsForTests(), ExpectedPanelIds);
	for (const FName PanelId : ExpectedPanelIds)
	{
		TestTrue(*FString::Printf(TEXT("%s starts expanded"), *PanelId.ToString()),
			Host->IsSectionExpandedForTests(PanelId));
		TestTrue(*FString::Printf(TEXT("%s collapses"), *PanelId.ToString()),
			Host->CollapseSectionForTests(PanelId));
		TestFalse(TEXT("collapsed category reports collapsed"), Host->IsSectionExpandedForTests(PanelId));
		TestTrue(*FString::Printf(TEXT("%s expands when focused"), *PanelId.ToString()),
			Host->ForegroundPanel(PanelId));
		TestEqual(TEXT("collapse/expand retains the selected move"),
			Fixture.Model->GetSelectedFlipbookIndex(), 0);
		TestTrue(TEXT("collapse/expand retains the stable selected edge"),
			Fixture.Model->HasSelectedTransition());
	}

	// Descriptor factories still construct focused widgets from CURRENT model state. In particular, a
	// Transitions view resolves the live edge rather than retaining a stale row identity.
	TArray<FProfileToolPanelDescriptor> Descriptors;
	Provider->GetContextualPanels(Descriptors);
	TestEqual(TEXT("provider returns three descriptors"), Descriptors.Num(), 3);
	for (const FProfileToolPanelDescriptor& Descriptor : Descriptors)
	{
		TSharedPtr<SWidget> Widget = Descriptor.TryCreateWidget();
		if (!TestTrue(TEXT("descriptor reconstructs a widget"), Widget.IsValid()))
		{
			continue;
		}
		TSharedPtr<SProfileDetailsPanel> FocusedPanel =
			StaticCastSharedPtr<SProfileDetailsPanel>(Widget);
		if (Descriptor.PanelId == FAnimationsContextPanelProvider::DetailsPanelId)
		{
			TestEqual(TEXT("Details descriptor has focused mode"),
				static_cast<int32>(FocusedPanel->GetPaneModeForTests()),
				static_cast<int32>(EProfileDetailsPaneMode::FlipbookDetails));
		}
		else if (Descriptor.PanelId == FAnimationsContextPanelProvider::TransitionsPanelId)
		{
			TestEqual(TEXT("Transitions descriptor has focused mode"),
				static_cast<int32>(FocusedPanel->GetPaneModeForTests()),
				static_cast<int32>(EProfileDetailsPaneMode::Transitions));
			TestTrue(TEXT("constructed Transitions panel resolves the current edge"),
				FocusedPanel->IsShowingResolvedEdgeForTests());
		}
		else if (Descriptor.PanelId == FAnimationsContextPanelProvider::TagsPanelId)
		{
			TestEqual(TEXT("Tags descriptor has focused mode"),
				static_cast<int32>(FocusedPanel->GetPaneModeForTests()),
				static_cast<int32>(EProfileDetailsPaneMode::Tags));
		}
	}

	Host->Shutdown();
	CleanupConfig(ConfigPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceHitboxContextLifecycleTest,
	"Paper2DPlus.ProfileWorkspace.HitboxDetailsCategoriesRetainLiveCanvasAndOneTransactionOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceHitboxContextLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	if (!TestNotNull(TEXT("GEditor available"), GEditor))
	{
		return false;
	}
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusProfileWorkspaceTest", "ResetHitboxContext", "Hitbox Context Test Reset"));

	FFixture Fixture = MakeFixture();
	Fixture.Profile->AddToRoot();
	FHitboxData FirstBox;
	FirstBox.Type = EHitboxType::Attack;
	FirstBox.Damage = 10;
	FHitboxData SecondBox;
	SecondBox.Type = EHitboxType::Hurtbox;
	SecondBox.Damage = 20;
	Fixture.Profile->Flipbooks[0].CombatData.Frames[3].Hitboxes = { FirstBox, SecondBox };

	TSharedPtr<SHitboxEditorPanel> Panel = SNew(SHitboxEditorPanel)
		.Model(Fixture.Model)
		.HostContract(FProfileToolPanelHostContract::External());
	const FString ConfigPath = MakeConfigPath(TEXT("HitboxContext"));
	const TSharedRef<SDockTab> OwnerTab = SNew(SDockTab);
	TSharedPtr<SProfileToolPanelHost> Host = MakeHost(
		OwnerTab, ConfigPath, TEXT("HitboxContext"));
	Host->RequestActiveTool(
		TEXT("Hitboxes"),
		StaticCastSharedPtr<IProfileToolPanelProvider>(Panel));
	TestTrue(TEXT("Hitbox provider applies to the contextual workspace"),
		Host->ApplyPendingSwitchForTests());

	const TArray<FName> ExpectedPanelIds = {
		SHitboxEditorPanel::HitboxesPanelId,
		SHitboxEditorPanel::PropertiesPanelId,
		SHitboxEditorPanel::FrameOperationsPanelId };
	TestEqual(TEXT("Hitboxes exports exactly three independent contextual panels"),
		Host->GetRegisteredPanelIdsForTests(), ExpectedPanelIds);
	for (const FName PanelId : ExpectedPanelIds)
	{
		TestTrue(*FString::Printf(TEXT("%s starts expanded"), *PanelId.ToString()),
			Host->IsSectionExpandedForTests(PanelId));
		TestEqual(TEXT("Details host constructs each category body once"),
			Panel->GetContextPanelBuildCountForTests(PanelId), 1);
		TestEqual(TEXT("Hitbox category does not add a second scroll owner"),
			Host->CountSectionDescendantWidgetsForTests(PanelId, TEXT("SScrollBox")), 0);
	}

	Panel->SetCanvasSelectionForTests(EHitboxSelectionType::Hitbox, 0);
	TestEqual(TEXT("canvas owns the selected box before panel changes"),
		Panel->GetCanvasPrimarySelectionForTests(), 0);
	TestTrue(TEXT("one canvas gesture opens one transaction owner"),
		Panel->BeginTransactionForTests(FText::FromString(TEXT("Move Hitbox"))));
	TestFalse(TEXT("a second begin cannot create a competing transaction"),
		Panel->BeginTransactionForTests(FText::FromString(TEXT("Duplicate Move"))));
	TestEqual(TEXT("only one transaction begins"),
		Panel->GetTransactionBeginCountForTests(), 1);

	for (const FName PanelId : ExpectedPanelIds)
	{
		TestTrue(*FString::Printf(TEXT("%s collapses independently"), *PanelId.ToString()),
			Host->CollapseSectionForTests(PanelId));
		TestFalse(TEXT("collapsed contextual category reports collapsed"),
			Host->IsSectionExpandedForTests(PanelId));
		TestEqual(TEXT("collapsing a contextual category preserves the canvas selection"),
			Panel->GetCanvasPrimarySelectionForTests(), 0);
		TestTrue(*FString::Printf(TEXT("%s expands when focused"), *PanelId.ToString()),
			Host->ForegroundPanel(PanelId));
		TestEqual(TEXT("collapse/expand retains the existing category body"),
			Panel->GetContextPanelBuildCountForTests(PanelId), 1);
		TestEqual(TEXT("expand preserves the one controller's selection"),
			Panel->GetCanvasPrimarySelectionForTests(), 0);
		TestTrue(TEXT("category expansion cannot close or replace the gesture owner"),
			Panel->HasActiveTransactionForTests());
	}

	TestTrue(TEXT("Properties collapses before the live selection changes"),
		Host->CollapseSectionForTests(SHitboxEditorPanel::PropertiesPanelId));
	Panel->SetCanvasSelectionForTests(EHitboxSelectionType::Hitbox, 1);
	TestTrue(TEXT("Properties expands after the selection change"),
		Host->ForegroundPanel(SHitboxEditorPanel::PropertiesPanelId));
	TestEqual(TEXT("live Properties resolves the current selection type"),
		static_cast<int32>(Panel->GetLastPropertiesSelectionTypeForTests()),
		static_cast<int32>(EHitboxSelectionType::Hitbox));
	TestEqual(TEXT("live Properties resolves the current index"),
		Panel->GetLastPropertiesSelectionIndexForTests(), 1);
	TestEqual(TEXT("Properties remains the one body owned by the active controller"),
		Panel->GetContextPanelBuildCountForTests(SHitboxEditorPanel::PropertiesPanelId), 1);

	Panel->HandleHostDeactivated();
	Panel->HandleHostDeactivated();
	TestFalse(TEXT("tool switch leaves no open hitbox transaction"),
		Panel->HasActiveTransactionForTests());
	TestEqual(TEXT("tool switch commits the gesture exactly once"),
		Panel->GetTransactionEndCountForTests(), 1);
	TestEqual(TEXT("tool switch retains the canvas selection for return"),
		Panel->GetCanvasPrimarySelectionForTests(), 1);

	Host->Shutdown();
	CleanupConfig(ConfigPath);
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusProfileWorkspaceTest", "EndHitboxContext", "Hitbox Context Test End"));
	Fixture.Profile->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceSpriteContextLifecycleTest,
	"Paper2DPlus.ProfileWorkspace.SpriteDetailsCategoriesRetainLivePreviewAndExposeSkinsMixer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceSpriteContextLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	FFixture Fixture = MakeFixture();
	for (FFlipbookProfileEntry& Animation : Fixture.Profile->Flipbooks)
	{
		Animation.CombatData.FrameExtractionInfo.SetNum(6);
	}
	Fixture.Model->SetSelectedFrame(3);
	TSharedPtr<SSpriteEditorPanel> Panel = SNew(SSpriteEditorPanel)
		.Model(Fixture.Model)
		.HostContract(FProfileToolPanelHostContract::External());
	const FString ConfigPath = MakeConfigPath(TEXT("SpriteContext"));
	const TSharedRef<SDockTab> OwnerTab = SNew(SDockTab);
	TSharedPtr<SProfileToolPanelHost> Host = MakeHost(
		OwnerTab, ConfigPath, TEXT("SpriteContext"));
	Host->RequestActiveTool(
		TEXT("Sprite"),
		StaticCastSharedPtr<IProfileToolPanelProvider>(Panel));
	TestTrue(TEXT("Sprite provider applies to the contextual workspace"),
		Host->ApplyPendingSwitchForTests());

	// Clipboard is no longer its own category: Copy/Paste act on the same offset the section
	// already edits, so they live under Reset Offset inside Offset & Nudge.
	const TArray<FName> ExpectedPanelIds = {
		SSpriteEditorPanel::OffsetNudgePanelId,
		SSpriteEditorPanel::OnionSkinsPanelId,
		SSpriteEditorPanel::BatchPanelId };
	TestEqual(TEXT("Sprite exposes exactly three focused contextual panels"),
		Host->GetRegisteredPanelIdsForTests(), ExpectedPanelIds);
	for (const FName PanelId : ExpectedPanelIds)
	{
		TestTrue(*FString::Printf(TEXT("%s starts expanded"), *PanelId.ToString()),
			Host->IsSectionExpandedForTests(PanelId));
		TestEqual(TEXT("initial Sprite Details host builds each category body once"),
			Panel->GetContextPanelBuildCountForTests(PanelId), 1);
	}
	TestEqual(TEXT("Onion & Skins directly exposes three channel sliders"),
		Host->CountSectionDescendantWidgetsForTests(
			SSpriteEditorPanel::OnionSkinsPanelId, TEXT("SSlider")),
		3);
	TestEqual(TEXT("Onion & Skins Details category is not another dropdown"),
		Host->CountSectionDescendantWidgetsForTests(
			SSpriteEditorPanel::OnionSkinsPanelId, TEXT("SComboButton")),
		0);
	TestEqual(TEXT("Onion & Skins category does not add a second scroll owner"),
		Host->CountSectionDescendantWidgetsForTests(
			SSpriteEditorPanel::OnionSkinsPanelId, TEXT("SScrollBox")),
		0);

	Panel->SetOnionSkinsStateForTests(
		true, true, 2, 0.55f, 0, 1, 0.65f);
	TestTrue(TEXT("onion settings drive the central preview"),
		Panel->IsCanvasShowingOnionForTests());
	TestTrue(TEXT("reference settings drive the central preview"),
		Panel->IsCanvasShowingReferenceForTests());
	TestTrue(TEXT("Onion & Reference collapses independently"),
		Host->CollapseSectionForTests(SSpriteEditorPanel::OnionSkinsPanelId));
	TestTrue(TEXT("central preview keeps onion state while its controls are collapsed"),
		Panel->IsCanvasShowingOnionForTests());
	TestTrue(TEXT("central preview keeps reference state while its controls are collapsed"),
		Panel->IsCanvasShowingReferenceForTests());
	TestTrue(TEXT("Onion & Reference expands from the same controller"),
		Host->ForegroundPanel(SSpriteEditorPanel::OnionSkinsPanelId));
	TestEqual(TEXT("expanded Onion & Reference retains its one mixer body"),
		Panel->GetContextPanelBuildCountForTests(SSpriteEditorPanel::OnionSkinsPanelId), 1);
	TestEqual(TEXT("category expansion preserves selected animation"),
		Fixture.Model->GetSelectedFlipbookIndex(), 0);
	TestEqual(TEXT("category expansion preserves selected frame"),
		Fixture.Model->GetSelectedFrameIndex(), 3);

	TestTrue(TEXT("Offset & Nudge collapses before frame navigation"),
		Host->CollapseSectionForTests(SSpriteEditorPanel::OffsetNudgePanelId));
	Fixture.Model->SetSelectedFrame(4);
	TestTrue(TEXT("Offset & Nudge expands after frame navigation"),
		Host->ForegroundPanel(SSpriteEditorPanel::OffsetNudgePanelId));
	TestEqual(TEXT("live Sprite controller follows the current frame"),
		Fixture.Model->GetSelectedFrameIndex(), 4);
	TestEqual(TEXT("Offset & Nudge remains one live category body"),
		Panel->GetContextPanelBuildCountForTests(SSpriteEditorPanel::OffsetNudgePanelId), 1);

	for (const FName PanelId : {
		SSpriteEditorPanel::OnionSkinsPanelId,
		SSpriteEditorPanel::BatchPanelId })
	{
		TestTrue(TEXT("secondary Sprite category collapses"), Host->CollapseSectionForTests(PanelId));
		TestTrue(TEXT("secondary Sprite category expands"), Host->ForegroundPanel(PanelId));
		TestEqual(TEXT("secondary Sprite category retains its view"),
			Panel->GetContextPanelBuildCountForTests(PanelId), 1);
		TestEqual(TEXT("secondary category expansion preserves live frame"),
			Fixture.Model->GetSelectedFrameIndex(), 4);
	}

	Host->Shutdown();
	CleanupConfig(ConfigPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceTimingContextLifecycleTest,
	"Paper2DPlus.ProfileWorkspace.TimingDetailsCategoriesRetainLiveFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceTimingContextLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	FFixture Fixture = MakeFixture();
	TSharedPtr<SFrameTimingEditor> Editor = SNew(SFrameTimingEditor)
		.Asset(Fixture.Profile)
		.Model(Fixture.Model)
		.HostContract(FProfileToolPanelHostContract::External());
	Editor->HandleHostActivated();
	const FString ConfigPath = MakeConfigPath(TEXT("TimingContext"));
	const TSharedRef<SDockTab> OwnerTab = SNew(SDockTab);
	TSharedPtr<SProfileToolPanelHost> Host = MakeHost(
		OwnerTab, ConfigPath, TEXT("TimingContext"));
	Host->RequestActiveTool(
		TEXT("Timing"),
		StaticCastSharedPtr<IProfileToolPanelProvider>(Editor));
	TestTrue(TEXT("Timing provider applies to the contextual workspace"),
		Host->ApplyPendingSwitchForTests());

	const TArray<FName> ExpectedPanelIds = {
		SFrameTimingEditor::TimingPanelId,
		SFrameTimingEditor::SelectionPanelId,
		SFrameTimingEditor::BatchPanelId };
	TestEqual(TEXT("Timing exposes exactly three focused contextual panels"),
		Host->GetRegisteredPanelIdsForTests(), ExpectedPanelIds);
	for (const FName PanelId : ExpectedPanelIds)
	{
		TestTrue(*FString::Printf(TEXT("%s starts expanded"), *PanelId.ToString()),
			Host->IsSectionExpandedForTests(PanelId));
		TestEqual(TEXT("initial Timing Details host builds each category body once"),
			Editor->GetContextPanelBuildCountForTests(PanelId), 1);
	}

	TestTrue(TEXT("Selection category collapses independently"),
		Host->CollapseSectionForTests(SFrameTimingEditor::SelectionPanelId));
	Fixture.Model->SetSelectedFrame(4);
	TestTrue(TEXT("central timing state follows model navigation while Selection is collapsed"),
		Editor->GetSelectedFrameIndexForTests() == 4);
	TestTrue(TEXT("Selection category expands"),
		Host->ForegroundPanel(SFrameTimingEditor::SelectionPanelId));
	TestEqual(TEXT("Selection remains one live category body"),
		Editor->GetContextPanelBuildCountForTests(SFrameTimingEditor::SelectionPanelId), 1);

	for (const FName PanelId : {
		SFrameTimingEditor::TimingPanelId,
		SFrameTimingEditor::BatchPanelId })
	{
		TestTrue(TEXT("secondary Timing category collapses"), Host->CollapseSectionForTests(PanelId));
		TestTrue(TEXT("secondary Timing category expands"), Host->ForegroundPanel(PanelId));
		TestEqual(TEXT("secondary Timing category retains its view"),
			Editor->GetContextPanelBuildCountForTests(PanelId), 1);
		TestEqual(TEXT("secondary category expansion preserves live frame"),
			Fixture.Model->GetSelectedFrameIndex(), 4);
	}

	Editor->HandleHostDeactivated();
	Host->Shutdown();
	CleanupConfig(ConfigPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceRootMotionContextLifecycleTest,
	"Paper2DPlus.ProfileWorkspace.RootMotionDetailsCategoriesRetainLiveFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceRootMotionContextLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	FFixture Fixture = MakeFixture();
	TSharedPtr<SRootMotionEditor> Editor = SNew(SRootMotionEditor)
		.Asset(Fixture.Profile)
		.Model(Fixture.Model)
		.HostContract(FProfileToolPanelHostContract::External());
	Editor->HandleHostActivated();
	const FString ConfigPath = MakeConfigPath(TEXT("RootMotionContext"));
	const TSharedRef<SDockTab> OwnerTab = SNew(SDockTab);
	TSharedPtr<SProfileToolPanelHost> Host = MakeHost(
		OwnerTab, ConfigPath, TEXT("RootMotionContext"));
	Host->RequestActiveTool(
		TEXT("RootMotion"),
		StaticCastSharedPtr<IProfileToolPanelProvider>(Editor));
	TestTrue(TEXT("Root Motion provider applies to the contextual workspace"),
		Host->ApplyPendingSwitchForTests());

	const TArray<FName> ExpectedPanelIds = {
		SRootMotionEditor::PositionPanelId,
		SRootMotionEditor::OnionSkinsPanelId,
		SRootMotionEditor::BatchPanelId };
	TestEqual(TEXT("Root Motion exposes exactly three focused contextual panels"),
		Host->GetRegisteredPanelIdsForTests(), ExpectedPanelIds);
	for (const FName PanelId : ExpectedPanelIds)
	{
		TestTrue(*FString::Printf(TEXT("%s starts expanded"), *PanelId.ToString()),
			Host->IsSectionExpandedForTests(PanelId));
		TestEqual(TEXT("initial Root Motion Details host builds each category body once"),
			Editor->GetContextPanelBuildCountForTests(PanelId), 1);
		TestEqual(TEXT("Root Motion category does not add a second scroll owner"),
			Host->CountSectionDescendantWidgetsForTests(PanelId, TEXT("SScrollBox")), 0);
	}

	Editor->SetPathSkinStateForTests(true, true, 2, 0.55f);
	TestTrue(TEXT("Path & Skins collapses independently"),
		Host->CollapseSectionForTests(SRootMotionEditor::OnionSkinsPanelId));
	TestTrue(TEXT("central preview keeps Onion while controls are collapsed"),
		Editor->IsCanvasShowingOnionForTests());
	TestTrue(TEXT("central preview keeps Forward while controls are collapsed"),
		Editor->IsCanvasShowingForwardOnionForTests());
	TestTrue(TEXT("Path & Skins expands over the same controller"),
		Host->ForegroundPanel(SRootMotionEditor::OnionSkinsPanelId));
	TestEqual(TEXT("expanded Path & Skins retains its live view"),
		Editor->GetContextPanelBuildCountForTests(SRootMotionEditor::OnionSkinsPanelId), 1);

	TestTrue(TEXT("Position collapses before frame navigation"),
		Host->CollapseSectionForTests(SRootMotionEditor::PositionPanelId));
	Fixture.Model->SetSelectedFrame(4);
	TestTrue(TEXT("central Root Motion state follows model navigation while Position is collapsed"),
		Editor->GetSelectedFrameIndexForTests() == 4);
	TestTrue(TEXT("Position expands"),
		Host->ForegroundPanel(SRootMotionEditor::PositionPanelId));
	TestEqual(TEXT("Position remains one live category body"),
		Editor->GetContextPanelBuildCountForTests(SRootMotionEditor::PositionPanelId), 1);

	TestTrue(TEXT("Batch collapses independently"),
		Host->CollapseSectionForTests(SRootMotionEditor::BatchPanelId));
	TestTrue(TEXT("Batch expands independently"),
		Host->ForegroundPanel(SRootMotionEditor::BatchPanelId));
	TestEqual(TEXT("expanded Batch retains its live view"),
		Editor->GetContextPanelBuildCountForTests(SRootMotionEditor::BatchPanelId), 1);
	TestEqual(TEXT("category expansion preserves selected animation"),
		Fixture.Model->GetSelectedFlipbookIndex(), 0);
	TestEqual(TEXT("category expansion preserves selected frame"),
		Fixture.Model->GetSelectedFrameIndex(), 4);

	Editor->HandleHostDeactivated();
	Host->Shutdown();
	CleanupConfig(ConfigPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceFrameCueContextLifecycleTest,
	"Paper2DPlus.ProfileWorkspace.FrameCueDetailsCategoriesRetainStableCue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceFrameCueContextLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	FFixture Fixture = MakeFixture();
	UPaper2DPlusEditorTestMomentCue* SelectedCue = NewObject<UPaper2DPlusEditorTestMomentCue>(
		Fixture.Profile, NAME_None, RF_Transactional);
	SelectedCue->DebugName = TEXT("StableSelectedCue");
	SelectedCue->TriggerFrame = 2;
	Fixture.Profile->Flipbooks[0].FrameEventData.FrameCues.Add(SelectedCue);

	TSharedPtr<SFrameEventEditor> Editor = SNew(SFrameEventEditor)
		.Model(Fixture.Model)
		.HostContract(FProfileToolPanelHostContract::External());
	Editor->HandleHostActivated();
	Editor->SelectCueForTests(0);
	const FPaper2DPlusFrameCuePreviewHost* InitialPreviewHost = Editor->GetPreviewHostForTests();

	const FString ConfigPath = MakeConfigPath(TEXT("FrameCueContext"));
	const TSharedRef<SDockTab> OwnerTab = SNew(SDockTab);
	TSharedPtr<SProfileToolPanelHost> Host = MakeHost(
		OwnerTab, ConfigPath, TEXT("FrameCueContext"));
	Host->RequestActiveTool(
		TEXT("FrameCues"),
		StaticCastSharedPtr<IProfileToolPanelProvider>(Editor));
	TestTrue(TEXT("Frame Cue provider applies to the contextual workspace"),
		Host->ApplyPendingSwitchForTests());

	const TArray<FName> ExpectedPanelIds = {
		SFrameEventEditor::DetailsPanelId,
		SFrameEventEditor::PreviewPanelId };
	TestEqual(TEXT("Frame Cues exposes only selection-driven Details and Preview"),
		Host->GetRegisteredPanelIdsForTests(), ExpectedPanelIds);
	for (const FName PanelId : ExpectedPanelIds)
	{
		TestTrue(*FString::Printf(TEXT("%s starts expanded"), *PanelId.ToString()),
			Host->IsSectionExpandedForTests(PanelId));
		TestEqual(TEXT("initial Frame Cue contextual host builds each category body once"),
			Editor->GetContextPanelBuildCountForTests(PanelId), 1);
	}
	TestTrue(TEXT("all panels share the controller's one preview host"),
		Editor->GetPreviewHostForTests() == InitialPreviewHost);

	TestTrue(TEXT("Details collapses independently"),
		Host->CollapseSectionForTests(SFrameEventEditor::DetailsPanelId));
	Fixture.Model->SetSelectedFrame(4);
	TestEqual(TEXT("central cue timeline follows model navigation while Details is collapsed"),
		Editor->GetSelectedFrameIndexForTests(), 4);
	TestTrue(TEXT("Details expands"),
		Host->ForegroundPanel(SFrameEventEditor::DetailsPanelId));
	TestEqual(TEXT("Details remains one live category body"),
		Editor->GetContextPanelBuildCountForTests(SFrameEventEditor::DetailsPanelId), 1);
	TestTrue(TEXT("expanded Details resolves the same stable Cue"),
		Editor->GetContextPanelResolvedCueForTests(SFrameEventEditor::DetailsPanelId) == SelectedCue);

	TestTrue(TEXT("Details collapses before an out-of-band structural rebuild"),
		Host->CollapseSectionForTests(SFrameEventEditor::DetailsPanelId));
	UPaper2DPlusEditorTestMomentCue* InsertedCue = NewObject<UPaper2DPlusEditorTestMomentCue>(
		Fixture.Profile, NAME_None, RF_Transactional);
	InsertedCue->DebugName = TEXT("InsertedBeforeSelection");
	Fixture.Profile->Flipbooks[0].FrameEventData.FrameCues.Insert(InsertedCue, 0);
	Fixture.Model->NotifyAssetDataChanged();
	TestTrue(TEXT("selection follows UObject identity after array reindex"),
		Editor->GetSelectedCueForTests() == SelectedCue);
	TestEqual(TEXT("selection view index reconciles after array reindex"),
		Editor->GetSelectedCueIndexForTests(), 1);
	TestTrue(TEXT("Details expands after structural rebuild"),
		Host->ForegroundPanel(SFrameEventEditor::DetailsPanelId));
	TestTrue(TEXT("expanded Details still resolves the stable selected Cue"),
		Editor->GetContextPanelResolvedCueForTests(SFrameEventEditor::DetailsPanelId) == SelectedCue);

	UPaper2DPlusFrameCuePreviewContext* PreviewContext = Editor->GetPreviewContextForTests();
	Editor->BeginTimelineInteractionForTests(/*bResize=*/ false);
	TestTrue(TEXT("timeline gesture is live before tool switch"),
		Editor->HasTimelineInteractionForTests());
	TestTrue(TEXT("timeline gesture owns one transaction before tool switch"), Editor->HasActiveTransaction());
	if (TestNotNull(TEXT("shared preview context exists"), PreviewContext))
	{
		// Drag start intentionally resets the old inspection session. Create one synthetic adapter-owned
		// proxy after that boundary so deactivation proves it clears work produced during the gesture.
		PreviewContext->ShowShape(
			FVector2D::ZeroVector, FVector2D(16.0f, 16.0f), FLinearColor::Green, 10.0f);
	}
	TestTrue(TEXT("preview host owns a resource before tool switch"),
		Editor->GetPreviewResourceCountForTests() > 0);
	Editor->HandleHostDeactivated();
	TestFalse(TEXT("tool switch settles the one gesture transaction"), Editor->HasActiveTransaction());
	TestFalse(TEXT("tool switch clears the timeline gesture"),
		Editor->HasTimelineInteractionForTests());
	TestEqual(TEXT("tool switch clears every preview resource"),
		Editor->GetPreviewResourceCountForTests(), 0);
	TestEqual(TEXT("tool switch leaves no active Cue State"),
		Editor->GetActiveRangeCountForTests(), 0);

	Host->Shutdown();
	CleanupConfig(ConfigPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceRetiredFrameCuePanelsLayoutFallbackTest,
	"Paper2DPlus.ProfileWorkspace.RetiredFrameCuePanelsStateCannotCreateDetailsCategories",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceRetiredFrameCuePanelsLayoutFallbackTest::RunTest(
	const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	const FString ConfigPath = MakeConfigPath(TEXT("RetiredFrameCueCurves"));
	const FName Scope(TEXT("RetiredFrameCueCurves"));
	const FName ToolId(TEXT("FrameCues"));
	const FName RetiredCurvesPanel(TEXT("Paper2DPlus.FrameCues.Curves"));
	const FName RetiredCuesPanel = SFrameEventEditor::CuesPanelId;
	const FName StateSection(TEXT(
		"Paper2DPlus.ProfileToolDetails.RetiredFrameCueCurves.FrameCues.v1"));
	GConfig->SetBool(
		*StateSection.ToString(),
		*RetiredCurvesPanel.ToString(),
		false,
		ConfigPath);
	GConfig->SetBool(
		*StateSection.ToString(),
		*RetiredCuesPanel.ToString(),
		false,
		ConfigPath);

	const TSharedRef<SDockTab> OwnerTab = SNew(SDockTab);
	TSharedPtr<SProfileToolPanelHost> Host = MakeHost(OwnerTab, ConfigPath, Scope);
	const TArray<FName> CurrentPanels = {
		SFrameEventEditor::DetailsPanelId,
		SFrameEventEditor::PreviewPanelId };
	TSharedPtr<FWorkspaceProvider> CurrentProvider =
		MakeShared<FWorkspaceProvider>(CurrentPanels);
	Host->RequestActiveTool(ToolId, CurrentProvider);
	TestTrue(TEXT("current Frame Cue provider activates"), Host->ApplyPendingSwitchForTests());
	TestEqual(TEXT("Frame Cues restores its stable expansion-state section"),
		Host->GetActiveStateSection(), StateSection);
	TestEqual(TEXT("only the current two contextual categories are registered"),
		Host->GetRegisteredPanelIdsForTests(), CurrentPanels);
	TestFalse(TEXT("retired Curves state never creates a category"),
		Host->IsSectionExpandedForTests(RetiredCurvesPanel));
	TestFalse(TEXT("reported category state contains no retired Curves entry"),
		Host->GetExpansionStateStringForTests().Contains(RetiredCurvesPanel.ToString()));
	TestFalse(TEXT("retired indexed Cues state never creates a category"),
		Host->IsSectionExpandedForTests(RetiredCuesPanel));
	TestFalse(TEXT("reported category state contains no retired indexed Cues entry"),
		Host->GetExpansionStateStringForTests().Contains(RetiredCuesPanel.ToString()));
	for (const FName PanelId : CurrentPanels)
	{
		TestTrue(*FString::Printf(TEXT("current category %s starts expanded"), *PanelId.ToString()),
			Host->IsSectionExpandedForTests(PanelId));
	}
	TestEqual(TEXT("Character Profile outer workspace is v15"),
		FCharacterProfileAssetEditorToolkit::CreateDefaultWorkspaceLayout()->GetLayoutName(),
		FName(TEXT("CharacterProfileAssetEditor_Layout_v15_ExpectedTags")));
	TestEqual(TEXT("Layer outer workspace uses the v13 playback-queue layout"),
		FCharacterLayerAssetEditorToolkit::WorkspaceLayoutId,
		FName(TEXT("CharacterLayerAssetEditor_Layout_v13_PlaybackQueue")));

	Host->Shutdown();
	CleanupConfig(ConfigPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceCharacterDataCompatibilityTest,
	"Paper2DPlus.ProfileWorkspace.CharacterDataFocusedTabsRetainTheirTools",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceCharacterDataCompatibilityTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	FFixture Fixture = MakeFixture();
	const FGameplayTag MappingTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Paper2DPlus.Animation.Combat")), false);
	if (!TestTrue(TEXT("mapping tag is registered"), MappingTag.IsValid()))
	{
		return false;
	}
	UPaperFlipbook* IdleFlipbook = NewObject<UPaperFlipbook>(
		Fixture.Profile, TEXT("CharacterDataIdleFlipbook"));
	UPaperFlipbook* AttackFlipbook = NewObject<UPaperFlipbook>(
		Fixture.Profile, TEXT("CharacterDataAttackFlipbook"));
	Fixture.Profile->Flipbooks[0].Identity.Flipbook = IdleFlipbook;
	Fixture.Profile->Flipbooks[1].Identity.Flipbook = AttackFlipbook;
	TestTrue(TEXT("retained mapping data assigns Idle through the production helper"),
		Fixture.Profile->AssignFlipbookToTagMapping(MappingTag, TEXT("Idle")));
	TestTrue(TEXT("retained mapping data assigns Attack through the production helper"),
		Fixture.Profile->AssignFlipbookToTagMapping(MappingTag, TEXT("Attack")));
	UObject* PaperZDSequenceSentinel =
		NewObject<UPaper2DPlusCharacterProfileAsset>(Fixture.Profile);
	Fixture.Profile->Flipbooks[0].Identity.PaperZDSequence = PaperZDSequenceSentinel;
	UClass* OptionalPaperZDSourceClass =
		SProfileDetailsPanel::ResolveOptionalPaperZDAnimationSourceClass();
	TestTrue(TEXT("optional PaperZD Source resolves only through its fully qualified class path"),
		!OptionalPaperZDSourceClass
		|| OptionalPaperZDSourceClass->GetPathName().Equals(
			TEXT("/Script/PaperZD.PaperZDAnimationSource"), ESearchCase::CaseSensitive));
	UClass* OptionalPaperZDSequenceClass =
		SProfileDetailsPanel::ResolveOptionalPaperZDFlipbookSequenceClass();
	TestTrue(TEXT("optional PaperZD sequence resolves only through its fully qualified class path"),
		!OptionalPaperZDSequenceClass
		|| OptionalPaperZDSequenceClass->GetPathName().Equals(
			TEXT("/Script/PaperZD.PaperZDAnimSequence_Flipbook"), ESearchCase::CaseSensitive));

	TSharedRef<SProfileDetailsPanel> Character = SNew(SProfileDetailsPanel)
		.Model(Fixture.Model)
		.PaneMode(EProfileDetailsPaneMode::Character);
	TSharedRef<SProfileDetailsPanel> PaperZDSequences = SNew(SProfileDetailsPanel)
		.Model(Fixture.Model)
		.PaneMode(EProfileDetailsPaneMode::PaperZDSequences);
	TSharedRef<SProfileDetailsPanel> AnimationTags = SNew(SProfileDetailsPanel)
		.Model(Fixture.Model)
		.PaneMode(EProfileDetailsPaneMode::Tags);
	TestTrue(TEXT("Character exposes the Profile-wide Sprite Bounds health surface"),
		Character->HasSpriteBoundsSurfaceForTests());
	TestTrue(TEXT("Character keeps a compact editable relative-transform surface"),
		Character->HasRelativeTransformSurfaceForTests());
	TestFalse(TEXT("Character does not construct PaperZD or animation Tags controls"),
		Character->HasPaperZDSurfaceForTests()
		|| Character->HasAnimationTagsAuthoringSurfaceForTests());
	TestTrue(TEXT("PaperZD Sequences owns the sequence management surface"),
		PaperZDSequences->HasPaperZDSurfaceForTests());
	TestFalse(TEXT("PaperZD Sequences does not construct Character or animation Tags controls"),
		PaperZDSequences->HasSpriteBoundsSurfaceForTests()
		|| PaperZDSequences->HasRelativeTransformSurfaceForTests()
		|| PaperZDSequences->HasAnimationTagsAuthoringSurfaceForTests());
	TestTrue(TEXT("the Animations Tags pane is the surviving selected-animation authoring home"),
		AnimationTags->HasAnimationTagsAuthoringSurfaceForTests());
	TestFalse(TEXT("an ordinary mapping member does not expose Chain Tags"),
		AnimationTags->HasChainTagsAuthoringSurfaceForTests());
	TestTrue(TEXT("PaperZD Sequences exposes sequence creation"),
		PaperZDSequences->HasPaperZDSequenceCreationActionForTests());
	const FString ProfilePrefix = Fixture.Profile->GetName() + TEXT("_");
	TestEqual(TEXT("default sequence name adds the profile prefix once"),
		PaperZDSequences->BuildDefaultPaperZDSequenceNameForTests(TEXT("Attack"), true),
		ProfilePrefix + TEXT("Attack"));
	TestEqual(TEXT("already-prefixed sequence name is not prefixed twice"),
		PaperZDSequences->BuildDefaultPaperZDSequenceNameForTests(
			ProfilePrefix + TEXT("Attack"), true),
		ProfilePrefix + TEXT("Attack"));
	TestEqual(TEXT("prefix can be disabled"),
		PaperZDSequences->BuildDefaultPaperZDSequenceNameForTests(TEXT("Attack"), false),
		FString(TEXT("Attack")));
	const TArray<FString> PendingSequenceNames =
		PaperZDSequences->GetPendingPaperZDSequenceNamesForTests(true);
	TestEqual(TEXT("only the unassigned flipbook needs a sequence"),
		PendingSequenceNames.Num(), 1);
	if (PendingSequenceNames.Num() == 1)
	{
		TestEqual(TEXT("pending sequence uses the stable default name"),
			PendingSequenceNames[0], ProfilePrefix + TEXT("Attack"));
	}

	TestTrue(TEXT("existing identity sequence fills its empty tag-mapping entry"),
		PaperZDSequences->AssignPaperZDSequenceForTests(
			IdleFlipbook, PaperZDSequenceSentinel));
	UObject* CreatedSequenceSentinel =
		NewObject<UPaper2DPlusCharacterProfileAsset>(Fixture.Profile);
	TestTrue(TEXT("created sequence fills identity and tag-mapping references"),
		PaperZDSequences->AssignPaperZDSequenceForTests(
			AttackFlipbook, CreatedSequenceSentinel));
	UObject* ConflictingSequenceSentinel =
		NewObject<UPaper2DPlusCharacterProfileAsset>(Fixture.Profile);
	TestFalse(TEXT("existing manual references are never overwritten"),
		PaperZDSequences->AssignPaperZDSequenceForTests(
			AttackFlipbook, ConflictingSequenceSentinel));
	const FFlipbookTagMapping* UpdatedMapping = Fixture.Profile->TagMappings.Find(MappingTag);
	TestTrue(TEXT("identity and mapping references stay paired after assignment"),
		UpdatedMapping
		&& UpdatedMapping->Entries.Num() == 2
		&& UpdatedMapping->Entries[0].PaperZDSequence == PaperZDSequenceSentinel
		&& UpdatedMapping->Entries[1].PaperZDSequence == CreatedSequenceSentinel
		&& Fixture.Profile->Flipbooks[0].Identity.PaperZDSequence == PaperZDSequenceSentinel
		&& Fixture.Profile->Flipbooks[1].Identity.PaperZDSequence == CreatedSequenceSentinel);
	TestTrue(TEXT("retained TagMappings still drive the visual FlipbookGroup"),
		Fixture.Profile->Flipbooks[0].FlipbookGroup == MappingTag.GetTagName()
		&& Fixture.Profile->Flipbooks[1].FlipbookGroup == MappingTag.GetTagName()
		&& Fixture.Profile->HasFlipbookGroup(MappingTag.GetTagName()));
	const TMap<FString, Paper2DPlusAnimationTagQuery::FAnimationTagSet> EffectiveTags =
		Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(Fixture.Profile);
	const Paper2DPlusAnimationTagQuery::FAnimationTagSet* IdleTags =
		EffectiveTags.Find(TEXT("idle"));
	const Paper2DPlusAnimationTagQuery::FAnimationTagSet* AttackTags =
		EffectiveTags.Find(TEXT("attack"));
	TestTrue(TEXT("retained TagMappings still contribute GroupImpliedTags"),
		IdleTags && AttackTags
		&& IdleTags->GroupImpliedTags.HasTagExact(MappingTag)
		&& AttackTags->GroupImpliedTags.HasTagExact(MappingTag));

	if (!TestNotNull(TEXT("GEditor available for relative-transform undo"), GEditor))
	{
		return false;
	}
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusProfileWorkspaceTest", "ResetRelativeTransform", "Relative Transform Test Reset"));
	const FVector AuthoredLocation(12.0, -3.0, 4.0);
	const FRotator AuthoredRotation(10.0, 20.0, 30.0);
	const FVector AuthoredScale(1.25, 0.75, 1.5);
	TestTrue(TEXT("relative Location commit writes through"),
		Character->CommitRelativeLocationForTests(AuthoredLocation));
	TestTrue(TEXT("relative Rotation commit writes through"),
		Character->CommitRelativeRotationForTests(AuthoredRotation));
	TestTrue(TEXT("relative Scale commit writes through"),
		Character->CommitRelativeScaleForTests(AuthoredScale));
	TestTrue(TEXT("all relative-transform values are stored"),
		Fixture.Profile->RelativeLocation.Equals(AuthoredLocation, 0.0)
		&& Fixture.Profile->RelativeRotation.Equals(AuthoredRotation, 0.0)
		&& Fixture.Profile->RelativeScale3D.Equals(AuthoredScale, 0.0));
	TestTrue(TEXT("undo restores Scale"), GEditor->UndoTransaction(true));
	TestTrue(TEXT("Scale returns to its default"),
		Fixture.Profile->RelativeScale3D.Equals(FVector(1.0, 1.0, 1.0), 0.0));
	TestTrue(TEXT("undo restores Rotation"), GEditor->UndoTransaction(true));
	TestTrue(TEXT("Rotation returns to its default"),
		Fixture.Profile->RelativeRotation.Equals(FRotator::ZeroRotator, 0.0));
	TestTrue(TEXT("undo restores Location"), GEditor->UndoTransaction(true));
	TestTrue(TEXT("Location returns to its default"),
		Fixture.Profile->RelativeLocation.Equals(FVector::ZeroVector, 0.0));
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusProfileWorkspaceTest", "EndRelativeTransform", "Relative Transform Test End"));

	Fixture.Model->SetSelectedFlipbook(1);
	Character->RefreshAll();
	PaperZDSequences->RefreshAll();
	AnimationTags->RefreshAll();
	TestEqual(TEXT("Character does not subscribe to irrelevant move-selection refreshes"),
		Character->GetSelectionRefreshCountForTests(), 0);
	TestEqual(TEXT("PaperZD Sequences does not subscribe to irrelevant move-selection refreshes"),
		PaperZDSequences->GetSelectionRefreshCountForTests(), 0);
	TestTrue(TEXT("the Animations Tags home follows the shared move selection"),
		AnimationTags->GetSelectionRefreshCountForTests() > 0);
	const FFlipbookTagMapping* Mapping = Fixture.Profile->TagMappings.Find(MappingTag);
	TestTrue(TEXT("Tag Mapping data survives retirement of its dedicated editor"),
		Mapping && Mapping->Entries.Num() == 2
		&& Mapping->Entries[0].FlipbookName == TEXT("Idle")
		&& Mapping->Entries[1].FlipbookName == TEXT("Attack"));
	TestTrue(TEXT("PaperZD sequence assignment survives Tag Mappings retirement"),
		Fixture.Profile->Flipbooks[0].Identity.PaperZDSequence == PaperZDSequenceSentinel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceChainAuthoringHomeTest,
	"Paper2DPlus.ProfileWorkspace.AnimationsAuthorsRetainedComboChainDataEndToEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceChainAuthoringHomeTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	if (!TestNotNull(TEXT("GEditor available for Map chain transactions"), GEditor))
	{
		return false;
	}

	FMapFixture Fixture = Workspace_MakeChainFixture();
	FFlipbookTagMapping* Mapping = Fixture.Profile->TagMappings.Find(Fixture.GroupTag);
	if (!TestNotNull(TEXT("chain fixture has its retained TagMappings group"), Mapping)
		|| !TestEqual(TEXT("chain fixture has three ordered members"),
			Mapping ? Mapping->Entries.Num() : 0, 3))
	{
		return false;
	}
	// The fixture normally starts projected as an existing chain. Clear that authored flag so this
	// test proves the NEW editor home creates every field through its production interaction funnels.
	Mapping->Entries[0].bIsChainStart = false;
	Mapping->Entries[2].bIsChainEnd = false;
	Mapping->Entries[0].ChainTags.Reset();
	Fixture.Model->SetSelectedFlipbook(0);

	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusProfileWorkspaceTest",
		"ResetAnimationsChainHome",
		"Animations Chain Home Test Reset"));
	TSharedRef<SAnimationMapPanel> MapPanel = SNew(SAnimationMapPanel)
		.Model(Fixture.Model);
	MapPanel->OpenAnimationMapView(TEXT("Combo"));

	UPaper2DPlusAnimationMapNode_ChainStart* StartMarker =
		Cast<UPaper2DPlusAnimationMapNode_ChainStart>(
			MapPanel->AddChainStartMarkerForTests(FVector2D(-220.0, -40.0)));
	TestNotNull(TEXT("Map Add Chain Start gesture creates a transient marker"), StartMarker);
	TestTrue(TEXT("Map aim gesture marks Jab as the retained TagMappings Chain Start"),
		MapPanel->LinkChainStartMarkerForTests(StartMarker, TEXT("Jab")));

	UPaper2DPlusAnimationMapNode_ChainEnd* EndMarker =
		Cast<UPaper2DPlusAnimationMapNode_ChainEnd>(
			MapPanel->AddChainEndMarkerForTests(FVector2D(420.0, 40.0)));
	TestNotNull(TEXT("Map Add Chain End gesture creates a transient marker"), EndMarker);
	TestTrue(TEXT("Map aim gesture marks Uppercut as the retained TagMappings Chain End"),
		MapPanel->LinkChainEndMarkerForTests(EndMarker, TEXT("Uppercut")));
	TestTrue(TEXT("Map gestures wrote the exact retained group entries"),
		Mapping->Entries[0].bIsChainStart
		&& !Mapping->Entries[1].bIsChainStart
		&& Mapping->Entries[2].bIsChainEnd);

	TSharedRef<SProfileDetailsPanel> TagsPanel = SNew(SProfileDetailsPanel)
		.Model(Fixture.Model)
		.PaneMode(EProfileDetailsPaneMode::Tags);
	TestTrue(TEXT("the selected opener exposes Chain Tags in the contextual Tags pane"),
		TagsPanel->HasChainTagsAuthoringSurfaceForTests());
	FGameplayTagContainer ChainIdentity;
	ChainIdentity.AddTag(Fixture.GroupTag);
	TestTrue(TEXT("the contextual Tags pane commits the opener's Chain Tags"),
		TagsPanel->CommitSelectedChainTagsForTests(ChainIdentity));
	TestTrue(TEXT("Chain Tags remain stored on the exact retained mapping entry"),
		Paper2DPlusAnimationTagQuery::AreExactTagSetsEqual(
			Mapping->Entries[0].ChainTags,
			ChainIdentity));

	const TArray<UPaperFlipbook*> Openers =
		UPaper2DPlusAnimationMapLibrary::GetComboOpenerFlipbooks(Fixture.Profile);
	TestEqual(TEXT("unchanged runtime opener discovery returns one Map-authored opener"),
		Openers.Num(), 1);
	if (Openers.Num() == 1)
	{
		TestTrue(TEXT("runtime opener discovery returns Jab"),
			Openers[0] == Fixture.Profile->Flipbooks[0].Identity.Flipbook.Get());
	}

	int32 ChainLength = 0;
	TestEqual(TEXT("unchanged flipbook-keyed runtime API resolves the authored chain"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainLength(
			Fixture.Profile,
			Fixture.Profile->Flipbooks[0].Identity.Flipbook.Get(),
			ChainLength),
		EPaper2DPlusComboChainResult::Success);
	TestEqual(TEXT("the authored Chain End keeps the countable chain at three moves"),
		ChainLength, 3);

	int32 TaggedChainLength = 0;
	TestEqual(TEXT("unchanged Chain-Tags runtime API resolves the contextual Tags write"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainLengthByTags(
			Fixture.Profile,
			ChainIdentity,
			TaggedChainLength),
		EPaper2DPlusComboChainResult::Success);
	TestEqual(TEXT("the Chain-Tags route resolves the same three-move chain"),
		TaggedChainLength, 3);

	UPaperFlipbook* FinalStep = nullptr;
	int32 IndexedChainLength = 0;
	TestEqual(TEXT("unchanged indexed runtime API resolves the authored final step"),
		UPaper2DPlusAnimationMapLibrary::GetComboChainFlipbookAtIndexByTags(
			Fixture.Profile,
			ChainIdentity,
			2,
			FinalStep,
			IndexedChainLength),
		EPaper2DPlusComboChainResult::Success);
	TestTrue(TEXT("the final runtime step is Uppercut"),
		FinalStep == Fixture.Profile->Flipbooks[2].Identity.Flipbook.Get());
	TestEqual(TEXT("indexed resolution reports the same chain length"),
		IndexedChainLength, 3);

	Mapping->Entries[0].bIsChainStart = false;
	FFlipbookTagMappingEntry DuplicateStart(TEXT("Jab"));
	const int32 DuplicateStartIndex = Mapping->Entries.Add(DuplicateStart);
	TestFalse(TEXT("a duplicate same-name target makes the Chain Start gesture fail closed"),
		MapPanel->LinkChainStartMarkerForTests(StartMarker, TEXT("Jab")));
	TestFalse(TEXT("the refused ambiguous Chain Start gesture writes neither duplicate"),
		Mapping->Entries[0].bIsChainStart
		|| Mapping->Entries[DuplicateStartIndex].bIsChainStart);
	TestFalse(TEXT("a marker with an ambiguous previous target also refuses a re-aim"),
		MapPanel->LinkChainStartMarkerForTests(StartMarker, TEXT("Cross")));
	TestFalse(TEXT("the refused Chain Start re-aim leaves its unique candidate untouched"),
		Mapping->Entries[1].bIsChainStart);

	Mapping->Entries[2].bIsChainEnd = false;
	FFlipbookTagMappingEntry DuplicateEnd(TEXT("Uppercut"));
	const int32 DuplicateEndIndex = Mapping->Entries.Add(DuplicateEnd);
	TestFalse(TEXT("a duplicate same-name target makes the Chain End gesture fail closed"),
		MapPanel->LinkChainEndMarkerForTests(EndMarker, TEXT("Uppercut")));
	TestFalse(TEXT("the refused ambiguous Chain End gesture writes neither duplicate"),
		Mapping->Entries[2].bIsChainEnd
		|| Mapping->Entries[DuplicateEndIndex].bIsChainEnd);
	TestFalse(TEXT("a Chain End marker with an ambiguous previous target refuses a re-aim"),
		MapPanel->LinkChainEndMarkerForTests(EndMarker, TEXT("Cross")));
	TestFalse(TEXT("the refused Chain End re-aim leaves its unique candidate untouched"),
		Mapping->Entries[1].bIsChainEnd);

	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusProfileWorkspaceTest",
		"EndAnimationsChainHome",
		"Animations Chain Home Test End"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceInvalidAnimationGroupRecoveryTest,
	"Paper2DPlus.ProfileWorkspace.AnimationMapRepairsInvalidTagMappingKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceInvalidAnimationGroupRecoveryTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	if (!TestNotNull(TEXT("GEditor available for invalid-group recovery transactions"), GEditor))
	{
		return false;
	}

	FMapFixture Fixture = Workspace_MakeChainFixture();
	FFlipbookTagMapping* SourceMapping =
		Fixture.Profile->TagMappings.Find(Fixture.GroupTag);
	if (!TestNotNull(TEXT("recovery fixture starts with its valid source mapping"), SourceMapping))
	{
		return false;
	}
	SourceMapping->Entries[2].bIsChainEnd = true;
	SourceMapping->Entries[0].ChainTags.AddTag(Fixture.GroupTag);
	TestTrue(TEXT("test setup moves the populated group onto the invalid key"),
		Fixture.Profile->RenameTagMapping(Fixture.GroupTag, FGameplayTag()));
	const FFlipbookTagMapping* InvalidMapping = Fixture.Profile->TagMappings.Find(FGameplayTag());
	if (!TestNotNull(TEXT("the invalid setup preserves the mapping"), InvalidMapping))
	{
		return false;
	}
	TestEqual(TEXT("the invalid setup preserves every ordered member"),
		InvalidMapping->Entries.Num(), 3);

	TSharedRef<SAnimationMapPanel> MapPanel = SNew(SAnimationMapPanel)
		.Model(Fixture.Model);
	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusProfileWorkspaceTest",
		"ResetInvalidAnimationGroupRepair",
		"Invalid Animation Group Repair Reset"));
	TestFalse(TEXT("a populated invalid group cannot be removed through the empty-shell action"),
		MapPanel->RepairInvalidTagMappingForTests(FGameplayTag()));
	TestTrue(TEXT("the Map recovery action moves a populated invalid group to an unused valid tag"),
		MapPanel->RepairInvalidTagMappingForTests(Fixture.GroupTag));
	const FFlipbookTagMapping* RepairedMapping =
		Fixture.Profile->TagMappings.Find(Fixture.GroupTag);
	TestTrue(TEXT("repair removes the invalid key and preserves the exact ordered mapping"),
		!Fixture.Profile->TagMappings.Contains(FGameplayTag())
		&& RepairedMapping
		&& RepairedMapping->Entries.Num() == 3
		&& RepairedMapping->Entries[0].FlipbookName == TEXT("Jab")
		&& RepairedMapping->Entries[1].FlipbookName == TEXT("Cross")
		&& RepairedMapping->Entries[2].FlipbookName == TEXT("Uppercut")
		&& RepairedMapping->Entries[0].bIsChainStart
		&& RepairedMapping->Entries[2].bIsChainEnd
		&& RepairedMapping->Entries[0].ChainTags.HasTagExact(Fixture.GroupTag));
	TestTrue(TEXT("repair restores every member's visual FlipbookGroup"),
		Fixture.Profile->Flipbooks[0].FlipbookGroup == Fixture.GroupTag.GetTagName()
		&& Fixture.Profile->Flipbooks[1].FlipbookGroup == Fixture.GroupTag.GetTagName()
		&& Fixture.Profile->Flipbooks[2].FlipbookGroup == Fixture.GroupTag.GetTagName());
	TestTrue(TEXT("one undo restores the populated invalid group"),
		GEditor->UndoTransaction(true));
	InvalidMapping = Fixture.Profile->TagMappings.Find(FGameplayTag());
	TestTrue(TEXT("undo restores the invalid key and its entries"),
		InvalidMapping && InvalidMapping->Entries.Num() == 3
		&& !Fixture.Profile->TagMappings.Contains(Fixture.GroupTag));

	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusProfileWorkspaceTest",
		"ResetInvalidEmptyAnimationGroupRemoval",
		"Invalid Empty Animation Group Removal Reset"));
	FFlipbookTagMapping* EmptyInvalidMapping =
		Fixture.Profile->TagMappings.Find(FGameplayTag());
	if (!TestNotNull(TEXT("empty-removal setup still has the invalid key"), EmptyInvalidMapping))
	{
		return false;
	}
	EmptyInvalidMapping->Entries.Reset();
	TestTrue(TEXT("the Map recovery action removes an invalid empty shell"),
		MapPanel->RepairInvalidTagMappingForTests(FGameplayTag()));
	TestFalse(TEXT("the empty invalid key is gone"),
		Fixture.Profile->TagMappings.Contains(FGameplayTag()));
	TestTrue(TEXT("one undo restores the empty invalid shell"),
		GEditor->UndoTransaction(true));
	const FFlipbookTagMapping* RestoredEmptyMapping =
		Fixture.Profile->TagMappings.Find(FGameplayTag());
	TestTrue(TEXT("undo restores the exact empty invalid mapping"),
		RestoredEmptyMapping && RestoredEmptyMapping->Entries.IsEmpty());

	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusProfileWorkspaceTest",
		"EndInvalidAnimationGroupRecovery",
		"Invalid Animation Group Recovery End"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceBrowseBarTest,
	"Paper2DPlus.ProfileWorkspace.FlipbookBrowserCompactBrowseBarExposesContextualAndOverflowActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceBrowseBarTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	FFixture Fixture = MakeFixture();
	TSharedPtr<SFlipbookBrowserPanel> Browser = SNew(SFlipbookBrowserPanel)
		.Model(Fixture.Model);

	// R4: one persistent collection mutation, everything else contextual or menu-hosted. The seam
	// measures the bar's registry of constructed mutation controls and their exact visibility
	// attributes, so this names the survivor rather than just counting it — a second always-visible
	// mutation, or a contextual control that lost its conditional visibility, changes the list.
	const TArray<FName> ExpectedPersistentMutations({ FName(TEXT("AddFlipbooks")) });
	TestEqual(TEXT("Add Flipbooks is the browse bar's only persistent collection mutation"),
		Browser->GetPersistentCollectionActionsForTests(), ExpectedPersistentMutations);
	TestEqual(TEXT("the persistent-mutation count agrees with the named set"),
		Browser->GetPersistentCollectionActionCountForTests(), 1);

	// The lens is restored per asset from GEditorPerProjectIni, so pin the starting mode instead of
	// depending on whatever a previous session persisted for this path.
	Browser->SetByTagLensForTests(false);

	// R8 / AE-organize: the labeled combo reports AND switches the existing lens, and switching the
	// organization mode never disturbs the shared animation selection.
	TestEqual(TEXT("the organize control reports the active My Groups lens"),
		Browser->GetOrganizeLabelForTests().ToString(),
		FString(TEXT("Organize: My Groups")));

	// R7: both group-maintenance commands stay LISTED in My Groups and are enabled there.
	{
		const TArray<FFlipbookBrowseOverflowEntry> Overflow = Browser->GetBrowseOverflowActionsForTests();
		TestEqual(TEXT("Browse overflow lists exactly New Group and Auto-group"), Overflow.Num(), 2);
		if (Overflow.Num() == 2)
		{
			TestTrue(TEXT("Browse overflow lists New Group first"),
				Overflow[0].Action == EFlipbookBrowseOverflowAction::NewGroup
				&& Overflow[0].Label.ToString() == FString(TEXT("New Group")));
			TestTrue(TEXT("Browse overflow lists Auto-group second"),
				Overflow[1].Action == EFlipbookBrowseOverflowAction::AutoGroup
				&& Overflow[1].Label.ToString() == FString(TEXT("Auto-group")));
			TestTrue(TEXT("both overflow actions are enabled in My Groups"),
				Overflow[0].bEnabled && Overflow[1].bEnabled);
			TestTrue(TEXT("enabled overflow actions still explain themselves"),
				!Overflow[0].ToolTip.IsEmpty() && !Overflow[1].ToolTip.IsEmpty());
		}
	}

	const int32 SelectionBeforeLensSwitch = Fixture.Model->GetSelectedFlipbookIndex();
	Browser->SetByTagLensForTests(true);
	TestTrue(TEXT("the organize control switches the existing lens"),
		Browser->IsByTagLensForTests());
	TestEqual(TEXT("the organize control reports By Tag Group once switched"),
		Browser->GetOrganizeLabelForTests().ToString(),
		FString(TEXT("Organize: By Tag Group")));
	TestEqual(TEXT("switching the organization mode preserves the shared selection"),
		Fixture.Model->GetSelectedFlipbookIndex(), SelectionBeforeLensSwitch);

	// R7 continued: still VISIBLE in By Tag Group, but disabled with an explanatory tooltip.
	{
		const TArray<FFlipbookBrowseOverflowEntry> Overflow = Browser->GetBrowseOverflowActionsForTests();
		TestEqual(TEXT("Browse overflow still lists both actions in By Tag Group"), Overflow.Num(), 2);
		if (Overflow.Num() == 2)
		{
			TestFalse(TEXT("New Group is disabled in By Tag Group"), Overflow[0].bEnabled);
			TestFalse(TEXT("Auto-group is disabled in By Tag Group"), Overflow[1].bEnabled);
			TestTrue(TEXT("the disabled state names By Tag Group as the reason"),
				Overflow[0].ToolTip.ToString().Contains(TEXT("By Tag Group"))
				&& Overflow[1].ToolTip.ToString().Contains(TEXT("By Tag Group")));
		}
	}

	Browser->SetByTagLensForTests(false);
	TestFalse(TEXT("the organize control returns to My Groups"), Browser->IsByTagLensForTests());

	// R5/R16 + AE2: an inactive completion filter offers no clear affordance; an active one is
	// directly clearable and clearing restores the compact inactive state.
	TestFalse(TEXT("an inactive completion filter exposes no clear affordance"),
		Browser->IsCompletionFilterActiveForTests());
	Fixture.Model->SetCompletionFilterMask(1 << 0);
	TestTrue(TEXT("an active completion filter is visibly identifiable"),
		Browser->IsCompletionFilterActiveForTests());
	Browser->ClearCompletionFilterForTests();
	TestEqual(TEXT("the one-click clear resets the completion filter mask"),
		Fixture.Model->GetCompletionFilterMask(), 0);
	TestFalse(TEXT("clearing restores the compact inactive filter state"),
		Browser->IsCompletionFilterActiveForTests());

	// R6/AE3: Delete is contextual on the selection. Fixed / Baked Layer ownership does not make it
	// vanish for a cause the designer cannot infer — it stays visible for its selected target and is
	// disabled with the same ownership explanation Add gives.
	Fixture.Model->SetSelectedFlipbook(0);
	TestTrue(TEXT("Delete is available for an ordinary deletable selection"),
		Browser->IsDeleteActionVisibleForTests());
	TestTrue(TEXT("Delete is enabled for an ordinary deletable selection"),
		Browser->IsDeleteActionEnabledForTests());
	TestEqual(TEXT("a deletable selection puts Add and Delete on the bar"),
		Browser->GetVisibleCollectionActionsForTests(),
		TArray<FName>({ FName(TEXT("AddFlipbooks")), FName(TEXT("DeleteSelected")) }));
	Fixture.Model->SetSelectedFlipbook(INDEX_NONE);
	TestFalse(TEXT("Delete collapses with no deletable selection"),
		Browser->IsDeleteActionVisibleForTests());
	TestEqual(TEXT("with nothing selected Add is the only mutation on the bar"),
		Browser->GetVisibleCollectionActionsForTests(),
		TArray<FName>({ FName(TEXT("AddFlipbooks")) }));
	TestEqual(TEXT("a collapsed contextual Delete never joins the persistent set"),
		Browser->GetPersistentCollectionActionsForTests(), ExpectedPersistentMutations);
	Fixture.Model->SetSelectedFlipbook(0);
#if WITH_EDITORONLY_DATA
	Fixture.Profile->LayerBakeOwnerToken = FGuid::NewGuid();
	TestTrue(TEXT("Fixed / Baked ownership keeps Delete visible for its selected target"),
		Browser->IsDeleteActionVisibleForTests());
	TestFalse(TEXT("Fixed / Baked ownership disables Delete instead of hiding it"),
		Browser->IsDeleteActionEnabledForTests());
	TestTrue(TEXT("the disabled Delete explains the Fixed / Baked Layer ownership"),
		Browser->GetDeleteActionTooltipForTests().ToString().Contains(TEXT("Fixed / Baked")));
	TestEqual(TEXT("bake ownership never changes the persistent-mutation set"),
		Browser->GetPersistentCollectionActionsForTests(), ExpectedPersistentMutations);
	Fixture.Profile->LayerBakeOwnerToken.Invalidate();
	TestTrue(TEXT("Delete is enabled again once bake ownership is released"),
		Browser->IsDeleteActionEnabledForTests());
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceExpectedTagDropTargetsTest,
	"Paper2DPlus.ProfileWorkspace.FlipbookBrowserRowsAndCardsAcceptExpectedTagDropsInBothLenses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceExpectedTagDropTargetsTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	FFixture Fixture = MakeFixture();
	const TSharedRef<SFlipbookBrowserPanel> Browser = SNew(SFlipbookBrowserPanel)
		.Model(Fixture.Model);
	const FName DropTargetType(TEXT("SExpectedTagAnimationDropTarget"));
	const FName ExistingGestureOwnerType(TEXT("SDragClickWrapper"));

	// The lens is persisted per asset, so pin the state before measuring the constructed tree.
	Browser->SetByTagLensForTests(false);
	Fixture.Model->SetFlipbookGroupGridView(true);
	TestEqual(TEXT("every My Groups grid card accepts an Expected Tag assignment"),
		Workspace_CountWidgetType(Browser, DropTargetType), Fixture.Profile->Flipbooks.Num());
	TestEqual(TEXT("every My Groups grid card retains its existing click/group-drag owner"),
		Workspace_CountWidgetType(Browser, ExistingGestureOwnerType), Fixture.Profile->Flipbooks.Num());

	Fixture.Model->SetFlipbookGroupGridView(false);
	TestEqual(TEXT("every My Groups list row accepts an Expected Tag assignment"),
		Workspace_CountWidgetType(Browser, DropTargetType), Fixture.Profile->Flipbooks.Num());
	TestEqual(TEXT("every My Groups list row retains its existing click/group-drag owner"),
		Workspace_CountWidgetType(Browser, ExistingGestureOwnerType), Fixture.Profile->Flipbooks.Num());

	// By Tag Group currently presents its derived sections as cards regardless of the My Groups
	// Grid/List choice. Those cards remain assignment targets even though group-reorder drag-out is
	// deliberately disabled in this read-only organization lens.
	Browser->SetByTagLensForTests(true);
	TestEqual(TEXT("every By Tag Group card accepts an Expected Tag assignment"),
		Workspace_CountWidgetType(Browser, DropTargetType), Fixture.Profile->Flipbooks.Num());
	TestEqual(TEXT("every By Tag Group card retains its existing click/context gesture owner"),
		Workspace_CountWidgetType(Browser, ExistingGestureOwnerType), Fixture.Profile->Flipbooks.Num());

	Browser->SetByTagLensForTests(false);
	return true;
}

// =====================================================================================================
// TASK-151 U1 — the shared header's View menu (R1-R3, R15, R17-R19; F1, F5; AE1, AE7-AE8)
// =====================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceAnimationsViewControlTest,
	"Paper2DPlus.ProfileWorkspace.AnimationsHeaderViewMenuNamesEveryViewAndRoutesThroughSetViewMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceAnimationsViewControlTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;

	// R2: GetViewModeLabel is the ONE label source shared by the closed control and its menu entries,
	// and the Slate-free seam this assertion exists for. A view whose label drifts from its designer
	// name (or falls through to the Grid default) is caught here before any widget is involved.
	TestEqual(TEXT("Grid is labelled Grid"),
		SAnimationsPanel::GetViewModeLabel(EAnimationsViewMode::Grid).ToString(),
		FString(TEXT("Grid")));
	TestEqual(TEXT("List is labelled List"),
		SAnimationsPanel::GetViewModeLabel(EAnimationsViewMode::List).ToString(),
		FString(TEXT("List")));
	TestEqual(TEXT("Map is labelled Map"),
		SAnimationsPanel::GetViewModeLabel(EAnimationsViewMode::Map).ToString(),
		FString(TEXT("Map")));

	FFixture Fixture = MakeFixture();
	Fixture.Model->SetSelectedFlipbook(1);
	Fixture.Model->SetSelectedFrame(3);
	const FString ViewModeKey = Workspace_AnimationsViewModeKey(Fixture.Profile);
	if (GConfig)
	{
		GConfig->RemoveKey(Workspace_AnimationsViewModeSection, *ViewModeKey, GEditorPerProjectIni);
	}

	TSharedRef<SAnimationsPanel> Animations = SNew(SAnimationsPanel)
		.Model(Fixture.Model)
		.PersistViewMode(false);

	// R1/AE1: the retired [Grid | List | Map] row is gone — the panel's whole ChildSlot is the view
	// switcher, so no second navigation row can have crept back in beside it.
	FChildren* PanelChildren = Animations->GetChildren();
	TestEqual(TEXT("the Animations panel hosts exactly one child"),
		PanelChildren ? PanelChildren->Num() : 0, 1);
	if (PanelChildren && PanelChildren->Num() == 1)
	{
		TestEqual(TEXT("that one child is the view switcher, not a dedicated view row"),
			PanelChildren->GetChildAt(0)->GetType(), FName(TEXT("SWidgetSwitcher")));
	}

	// KTD1: the header action is built FRESH per tab host. A cached/reused widget attached to a second
	// Slate parent is a hard error, and the Animations tab can be respawned.
	const TSharedRef<SWidget> HeaderControl = Animations->MakeHeaderViewControl();
	const TSharedRef<SWidget> SecondHeaderControl = Animations->MakeHeaderViewControl();
	TestFalse(TEXT("each header host receives its own View control instance"),
		HeaderControl == SecondHeaderControl);

	// R2/R16: the closed control states the active view without opening the menu, and it reads that
	// name from the same GetViewModeLabel seam asserted above.
	auto TestHeaderNamesView = [this, &HeaderControl](EAnimationsViewMode Mode)
	{
		const FString Expected = FString::Printf(
			TEXT("View: %s"), *SAnimationsPanel::GetViewModeLabel(Mode).ToString());
		TestTrue(
			*FString::Printf(TEXT("the shared-header control reads '%s'"), *Expected),
			Workspace_ReadWidgetTexts(HeaderControl).HasVisible(*Expected));
	};

	// KTD2/F1: every view routes through the unchanged SetViewMode state authority — Grid and List
	// drive the browser's card-vs-row flag through the model, Map leaves it alone, and no view switch
	// disturbs the shared animation selection. The panel opens on Grid, so List is asserted first:
	// every step below is a genuine transition rather than a same-value no-op.
	Animations->SetViewMode(EAnimationsViewMode::List);
	TestEqual(TEXT("List is the active view"),
		static_cast<int32>(Animations->GetViewMode()),
		static_cast<int32>(EAnimationsViewMode::List));
	TestFalse(TEXT("List renders the browser as rows"), Fixture.Model->IsFlipbookGroupGridView());
	TestHeaderNamesView(EAnimationsViewMode::List);

	Animations->SetViewMode(EAnimationsViewMode::Grid);
	TestEqual(TEXT("Grid is the active view"),
		static_cast<int32>(Animations->GetViewMode()),
		static_cast<int32>(EAnimationsViewMode::Grid));
	TestTrue(TEXT("Grid renders the browser as cards"), Fixture.Model->IsFlipbookGroupGridView());
	TestHeaderNamesView(EAnimationsViewMode::Grid);

	Animations->SetViewMode(EAnimationsViewMode::Map);
	TestEqual(TEXT("Map is the active view"),
		static_cast<int32>(Animations->GetViewMode()),
		static_cast<int32>(EAnimationsViewMode::Map));
	// Map is not a browser view, so it must not touch the browser's render flag — it stays exactly
	// where Grid left it, ready for the designer's return.
	TestTrue(TEXT("Map leaves the browser's card-vs-row flag at the value Grid set"),
		Fixture.Model->IsFlipbookGroupGridView());
	TestHeaderNamesView(EAnimationsViewMode::Map);

	TestEqual(TEXT("switching views never disturbs the shared animation selection"),
		Fixture.Model->GetSelectedFlipbookIndex(), 1);
	TestEqual(TEXT("switching views never disturbs the shared frame selection"),
		Fixture.Model->GetSelectedFrameIndex(), 3);

	FString PersistedViewMode;
	TestFalse(TEXT("disabled persistence writes no per-asset view mode"),
		GConfig
		&& GConfig->GetString(
			Workspace_AnimationsViewModeSection,
			*ViewModeKey,
			PersistedViewMode,
			GEditorPerProjectIni));

	// R3/AE7: no Character Profile workspace surface carries a persistent Validate action any more —
	// validation lives in the Asset menu (or the world-centric Profile Actions fallback).
	Animations->SetViewMode(EAnimationsViewMode::Grid);
	TestFalse(TEXT("the Animations workspace constructs no Validate control"),
		Workspace_ReadWidgetTexts(Animations).HasAnyContaining(TEXT("Validate")));
	TestFalse(TEXT("the shared-header View control is not a Validate host"),
		Workspace_ReadWidgetTexts(HeaderControl).HasAnyContaining(TEXT("Validate")));

	// KTD4: the Content Browser action and the editor Asset menu both call this one interactive JSON
	// boundary, so its user-facing copy exists exactly once. The Apply/Cancel/warning behaviour of
	// that boundary is ratcheted by Paper2DPlus.FrameCueTracks.Json.LayoutNeutralDisclosureApplyCancelAndWarning
	// and is deliberately not duplicated here.
	TestFalse(TEXT("the shared interactive export disclosure is authored"),
		Paper2DPlusCharacterProfileJsonInteraction::GetExportLayoutDisclosure().IsEmpty());
	return true;
}

// =====================================================================================================
// TASK-151 U3 — the Animation Map canvas rail (R9-R14, R16-R19; F3-F4; AE1-AE6)
//
// These read the constructed rail rather than a per-control seam, because SAnimationMapPanel exposes
// none. "Visible" and "constructed but collapsed" are asserted separately: a contextual action must be
// BUILT and COLLAPSED, while a command that moved into the overflow menu must not be on the rail at
// all until the menu is opened.
// =====================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceMapCanvasRailTest,
	"Paper2DPlus.ProfileWorkspace.AnimationMapCanvasRailShowsOnlyScopeAppropriateControls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceMapCanvasRailTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	FMapFixture Fixture = Workspace_MakeChainFixture();
	if (!TestTrue(TEXT("the fixture's Animation Group tag is registered"), Fixture.GroupTag.IsValid()))
	{
		return false;
	}

	// Pin the filter OFF: the rail restores a persisted tag at construct time, and this test asserts the
	// inactive compact control (the active chip is FPaper2DPlusProfileWorkspaceMapFilterChipTest's job).
	const FWorkspaceScopedMapFilterConfig NoPersistedFilter{ FString() };
	const TSharedRef<SAnimationMapPanel> Map = SNew(SAnimationMapPanel).Model(Fixture.Model);

	// Asserted in EVERY scope below: advanced tools and the stable overflow entry point do not
	// depend on which graph is open.
	auto TestScopeIndependentRailContract = [this, &Map](const TCHAR* Scope)
	{
		const FWorkspaceWidgetTexts Rail = Workspace_ReadWidgetTexts(Map);

		// R13: phase derivation lives in the Map overflow, so it is not a persistent rail control. The
		// overflow builds its entries on demand, so its label must not appear while the menu is closed.
		TestFalse(*FString::Printf(TEXT("%s: Derive Missing Phases is not a persistent rail control"), Scope),
			Rail.HasAnyContaining(TEXT("Derive Missing Phases")));

		// R9/R19: the labelled overflow entry point is always reachable — it is also the stable focus
		// target a self-removing contextual action returns to.
		TestTrue(*FString::Printf(TEXT("%s: the Map overflow entry point is available"), Scope),
			Rail.HasVisible(TEXT("Map")));

	};

	// --- The group board (AE1): scope navigation offers Full Map only; the graph-only viewport and
	// scope controls stay collapsed.
	Map->OpenAnimationMapView(TEXT("groups"));
	{
		const FWorkspaceWidgetTexts Rail = Workspace_ReadWidgetTexts(Map);
		TestTrue(TEXT("board: Full Map is offered"), Rail.HasVisible(TEXT("Full Map")));
		TestFalse(TEXT("board: Back to Groups is hidden on the board itself"),
			Rail.HasVisible(TEXT("Back to Groups")));
		TestFalse(TEXT("board: Zoom to Fit is hidden without a graph"),
			Rail.HasVisible(TEXT("Zoom to Fit")));
		TestFalse(TEXT("board: no graph scope label is shown"),
			Rail.HasVisibleContaining(TEXT(" graph")));
		TestTrue(TEXT("board: the compact Filter control is the inactive filter state"),
			Rail.HasVisible(TEXT("Filter")));
	}
	TestScopeIndependentRailContract(TEXT("board"));

	// --- The full composition graph: navigation flips to Back to Groups, Zoom to Fit applies, and the
	// scope label names the open graph.
	Map->OpenAnimationMapView(TEXT("full"));
	{
		const FWorkspaceWidgetTexts Rail = Workspace_ReadWidgetTexts(Map);
		TestFalse(TEXT("full graph: Full Map collapses once it is open"),
			Rail.HasVisible(TEXT("Full Map")));
		TestTrue(TEXT("full graph: Back to Groups is offered"),
			Rail.HasVisible(TEXT("Back to Groups")));
		TestTrue(TEXT("full graph: Zoom to Fit is offered"), Rail.HasVisible(TEXT("Zoom to Fit")));
		TestTrue(TEXT("full graph: the rail names the open scope"),
			Rail.HasVisible(TEXT("Full composition graph")));
	}
	TestScopeIndependentRailContract(TEXT("full graph"));

	// --- A scoped group graph names its own group.
	Map->OpenAnimationMapView(TEXT("Combo"));
	{
		const FWorkspaceWidgetTexts Rail = Workspace_ReadWidgetTexts(Map);
		TestTrue(TEXT("group graph: Back to Groups is offered"),
			Rail.HasVisible(TEXT("Back to Groups")));
		TestTrue(TEXT("group graph: Zoom to Fit is offered"), Rail.HasVisible(TEXT("Zoom to Fit")));
		TestTrue(TEXT("group graph: the rail names the open group"),
			Rail.HasVisible(TEXT("Combat.Combo graph")));
	}
	TestScopeIndependentRailContract(TEXT("group graph"));

	// --- The unassigned bucket is a graph surface too.
	Map->OpenAnimationMapView(TEXT("unassigned"));
	{
		const FWorkspaceWidgetTexts Rail = Workspace_ReadWidgetTexts(Map);
		TestTrue(TEXT("unassigned graph: the rail names the open scope"),
			Rail.HasVisible(TEXT("Unassigned graph")));
		TestFalse(TEXT("unassigned graph: Full Map collapses on a graph surface"),
			Rail.HasVisible(TEXT("Full Map")));
	}
	TestScopeIndependentRailContract(TEXT("unassigned graph"));

	// --- Returning to the board restores the board's own navigation without leaking graph controls.
	Map->OpenAnimationMapView(TEXT("groups"));
	{
		const FWorkspaceWidgetTexts Rail = Workspace_ReadWidgetTexts(Map);
		TestTrue(TEXT("back on the board: Full Map returns"), Rail.HasVisible(TEXT("Full Map")));
		TestFalse(TEXT("back on the board: Zoom to Fit collapses again"),
			Rail.HasVisible(TEXT("Zoom to Fit")));
		TestFalse(TEXT("back on the board: the scope label collapses again"),
			Rail.HasVisibleContaining(TEXT(" graph")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceMapFilterChipTest,
	"Paper2DPlus.ProfileWorkspace.AnimationMapFilterIsCompactWhenInactiveAndARemovableChipWhenActive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceMapFilterChipTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	if (!TestNotNull(TEXT("GConfig available"), GConfig))
	{
		return false;
	}
	FMapFixture Fixture = Workspace_MakeChainFixture();
	if (!TestTrue(TEXT("the fixture's Animation Group tag is registered"), Fixture.GroupTag.IsValid()))
	{
		return false;
	}

	// R10/AE2 inactive: filtering costs one compact control and shows no tag state.
	{
		const FWorkspaceScopedMapFilterConfig NoFilter{ FString() };
		const TSharedRef<SAnimationMapPanel> Map = SNew(SAnimationMapPanel).Model(Fixture.Model);
		const FWorkspaceWidgetTexts Rail = Workspace_ReadWidgetTexts(Map);
		TestTrue(TEXT("an inactive filter is one compact Filter control"),
			Rail.HasVisible(TEXT("Filter")));
		TestFalse(TEXT("an inactive filter shows no tag chip"),
			Rail.HasVisibleContaining(TEXT("Combo")));
		TestFalse(TEXT("an inactive filter offers no clear affordance"),
			Rail.HasVisible(TEXT("✕")));
	}

	// R10/R16/AE2 active: the compact control is REPLACED by a removable chip naming the tag, so a
	// collapsed control can never hide a live filter.
	{
		const FWorkspaceScopedMapFilterConfig ActiveFilter{ Fixture.GroupTag.GetTagName().ToString() };
		const TSharedRef<SAnimationMapPanel> Map = SNew(SAnimationMapPanel).Model(Fixture.Model);
		const FWorkspaceWidgetTexts Rail = Workspace_ReadWidgetTexts(Map);
		TestTrue(TEXT("an active filter names its tag on the rail"),
			Rail.HasVisibleContaining(TEXT("Combo")));
		TestTrue(TEXT("an active filter chip is directly removable"),
			Rail.HasVisible(TEXT("✕")));
		TestFalse(TEXT("the compact Filter control gives way to the active chip"),
			Rail.HasVisible(TEXT("Filter")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusProfileWorkspaceMapDerivePhasesTest,
	"Paper2DPlus.ProfileWorkspace.AnimationMapDeriveMissingPhasesIsWholeProfileAndFillOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusProfileWorkspaceMapDerivePhasesTest::RunTest(const FString& Parameters)
{
	using namespace Paper2DPlusProfileWorkspaceTest;
	if (!TestNotNull(TEXT("GEditor available"), GEditor))
	{
		return false;
	}
	const FGameplayTag StartupTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Paper2DPlus.Phase.Startup")), false);
	const FGameplayTag RecoveryTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("Paper2DPlus.Phase.Recovery")), false);
	if (!TestTrue(TEXT("phase tags are registered"), StartupTag.IsValid() && RecoveryTag.IsValid()))
	{
		return false;
	}

	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusProfileWorkspaceTest", "ResetDerivePhases", "Derive Phases Test Reset"));
	FMapFixture Fixture = Workspace_MakeChainFixture();
	if (!TestTrue(TEXT("the fixture's Animation Group tag is registered"), Fixture.GroupTag.IsValid()))
	{
		return false;
	}
	Fixture.Profile->AddToRoot();

	// A manually authored phase tag that derivation would NOT have chosen for that chain position.
	// Fill-only means this exact value survives.
	Fixture.Profile->Flipbooks[1].EditorMeta.PhaseTag = StartupTag;

	const TSharedRef<SAnimationMapPanel> Map = SNew(SAnimationMapPanel).Model(Fixture.Model);

	// R13/AE6: whole-profile and fill-only — it writes the two untagged moves and leaves the manual one.
	TestEqual(TEXT("Derive Missing Phases fills only the untagged moves"),
		Map->DerivePhasesFromMapAction(), 2);
	TestEqual(TEXT("the chain opener becomes Startup"),
		Fixture.Profile->Flipbooks[0].EditorMeta.PhaseTag.GetTagName(), StartupTag.GetTagName());
	TestEqual(TEXT("the manually authored phase tag is preserved exactly"),
		Fixture.Profile->Flipbooks[1].EditorMeta.PhaseTag.GetTagName(), StartupTag.GetTagName());
	TestEqual(TEXT("the chain's last countable step becomes Recovery"),
		Fixture.Profile->Flipbooks[2].EditorMeta.PhaseTag.GetTagName(), RecoveryTag.GetTagName());

	// A second run has nothing left to fill, so it is a no-op that opens no transaction.
	TestEqual(TEXT("a second run derives nothing"), Map->DerivePhasesFromMapAction(), 0);

	// KTD8: the action still runs through the panel's existing transaction template.
	TestTrue(TEXT("one Undo reverts the whole derivation"), GEditor->UndoTransaction(true));
	TestFalse(TEXT("Undo clears the derived opener phase"),
		Fixture.Profile->Flipbooks[0].EditorMeta.PhaseTag.IsValid());
	TestFalse(TEXT("Undo clears the derived finisher phase"),
		Fixture.Profile->Flipbooks[2].EditorMeta.PhaseTag.IsValid());
	TestEqual(TEXT("Undo leaves the manually authored phase tag alone"),
		Fixture.Profile->Flipbooks[1].EditorMeta.PhaseTag.GetTagName(), StartupTag.GetTagName());

	GEditor->ResetTransaction(NSLOCTEXT(
		"Paper2DPlusProfileWorkspaceTest", "EndDerivePhases", "Derive Phases Test End"));
	Fixture.Profile->RemoveFromRoot();
	return true;
}

#endif // WITH_EDITOR
