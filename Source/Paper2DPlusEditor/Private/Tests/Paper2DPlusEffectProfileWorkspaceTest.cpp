// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Paper2DPlusTestSkip.h"

#include "AssetRegistry/AssetData.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorCanvasUtils.h"
#include "EffectProfileAssetEditorToolkit.h"
#include "EffectProfileEditor/EffectProfileDetailsPanel.h"
#include "EffectProfileEditor/EffectProfileLibraryPanel.h"
#include "EffectProfileEditor/EffectProfilePreviewPanel.h"
#include "EffectProfileEditorModel.h"
#include "Engine/Texture2D.h"
#include "Paper2DPlusEditorTestFrameCueTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Misc/App.h"
#include "Paper2DPlusEffectProfileAsset.h"
#include "Paper2DPlusEffectTags.h"
#include "PaperFlipbook.h"
#include "ProfileItemPicker.h"
#include "Subsystems/ImportSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWindow.h"

namespace
{
	UPaper2DPlusEffectProfileAsset* EffectWorkspace_MakeAsset(const TCHAR* BaseName)
	{
		const FName Name = MakeUniqueObjectName(
			GetTransientPackage(),
			UPaper2DPlusEffectProfileAsset::StaticClass(),
			FName(BaseName));
		return NewObject<UPaper2DPlusEffectProfileAsset>(
			GetTransientPackage(), Name, RF_Transactional);
	}

	UPaperFlipbook* EffectWorkspace_MakeFlipbook(
		const TCHAR* BaseName,
		int32 KeyFrames = 0,
		int32 FrameRun = 1,
		float FramesPerSecond = 10.0f)
	{
		const FName Name = MakeUniqueObjectName(
			GetTransientPackage(),
			UPaperFlipbook::StaticClass(),
			FName(BaseName));
		UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(
			GetTransientPackage(), Name, RF_Transactional);
		if (KeyFrames > 0)
		{
			FScopedFlipbookMutator Mutator(Flipbook);
			Mutator.FramesPerSecond = FramesPerSecond;
			for (int32 Index = 0; Index < KeyFrames; ++Index)
			{
				FPaperFlipbookKeyFrame Frame;
				Frame.FrameRun = FrameRun;
				Mutator.KeyFrames.Add(Frame);
			}
		}
		return Flipbook;
	}

	FPaper2DPlusEffectProfileEntry& EffectWorkspace_AddEntry(
		UPaper2DPlusEffectProfileAsset* Asset,
		UPaperFlipbook* Flipbook,
		FGameplayTag Type = FGameplayTag(),
		FGameplayTag Descriptor = FGameplayTag())
	{
		FPaper2DPlusEffectProfileEntry& Entry = Asset->Effects.AddDefaulted_GetRef();
		Entry.EffectFlipbook = Flipbook;
		Entry.TypeTag = Type;
		if (Descriptor.IsValid())
		{
			Entry.DescriptorTags.AddTag(Descriptor);
		}
		return Entry;
	}

	TSharedRef<FEffectProfileEditorModel> EffectWorkspace_MakeModel(
		UPaper2DPlusEffectProfileAsset* Asset,
		bool bRestoreSelection = false)
	{
		TSharedRef<FEffectProfileEditorModel> Model = MakeShared<FEffectProfileEditorModel>();
		Model->Initialize(Asset, bRestoreSelection);
		return Model;
	}

	FSoftObjectPath EffectWorkspace_Path(const UPaperFlipbook* Flipbook)
	{
		return FSoftObjectPath(Flipbook);
	}

	void EffectWorkspace_ResetTransactions(const TCHAR* Label)
	{
		if (GEditor)
		{
			GEditor->ResetTransaction(FText::FromString(Label));
		}
	}

	FKeyEvent EffectWorkspace_MakeKeyEvent(const FKey& Key)
	{
		return FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0);
	}

	/** Keeps automation input isolated from whichever editor field was focused before the test. */
	class FEffectWorkspaceScopedKeyboardFocus
	{
	public:
		FEffectWorkspaceScopedKeyboardFocus()
		{
			if (FSlateApplication::IsInitialized())
			{
				PreviousFocus = FSlateApplication::Get().GetKeyboardFocusedWidget();
			}
		}

		~FEffectWorkspaceScopedKeyboardFocus()
		{
			if (!FSlateApplication::IsInitialized())
			{
				return;
			}

			if (const TSharedPtr<SWidget> FocusToRestore = PreviousFocus.Pin())
			{
				FSlateApplication::Get().SetKeyboardFocus(FocusToRestore, EFocusCause::SetDirectly);
			}
			else
			{
				FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
			}
		}

		void ClearForDirectDispatch() const
		{
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
			}
		}

	private:
		TWeakPtr<SWidget> PreviousFocus;
	};

	/** Render-capable integration host for real focus, routed input, and post-layout scrolling. */
	class FEffectWorkspaceScopedSlateWindow
	{
	public:
		explicit FEffectWorkspaceScopedSlateWindow(const TSharedRef<SWidget>& Content)
		{
			if (!FSlateApplication::IsInitialized() || !FApp::CanEverRender())
			{
				return;
			}

			Window = SNew(SWindow)
				.Title(FText::FromString(TEXT("Paper2DPlus Effect Preview Input Test")))
				.ClientSize(FVector2D(360.0f, 320.0f))
				.FocusWhenFirstShown(false)
				.SupportsMaximize(false)
				.SupportsMinimize(false)
				[
					Content
				];
			FSlateApplication::Get().AddWindow(Window.ToSharedRef(), true);
			TickWidgets();
		}

		~FEffectWorkspaceScopedSlateWindow()
		{
			if (Window.IsValid() && FSlateApplication::IsInitialized())
			{
				Window->RequestDestroyWindow();
				TickWidgets();
			}
		}

		bool IsValid() const { return Window.IsValid(); }

		void TickWidgets(int32 TickCount = 1) const
		{
			if (!FSlateApplication::IsInitialized())
			{
				return;
			}
			for (int32 TickIndex = 0; TickIndex < TickCount; ++TickIndex)
			{
				FSlateApplication::Get().Tick(ESlateTickType::TimeAndWidgets);
			}
		}

	private:
		TSharedPtr<SWindow> Window;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectWorkspaceLayoutStructureTest,
	"Paper2DPlus.EffectProfile.Workspace.DefaultLayoutIsCharacterGrammar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectWorkspaceLayoutStructureTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FTabManager::FLayout> Layout =
		FEffectProfileAssetEditorToolkit::BuildDefaultLayout();
	const TSharedRef<FJsonObject> Root = Layout->ToJson();
	TestEqual(TEXT("Layout uses the versioned workspace id"),
		Root->GetStringField(TEXT("Name")),
		FEffectProfileAssetEditorToolkit::WorkspaceLayoutId.ToString());
	const TArray<TSharedPtr<FJsonValue>>& Areas = Root->GetArrayField(TEXT("Areas"));
	if (!TestEqual(TEXT("Layout has one primary area"), Areas.Num(), 1))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Area = Areas[0]->AsObject();
	if (!TestNotNull(TEXT("Primary area serializes"), Area.Get()))
	{
		return false;
	}
	TestEqual(TEXT("Primary area is horizontal"),
		Area->GetStringField(TEXT("Orientation")), FString(TEXT("Orient_Horizontal")));
	const TArray<TSharedPtr<FJsonValue>>& Columns = Area->GetArrayField(TEXT("Nodes"));
	if (!TestEqual(TEXT("Character grammar has exactly three columns"), Columns.Num(), 3))
	{
		return false;
	}

	auto CheckColumn = [this, &Columns](
		int32 ColumnIndex,
		float ExpectedCoefficient,
		const TArray<TPair<FName, FString>>& ExpectedTabs,
		FName ExpectedForeground) -> bool
	{
		const TSharedPtr<FJsonObject> Column = Columns[ColumnIndex]->AsObject();
		if (!TestNotNull(*FString::Printf(TEXT("Column %d serializes"), ColumnIndex), Column.Get()))
		{
			return false;
		}
		TestEqual(*FString::Printf(TEXT("Column %d is a stack"), ColumnIndex),
			Column->GetStringField(TEXT("Type")), FString(TEXT("Stack")));
		TestTrue(*FString::Printf(TEXT("Column %d keeps its authored ratio"), ColumnIndex),
			FMath::IsNearlyEqual(
				static_cast<float>(Column->GetNumberField(TEXT("SizeCoefficient"))),
				ExpectedCoefficient));
		const TArray<TSharedPtr<FJsonValue>>& Tabs = Column->GetArrayField(TEXT("Tabs"));
		if (!TestEqual(*FString::Printf(TEXT("Column %d has the expected tab count"), ColumnIndex),
			Tabs.Num(), ExpectedTabs.Num()))
		{
			return false;
		}
		for (int32 TabIndex = 0; TabIndex < ExpectedTabs.Num(); ++TabIndex)
		{
			const TSharedPtr<FJsonObject> Tab = Tabs[TabIndex]->AsObject();
			if (!TestNotNull(*FString::Printf(TEXT("Column %d tab %d serializes"), ColumnIndex, TabIndex), Tab.Get()))
			{
				return false;
			}
			TestEqual(*FString::Printf(TEXT("Column %d tab %d id"), ColumnIndex, TabIndex),
				Tab->GetStringField(TEXT("TabId")), ExpectedTabs[TabIndex].Key.ToString());
			TestEqual(*FString::Printf(TEXT("Column %d tab %d state"), ColumnIndex, TabIndex),
				Tab->GetStringField(TEXT("TabState")), ExpectedTabs[TabIndex].Value);
		}
		TestEqual(*FString::Printf(TEXT("Column %d foreground tab"), ColumnIndex),
			Column->GetStringField(TEXT("ForegroundTab")), ExpectedForeground.ToString());
		return true;
	};

	// These track BuildDefaultLayout's authored coefficients: v5 narrowed the Library from 0.36 to
	// 0.24 and gave the reclaimed width to the Preview (0.38 -> 0.50). The right column stayed 0.26.
	if (!CheckColumn(0, 0.24f,
		{ { FEffectProfileAssetEditorToolkit::LibraryTabId, TEXT("OpenedTab") } },
		FEffectProfileAssetEditorToolkit::LibraryTabId))
	{
		return false;
	}
	if (!CheckColumn(1, 0.50f,
		{ { FEffectProfileAssetEditorToolkit::PreviewTabId, TEXT("OpenedTab") } },
		FEffectProfileAssetEditorToolkit::PreviewTabId))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> RightColumn = Columns[2]->AsObject();
	if (!TestNotNull(TEXT("Right completion column serializes"), RightColumn.Get()))
	{
		return false;
	}
	TestEqual(TEXT("Right column is vertically split"),
		RightColumn->GetStringField(TEXT("Type")), FString(TEXT("Splitter")));
	TestTrue(TEXT("Right column keeps its authored ratio"),
		FMath::IsNearlyEqual(
			static_cast<float>(RightColumn->GetNumberField(TEXT("SizeCoefficient"))),
			0.26f));
	TestEqual(TEXT("Right column is vertical"),
		RightColumn->GetStringField(TEXT("Orientation")), FString(TEXT("Orient_Vertical")));
	const TArray<TSharedPtr<FJsonValue>>& RightStacks = RightColumn->GetArrayField(TEXT("Nodes"));
	if (!TestEqual(TEXT("Right column has Details and Completion stacks"), RightStacks.Num(), 2))
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
	const TSharedPtr<FJsonObject> DetailsStack = RightStacks[0]->AsObject();
	const TSharedPtr<FJsonObject> CompletionStack = RightStacks[1]->AsObject();
	TestTrue(TEXT("Upper right stack retains Details"),
		StackContainsTab(DetailsStack, FEffectProfileAssetEditorToolkit::DetailsTabId));
	TestTrue(TEXT("Upper right stack retains closed Advanced Details"),
		StackContainsTab(DetailsStack, FEffectProfileAssetEditorToolkit::AdvancedDetailsTabId));
	TestTrue(TEXT("Lower right stack contains Completion"),
		StackContainsTab(CompletionStack, FEffectProfileAssetEditorToolkit::CompletionTabId));
	TestTrue(TEXT("Lower right stack contains Related Profiles"),
		StackContainsTab(CompletionStack, FEffectProfileAssetEditorToolkit::RelatedProfilesTabId));
	TestFalse(TEXT("Validation is modeless rather than a permanent dock tab"),
		Layout->ToString().Contains(TEXT("Validation"), ESearchCase::IgnoreCase));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectWorkspaceBulkIntakeUndoTest,
	"Paper2DPlus.EffectProfile.Workspace.BulkIntakeUniqueSingleUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectWorkspaceBulkIntakeUndoTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("GEditor is available"), GEditor)) return false;
	EffectWorkspace_ResetTransactions(TEXT("Effect bulk intake start"));
	UPaper2DPlusEffectProfileAsset* Asset = EffectWorkspace_MakeAsset(TEXT("FXBulkIntake"));
	UPaperFlipbook* A = EffectWorkspace_MakeFlipbook(TEXT("FX_Bulk_A"));
	UPaperFlipbook* B = EffectWorkspace_MakeFlipbook(TEXT("FX_Bulk_B"));
	TSharedRef<FEffectProfileEditorModel> Model = EffectWorkspace_MakeModel(Asset);

	const FEffectProfileIntakeResult Result = Model->AddFlipbooks({ A, B, A });
	TestEqual(TEXT("Two unique flipbooks added"), Result.AddedCount, 2);
	TestEqual(TEXT("Duplicate inside one batch reported"), Result.DuplicateCount, 1);
	TestEqual(TEXT("One command creates two rows"), Asset->Effects.Num(), 2);
	TestEqual(TEXT("One mutation command recorded"), Model->GetMutationCountForTests(), 1);
	TestTrue(TEXT("First accepted flipbook becomes selection"), Model->GetSelectedFlipbook() == A);

	TestTrue(TEXT("One undo removes the entire bulk command"), GEditor->UndoTransaction(true));
	Model->RefreshFromAsset();
	TestEqual(TEXT("Bulk rows both removed by one undo"), Asset->Effects.Num(), 0);
	TestTrue(TEXT("One redo restores the entire bulk command"), GEditor->RedoTransaction());
	Model->RefreshFromAsset();
	TestEqual(TEXT("Bulk rows both restored by one redo"), Asset->Effects.Num(), 2);
	TestTrue(TEXT("Path selection restores with redo"), Model->GetSelectedFlipbook() == A);
	EffectWorkspace_ResetTransactions(TEXT("Effect bulk intake end"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectWorkspaceMixedDropTest,
	"Paper2DPlus.EffectProfile.Workspace.MixedContentBrowserDropReportsAllOutcomes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectWorkspaceMixedDropTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusEffectProfileAsset* Asset = EffectWorkspace_MakeAsset(TEXT("FXMixedDrop"));
	UPaperFlipbook* Existing = EffectWorkspace_MakeFlipbook(TEXT("FX_Drop_Existing"));
	UPaperFlipbook* NewFlipbook = EffectWorkspace_MakeFlipbook(TEXT("FX_Drop_New"));
	UTexture2D* WrongType = NewObject<UTexture2D>(
		GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("FX_Drop_Texture")));
	TSharedRef<FEffectProfileEditorModel> Model = EffectWorkspace_MakeModel(Asset);
	Model->AddFlipbooks({ Existing });
	const FSoftObjectPath SelectedBefore = Model->GetSelectedEffectPath();

	const FEffectProfileIntakeResult Result = Model->AddAssetData(
		{ FAssetData(Existing), FAssetData(NewFlipbook), FAssetData(WrongType) });
	TestEqual(TEXT("Valid new flipbook accepted"), Result.AddedCount, 1);
	TestEqual(TEXT("Existing flipbook explicitly reported duplicate"), Result.DuplicateCount, 1);
	TestEqual(TEXT("Unrelated asset explicitly reported incompatible"), Result.RejectedCount, 1);
	TestEqual(TEXT("Only the valid unique row mutates membership"), Asset->Effects.Num(), 2);
	TestTrue(TEXT("Mixed drop does not silently replace selection"),
		Model->GetSelectedEffectPath() == SelectedBefore);
	TestTrue(TEXT("Visible report names all three outcome counts"),
		Result.BuildSummary().ToString().Contains(TEXT("Added 1"))
		&& Result.BuildSummary().ToString().Contains(TEXT("duplicate"))
		&& Result.BuildSummary().ToString().Contains(TEXT("Rejected 1")));
	TestTrue(TEXT("Visible report identifies every skipped asset"),
		Result.BuildSummary().ToString().Contains(Existing->GetName())
		&& Result.BuildSummary().ToString().Contains(WrongType->GetName()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectWorkspaceSearchGroupingFilterTest,
	"Paper2DPlus.EffectProfile.Workspace.SearchTypeGroupingDescriptorFilterNoResults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectWorkspaceSearchGroupingFilterTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusEffectProfileAsset* Asset = EffectWorkspace_MakeAsset(TEXT("FXSearch"));
	for (int32 Index = 0; Index < 200; ++Index)
	{
		UPaperFlipbook* Flipbook = EffectWorkspace_MakeFlipbook(
			*FString::Printf(TEXT("FX_Search_%03d"), Index));
		FPaper2DPlusEffectProfileEntry& Entry = EffectWorkspace_AddEntry(
			Asset,
			Flipbook,
			Index % 2 == 0 ? Paper2DPlusEffectTags::Type_Impact.GetTag()
				: Paper2DPlusEffectTags::Type_Projectile.GetTag(),
			Index % 2 == 0 ? Paper2DPlusEffectTags::Descriptor_Fire.GetTag()
				: Paper2DPlusEffectTags::Descriptor_Poison.GetTag());
		Entry.DisplayLabel = FText::FromString(FString::Printf(TEXT("Visual %03d"), Index));
	}
	TSharedRef<FEffectProfileEditorModel> Model = EffectWorkspace_MakeModel(Asset);
	TSharedPtr<IProfileItemPickerSource> Source =
		StaticCastSharedRef<IProfileItemPickerSource>(Model);
	FProfilePickerResultModel Results(Source);
	TestEqual(TEXT("All 200 rows project"), Results.GetMatchingItems().Num(), 200);
	TestTrue(TEXT("Type tags create explicit Impact and Projectile group headers"),
		Results.GetRows().ContainsByPredicate([](const TSharedPtr<FProfilePickerDisplayRow>& Row)
		{
			return Row.IsValid() && Row->Kind == FProfilePickerDisplayRow::EKind::Header
				&& Row->Header.ToString() == TEXT("Impact");
		})
		&& Results.GetRows().ContainsByPredicate([](const TSharedPtr<FProfilePickerDisplayRow>& Row)
		{
			return Row.IsValid() && Row->Kind == FProfilePickerDisplayRow::EKind::Header
				&& Row->Header.ToString() == TEXT("Projectile");
		}));

	Results.SetQuery(TEXT("Visual 042"));
	TestEqual(TEXT("Display label search is deterministic"), Results.GetMatchingItems().Num(), 1);
	Results.SetQuery(TEXT("Impact"));
	TestEqual(TEXT("Primary type leaf participates in search"), Results.GetMatchingItems().Num(), 100);
	Results.SetQuery(TEXT("Paper2DPlus.Effect.Descriptor.Poison"));
	TestEqual(TEXT("Full descriptor path participates in search"), Results.GetMatchingItems().Num(), 100);

	FGameplayTagContainer FireFilter;
	FireFilter.AddTag(Paper2DPlusEffectTags::Descriptor_Fire.GetTag());
	Model->SetDescriptorFilter(FireFilter);
	Results.Refresh();
	Results.SetQuery(TEXT(""));
	TestEqual(TEXT("Descriptor filter requires selected descriptor"), Results.GetMatchingItems().Num(), 100);
	Results.SetQuery(TEXT("definitely-no-effect"));
	TestEqual(TEXT("No-results state has zero matches"), Results.GetMatchingItems().Num(), 0);
	TestTrue(TEXT("Filtering and search never mutate membership"), Asset->Effects.Num() == 200);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectWorkspaceStableSelectionTest,
	"Paper2DPlus.EffectProfile.Workspace.SelectionSurvivesReorderRelabelFilterRenameUndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectWorkspaceStableSelectionTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("GEditor is available"), GEditor)) return false;
	EffectWorkspace_ResetTransactions(TEXT("Effect stable selection start"));
	UPaper2DPlusEffectProfileAsset* Asset = EffectWorkspace_MakeAsset(TEXT("FXStableSelection"));
	UPaperFlipbook* A = EffectWorkspace_MakeFlipbook(TEXT("FX_Select_A"));
	UPaperFlipbook* B = EffectWorkspace_MakeFlipbook(TEXT("FX_Select_B"));
	UPaperFlipbook* C = EffectWorkspace_MakeFlipbook(TEXT("FX_Select_C"));
	EffectWorkspace_AddEntry(Asset, A, Paper2DPlusEffectTags::Type_Impact.GetTag(), Paper2DPlusEffectTags::Descriptor_Fire.GetTag());
	EffectWorkspace_AddEntry(Asset, B, Paper2DPlusEffectTags::Type_Projectile.GetTag(), Paper2DPlusEffectTags::Descriptor_Poison.GetTag());
	EffectWorkspace_AddEntry(Asset, C, Paper2DPlusEffectTags::Type_Aura.GetTag(), Paper2DPlusEffectTags::Descriptor_Healing.GetTag());
	TSharedRef<FEffectProfileEditorModel> Model = EffectWorkspace_MakeModel(Asset);
	Model->SelectEffectPath(EffectWorkspace_Path(B));
	const FSoftObjectPath OriginalPath = Model->GetSelectedEffectPath();

	TestTrue(TEXT("Selected row moves upward"), Model->MoveSelected(-1));
	TestTrue(TEXT("Selection follows object through reorder"), Model->GetSelectedFlipbook() == B);
	TestTrue(TEXT("Display label edit commits"), Model->SetSelectedDisplayLabel(FText::FromString(TEXT("Venom Bolt"))));
	TestTrue(TEXT("Selection follows object through relabel"), Model->GetSelectedFlipbook() == B);
	TestTrue(TEXT("Undo relabel succeeds"), GEditor->UndoTransaction(true));
	Model->RefreshFromAsset();
	TestTrue(TEXT("Selection survives undo"), Model->GetSelectedFlipbook() == B);
	TestTrue(TEXT("Redo relabel succeeds"), GEditor->RedoTransaction());
	Model->RefreshFromAsset();
	TestTrue(TEXT("Selection survives redo"), Model->GetSelectedFlipbook() == B);

	FGameplayTagContainer FireOnly;
	FireOnly.AddTag(Paper2DPlusEffectTags::Descriptor_Fire.GetTag());
	Model->SetDescriptorFilter(FireOnly);
	TestEqual(TEXT("Filter hides selected Poison row from results"), Model->GetVisibleEntryCount(), 1);
	TestTrue(TEXT("Hidden selection remains the same effect"), Model->GetSelectedFlipbook() == B);
	Model->ClearDescriptorFilter();

	const FName Renamed = MakeUniqueObjectName(GetTransientPackage(), UPaperFlipbook::StaticClass(), TEXT("FX_Select_B_Renamed"));
	TestTrue(TEXT("Source flipbook rename succeeds"), B->Rename(*Renamed.ToString(), nullptr, REN_DontCreateRedirectors | REN_NonTransactional));
	Model->RefreshFromAsset();
	TestTrue(TEXT("Selection follows live object through rename"), Model->GetSelectedFlipbook() == B);
	TestTrue(TEXT("Path identity updates after rename"),
		Model->GetSelectedEffectPath() != OriginalPath
		&& Model->GetSelectedEffectPath() == EffectWorkspace_Path(B));

	Asset->Effects.Swap(0, 2); // external refresh, deliberately outside the command model
	Model->RefreshFromAsset();
	TestTrue(TEXT("External reorder preserves selected object"), Model->GetSelectedFlipbook() == B);
	EffectWorkspace_ResetTransactions(TEXT("Effect stable selection end"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectWorkspaceWorldlessPlaybackTest,
	"Paper2DPlus.EffectProfile.Workspace.WorldlessPlaybackNullOneAndManyFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectWorkspaceWorldlessPlaybackTest::RunTest(const FString& Parameters)
{
	FEffectProfilePlaybackState Playback;
	Playback.SetFlipbook(nullptr);
	Playback.Play();
	Playback.Tick(1.0f);
	TestFalse(TEXT("Null playback never starts"), Playback.IsPlaying());
	TestEqual(TEXT("Null playback reports zero frames"), Playback.GetNumKeyFrames(), 0);

	UPaperFlipbook* OneFrame = EffectWorkspace_MakeFlipbook(TEXT("FX_OneFrame"), 1);
	Playback.SetFlipbook(OneFrame);
	Playback.SetLooping(false);
	Playback.Play();
	Playback.Tick(0.2f);
	TestEqual(TEXT("One-frame preview stays on frame zero"), Playback.GetCurrentKeyFrame(), 0);
	TestFalse(TEXT("Non-looping one-frame preview completes"), Playback.IsPlaying());

	UPaperFlipbook* ManyFrames = EffectWorkspace_MakeFlipbook(TEXT("FX_ManyFrames"), 3, 1, 10.0f);
	Playback.SetFlipbook(ManyFrames);
	Playback.SetLooping(false);
	Playback.Play();
	Playback.Tick(0.11f);
	TestEqual(TEXT("Multi-frame playback advances by FPS"), Playback.GetCurrentKeyFrame(), 1);
	Playback.Tick(0.30f);
	TestEqual(TEXT("Non-looping playback stops on final key frame"), Playback.GetCurrentKeyFrame(), 2);
	TestFalse(TEXT("Non-looping playback stops"), Playback.IsPlaying());

	Playback.SetLooping(true);
	Playback.SeekKeyFrame(2);
	Playback.Play();
	Playback.Tick(0.11f);
	TestEqual(TEXT("Looping playback wraps to first key frame"), Playback.GetCurrentKeyFrame(), 0);
	TestTrue(TEXT("Looping playback remains active"), Playback.IsPlaying());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectWorkspaceAtlasPreviewTest,
	"Paper2DPlus.EffectProfile.Workspace.PreviewUsesSlateAtlasCoordinatesAndClearsStaleBrush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectWorkspaceAtlasPreviewTest::RunTest(const FString& Parameters)
{
	UTexture2D* AtlasTexture = UTexture2D::CreateTransient(80, 40);
	if (!TestNotNull(TEXT("Transient atlas texture created"), AtlasTexture))
	{
		return false;
	}

	const FSlateAtlasData AtlasData(
		AtlasTexture,
		FVector2D(0.25f, 0.50f),
		FVector2D(0.50f, 0.25f));
	FSpriteSlateRegion Region;
	TestTrue(TEXT("Atlas data resolves"),
		FSpriteSlateAtlasUtils::ResolveAtlasData(AtlasData, Region));
	TestTrue(TEXT("Resolved region keeps the atlas resource"), Region.Texture == AtlasTexture);
	TestTrue(TEXT("Resolved region keeps baked atlas UV minimum"),
		Region.UVMin.Equals(FVector2D(0.25f, 0.50f)));
	TestTrue(TEXT("Resolved region derives baked atlas UV maximum"),
		Region.UVMax.Equals(FVector2D(0.75f, 0.75f)));
	TestTrue(TEXT("Resolved region derives atlas-cell pixel size"),
		Region.PixelSize.Equals(FVector2D(40.0f, 10.0f)));

	FSlateBrush Brush;
	Brush.SetResourceObject(AtlasTexture);
	TestFalse(TEXT("Invalid sprite cannot configure a brush"),
		FSpriteSlateAtlasUtils::ConfigureBrush(nullptr, Brush));
	TestNull(TEXT("Failed refresh clears the previous brush resource"), Brush.GetResourceObject());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectWorkspaceForceRefreshTest,
	"Paper2DPlus.EffectProfile.Workspace.PreviewForceRefreshesSameSelectionPropertyAndReimport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectWorkspaceForceRefreshTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusEffectProfileAsset* Asset = EffectWorkspace_MakeAsset(TEXT("FXForceRefresh"));
	UPaperFlipbook* Flipbook = EffectWorkspace_MakeFlipbook(TEXT("FX_Refresh_Source"), 2);
	auto MakeSourceTexture = [](int32 Size)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8);
		if (Texture)
		{
			Texture->Source.Init(Size, Size, 1, 1, TSF_BGRA8);
		}
		return Texture;
	};
	UTexture2D* SourceTextureA = MakeSourceTexture(16);
	if (!TestNotNull(TEXT("First source texture created"), SourceTextureA)) return false;
	UPaperSprite* Sprite = NewObject<UPaperSprite>(Flipbook);
	FSpriteAssetInitParameters SpriteInit;
	SpriteInit.Texture = SourceTextureA;
	SpriteInit.Offset = FIntPoint::ZeroValue;
	SpriteInit.Dimension = FIntPoint(16, 16);
	SpriteInit.SetPixelsPerUnrealUnit(1.0f);
	Sprite->InitializeSprite(SpriteInit);
	{
		FScopedFlipbookMutator Mutator(Flipbook);
		for (FPaperFlipbookKeyFrame& KeyFrame : Mutator.KeyFrames)
		{
			KeyFrame.Sprite = Sprite;
		}
	}
	EffectWorkspace_AddEntry(Asset, Flipbook);
	TSharedRef<FEffectProfileEditorModel> Model = EffectWorkspace_MakeModel(Asset);
	Model->SelectEffectPath(EffectWorkspace_Path(Flipbook));
	TSharedRef<SEffectProfilePreviewPanel> Preview =
		SNew(SEffectProfilePreviewPanel).Model(Model);
	TestTrue(TEXT("Preview brush resolves the selected sprite source"),
		Preview->GetPreviewBrushResourceForTests() == SourceTextureA);

	int32 PreviewRevision = Preview->GetPreviewRefreshRevisionForTests();
	int32 StripRevision = Preview->GetFrameStripRefreshRevisionForTests();
	Model->RefreshFromAsset();
	TestTrue(TEXT("Same-selection model refresh rebuilds the large canvas"),
		Preview->GetPreviewRefreshRevisionForTests() > PreviewRevision);
	TestTrue(TEXT("Same-selection model refresh rebuilds the frame strip"),
		Preview->GetFrameStripRefreshRevisionForTests() > StripRevision);

	PreviewRevision = Preview->GetPreviewRefreshRevisionForTests();
	StripRevision = Preview->GetFrameStripRefreshRevisionForTests();
	UTexture2D* SourceTextureB = MakeSourceTexture(32);
	if (!TestNotNull(TEXT("Replacement source texture created"), SourceTextureB)) return false;
	SpriteInit.Texture = SourceTextureB;
	SpriteInit.Dimension = FIntPoint(32, 32);
	Sprite->InitializeSprite(SpriteInit);
	FPropertyChangedEvent PropertyEvent(nullptr, EPropertyChangeType::ValueSet);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Sprite, PropertyEvent);
	TestTrue(TEXT("Same selected sprite property change force-refreshes the canvas"),
		Preview->GetPreviewRefreshRevisionForTests() > PreviewRevision);
	TestTrue(TEXT("Same selected sprite property change force-refreshes the strip"),
		Preview->GetFrameStripRefreshRevisionForTests() > StripRevision);
	TestTrue(TEXT("Same-sprite refresh replaces the stale brush resource"),
		Preview->GetPreviewBrushResourceForTests() == SourceTextureB);

	UImportSubsystem* ImportSubsystem = GEditor
		? GEditor->GetEditorSubsystem<UImportSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("Import subsystem is available"), ImportSubsystem))
	{
		return false;
	}
	PreviewRevision = Preview->GetPreviewRefreshRevisionForTests();
	StripRevision = Preview->GetFrameStripRefreshRevisionForTests();
	ImportSubsystem->BroadcastAssetReimport(SourceTextureB);
	TestTrue(TEXT("Selected sprite source-sheet reimport force-refreshes the canvas"),
		Preview->GetPreviewRefreshRevisionForTests() > PreviewRevision);
	TestTrue(TEXT("Selected sprite source-sheet reimport force-refreshes the strip"),
		Preview->GetFrameStripRefreshRevisionForTests() > StripRevision);
	TestTrue(TEXT("Refresh never changes the stable selected source"),
		Model->GetSelectedFlipbook() == Flipbook);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectWorkspaceSpacePlaybackTest,
	"Paper2DPlus.EffectProfile.Workspace.PreviewSpacePlaybackUsesScopedActiveTimer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectWorkspaceSpacePlaybackTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusEffectProfileAsset* Asset = EffectWorkspace_MakeAsset(TEXT("FXSpacePlayback"));
	UPaperFlipbook* Flipbook = EffectWorkspace_MakeFlipbook(TEXT("FX_Space_Source"), 3);
	EffectWorkspace_AddEntry(Asset, Flipbook);
	TSharedRef<FEffectProfileEditorModel> Model = EffectWorkspace_MakeModel(Asset);
	Model->SelectEffectPath(EffectWorkspace_Path(Flipbook));
	TSharedRef<SEffectProfilePreviewPanel> Preview =
		SNew(SEffectProfilePreviewPanel).Model(Model);

	TestFalse(TEXT("Preview starts paused"), Preview->GetPlaybackStateForTests().IsPlaying());
	TestFalse(TEXT("Paused preview owns no timer"), Preview->HasPlaybackTimerForTests());
	TestEqual(TEXT("Frame strip has one clickable cell per key frame"),
		Preview->GetFrameStripFrameCountForTests(), 3);
	TestEqual(TEXT("Preview has no draggable transport slider"),
		Preview->CountDescendantWidgetsForTests(TEXT("SSlider")), 0);
	TestEqual(TEXT("Open Flipbook is the only visible Preview button"),
		Preview->CountDescendantWidgetsForTests(TEXT("SButton")), 1);
	FEffectWorkspaceScopedKeyboardFocus FocusGuard;
	FocusGuard.ClearForDirectDispatch();
	TestTrue(TEXT("Space is handled by the Preview"),
		Preview->OnKeyDown(FGeometry(), EffectWorkspace_MakeKeyEvent(EKeys::SpaceBar)).IsEventHandled());
	TestTrue(TEXT("Space seam starts playback"), Preview->GetPlaybackStateForTests().IsPlaying());
	TestTrue(TEXT("Playing preview owns one scoped active timer"), Preview->HasPlaybackTimerForTests());
	{
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.KeyFrames.SetNum(1);
	}
	Preview->ForceRefreshForTests();
	TestEqual(TEXT("Reimport shrink refreshes the strip topology"),
		Preview->GetFrameStripFrameCountForTests(), 1);
	TestFalse(TEXT("A source shrunk to one frame pauses playback"),
		Preview->GetPlaybackStateForTests().IsPlaying());
	TestFalse(TEXT("A source shrunk to one frame retires its active timer"),
		Preview->HasPlaybackTimerForTests());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectWorkspaceKeyboardFrameNavigationTest,
	"Paper2DPlus.EffectProfile.Workspace.PreviewKeyboardFrameNavigationIsVisibleAndAccessible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectWorkspaceKeyboardFrameNavigationTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusEffectProfileAsset* Asset = EffectWorkspace_MakeAsset(TEXT("FXKeyboardNavigation"));
	UPaperFlipbook* Flipbook = EffectWorkspace_MakeFlipbook(TEXT("FX_Keyboard_Source"), 12);
	EffectWorkspace_AddEntry(Asset, Flipbook);
	TSharedRef<FEffectProfileEditorModel> Model = EffectWorkspace_MakeModel(Asset);
	Model->SelectEffectPath(EffectWorkspace_Path(Flipbook));
	TSharedRef<SEffectProfilePreviewPanel> Preview =
		SNew(SEffectProfilePreviewPanel).Model(Model);
	FEffectWorkspaceScopedKeyboardFocus FocusGuard;
	FEffectWorkspaceScopedSlateWindow SlateWindow(Preview);
	const bool bUsesRealSlateRouting = SlateWindow.IsValid();
	if (bUsesRealSlateRouting)
	{
		const int32 FocusRevisionBefore = Preview->GetFocusChangeRevisionForTests();
		TestTrue(TEXT("Live Preview accepts real Slate keyboard focus"),
			FSlateApplication::Get().SetKeyboardFocus(Preview, EFocusCause::SetDirectly));
		SlateWindow.TickWidgets();
		TestTrue(TEXT("Live Preview owns Slate keyboard focus"), Preview->HasKeyboardFocus());
		TestTrue(TEXT("Real focus transition invalidates the focus indicator"),
			Preview->GetFocusChangeRevisionForTests() > FocusRevisionBefore);
		TestTrue(TEXT("Keyboard focus indicator is visible for the focused Preview"),
			Preview->IsKeyboardFocusIndicatorVisibleForTests());
	}
	else
	{
		FocusGuard.ClearForDirectDispatch();
		AddInfo(TEXT("Null-RHI/headless run uses the direct key semantic fallback; render-capable automation exercises real Slate focus, routed input, and scrolling."));
	}
	auto DispatchPreviewKey = [&Preview, bUsesRealSlateRouting](const FKey& Key)
	{
		return bUsesRealSlateRouting
			? FSlateApplication::Get().ProcessKeyDownEvent(EffectWorkspace_MakeKeyEvent(Key))
			: Preview->OnKeyDown(FGeometry(), EffectWorkspace_MakeKeyEvent(Key)).IsEventHandled();
	};

	TestTrue(TEXT("Focusable Preview constructs a visible keyboard-focus indicator"),
		Preview->HasKeyboardFocusIndicatorForTests());
	TestTrue(TEXT("Accessible Preview state names the initial one-based frame"),
		Preview->GetAccessibleSummaryForTests().ToString().Contains(TEXT("Frame 1 of 12")));
	TestTrue(TEXT("Accessible Preview state names the selected effect"),
		Preview->GetAccessibleSummaryForTests().ToString().Contains(TEXT("FX_Keyboard_Source")));
#if WITH_ACCESSIBILITY
	TestTrue(TEXT("Focusable Preview publishes its live state through Slate accessibility"),
		Preview->GetAccessibleText().ToString().Contains(TEXT("Frame 1 of 12")));
	const int32 AnnouncementRevisionBefore = Preview->GetAccessibleAnnouncementRevisionForTests();
#endif

	TestTrue(TEXT("Space is routed through Slate to the focused Preview"),
		DispatchPreviewKey(EKeys::SpaceBar));
	TestTrue(TEXT("Navigation setup starts playback"), Preview->GetPlaybackStateForTests().IsPlaying());
	TestTrue(TEXT("Navigation setup owns its playback timer"), Preview->HasPlaybackTimerForTests());
	TestTrue(TEXT("Accessible Preview state exposes playback state"),
		Preview->GetAccessibleSummaryForTests().ToString().Contains(TEXT("Playing")));
#if WITH_ACCESSIBILITY
	if (bUsesRealSlateRouting)
	{
		TestTrue(TEXT("Focused Space playback raises an accessibility state notification"),
			Preview->GetAccessibleAnnouncementRevisionForTests() > AnnouncementRevisionBefore);
	}
#endif
	TestTrue(TEXT("Right Arrow is handled by the Preview"),
		DispatchPreviewKey(EKeys::Right));
	TestEqual(TEXT("Right Arrow selects the next frame"),
		Preview->GetPlaybackStateForTests().GetCurrentKeyFrame(), 1);
	TestFalse(TEXT("Keyboard seeking pauses playback"), Preview->GetPlaybackStateForTests().IsPlaying());
	TestFalse(TEXT("Keyboard seeking retires the playback timer"), Preview->HasPlaybackTimerForTests());
	TestTrue(TEXT("Accessible Preview state exposes paused state after seeking"),
		Preview->GetAccessibleSummaryForTests().ToString().Contains(TEXT("Paused")));

	TestTrue(TEXT("End is handled by the Preview"),
		DispatchPreviewKey(EKeys::End));
	TestEqual(TEXT("End selects the last frame"),
		Preview->GetPlaybackStateForTests().GetCurrentKeyFrame(), 11);
	TestTrue(TEXT("Accessible Preview state follows keyboard selection"),
		Preview->GetAccessibleSummaryForTests().ToString().Contains(TEXT("Frame 12 of 12")));
	if (bUsesRealSlateRouting)
	{
		SlateWindow.TickWidgets(2);
		TestTrue(TEXT("Routed End scrolls the selected frame into the live viewport"),
			Preview->GetFrameStripScrollOffsetForTests() > 0.0f);
	}
	TestTrue(TEXT("Right Arrow remains handled at the last-frame boundary"),
		DispatchPreviewKey(EKeys::Right));
	TestEqual(TEXT("Right Arrow clamps at the last frame"),
		Preview->GetPlaybackStateForTests().GetCurrentKeyFrame(), 11);
	TestTrue(TEXT("Left Arrow is handled by the Preview"),
		DispatchPreviewKey(EKeys::Left));
	TestEqual(TEXT("Left Arrow selects the previous frame"),
		Preview->GetPlaybackStateForTests().GetCurrentKeyFrame(), 10);
	TestTrue(TEXT("Home is handled by the Preview"),
		DispatchPreviewKey(EKeys::Home));
	TestEqual(TEXT("Home selects the first frame"),
		Preview->GetPlaybackStateForTests().GetCurrentKeyFrame(), 0);

	TestEqual(TEXT("Keyboard navigation adds no transport slider"),
		Preview->CountDescendantWidgetsForTests(TEXT("SSlider")), 0);
	TestEqual(TEXT("Keyboard navigation adds no transport button"),
		Preview->CountDescendantWidgetsForTests(TEXT("SButton")), 1);

	UPaper2DPlusEffectProfileAsset* EmptyAsset = EffectWorkspace_MakeAsset(TEXT("FXKeyboardNavigationEmpty"));
	TSharedRef<FEffectProfileEditorModel> EmptyModel = EffectWorkspace_MakeModel(EmptyAsset);
	TSharedRef<SEffectProfilePreviewPanel> EmptyPreview =
		SNew(SEffectProfilePreviewPanel).Model(EmptyModel);
	TestFalse(TEXT("Frame navigation is unhandled when no frame exists"),
		EmptyPreview->OnKeyDown(FGeometry(), EffectWorkspace_MakeKeyEvent(EKeys::Right)).IsEventHandled());
	TestTrue(TEXT("Accessible Preview state explains the no-frame state"),
		EmptyPreview->GetAccessibleSummaryForTests().ToString().Contains(TEXT("No frame is available")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectWorkspaceRemoveUndoIsolationTest,
	"Paper2DPlus.EffectProfile.Workspace.RemoveDoesNotRewriteCueUndoRestoresRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectWorkspaceRemoveUndoIsolationTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("GEditor is available"), GEditor)) return false;
	EffectWorkspace_ResetTransactions(TEXT("Effect remove isolation start"));
	UPaper2DPlusEffectProfileAsset* Asset = EffectWorkspace_MakeAsset(TEXT("FXRemove"));
	UPaperFlipbook* A = EffectWorkspace_MakeFlipbook(TEXT("FX_Remove_A"));
	UPaperFlipbook* B = EffectWorkspace_MakeFlipbook(TEXT("FX_Remove_B"));
	FPaper2DPlusEffectProfileEntry& AEntry = EffectWorkspace_AddEntry(
		Asset, A, Paper2DPlusEffectTags::Type_Impact.GetTag(), Paper2DPlusEffectTags::Descriptor_Fire.GetTag());
	AEntry.DisplayLabel = FText::FromString(TEXT("Impact A"));
	EffectWorkspace_AddEntry(Asset, B, Paper2DPlusEffectTags::Type_Aura.GetTag(), Paper2DPlusEffectTags::Descriptor_Healing.GetTag());
	TSharedRef<FEffectProfileEditorModel> Model = EffectWorkspace_MakeModel(Asset);
	Model->SelectEffectPath(EffectWorkspace_Path(A));
	UPaper2DPlusEditorTestMomentCue* ExternalCue = NewObject<UPaper2DPlusEditorTestMomentCue>(
		GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UPaper2DPlusEditorTestMomentCue::StaticClass(), TEXT("ExternalCue")));
	ExternalCue->EffectArt = A;

	TestTrue(TEXT("Selected membership removes"), Model->RemoveSelected());
	TestEqual(TEXT("Only one library row remains"), Asset->Effects.Num(), 1);
	TestTrue(TEXT("External direct cue is untouched"), ExternalCue->EffectArt == A);
	TestFalse(TEXT("Removed selection resolves to no active row"), Model->HasResolvedSelection());

	TestTrue(TEXT("Undo removal succeeds"), GEditor->UndoTransaction(true));
	Model->RefreshFromAsset();
	TestEqual(TEXT("Undo restores both rows"), Asset->Effects.Num(), 2);
	TestTrue(TEXT("Undo restores original order"), Asset->Effects[0].EffectFlipbook == A && Asset->Effects[1].EffectFlipbook == B);
	TestEqual(TEXT("Undo restores display label"), Asset->Effects[0].DisplayLabel.ToString(), FString(TEXT("Impact A")));
	TestTrue(TEXT("Undo restores type and descriptor metadata"),
		Asset->Effects[0].TypeTag == Paper2DPlusEffectTags::Type_Impact.GetTag()
		&& Asset->Effects[0].DescriptorTags.HasTagExact(Paper2DPlusEffectTags::Descriptor_Fire.GetTag()));
	TestTrue(TEXT("Undo restores the same path selection"), Model->GetSelectedFlipbook() == A);
	TestTrue(TEXT("External cue remains untouched after undo"), ExternalCue->EffectArt == A);
	EffectWorkspace_ResetTransactions(TEXT("Effect remove isolation end"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectWorkspaceLargeVirtualizedNoTickRebuildTest,
	"Paper2DPlus.EffectProfile.Workspace.LargeVirtualizedLibraryPlaybackDoesNotRebuildRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectWorkspaceLargeVirtualizedNoTickRebuildTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusEffectProfileAsset* Asset = EffectWorkspace_MakeAsset(TEXT("FXLarge"));
	UPaperFlipbook* Animated = nullptr;
	for (int32 Index = 0; Index < 200; ++Index)
	{
		UPaperFlipbook* Flipbook = EffectWorkspace_MakeFlipbook(
			*FString::Printf(TEXT("FX_Large_%03d"), Index),
			Index == 0 ? 3 : 0);
		if (Index == 0) Animated = Flipbook;
		EffectWorkspace_AddEntry(Asset, Flipbook, Paper2DPlusEffectTags::Type_Impact.GetTag());
	}
	TSharedRef<FEffectProfileEditorModel> Model = EffectWorkspace_MakeModel(Asset);
	Model->SelectEffectPath(EffectWorkspace_Path(Animated));
	TSharedRef<SEffectProfileLibraryPanel> Library = SNew(SEffectProfileLibraryPanel).Model(Model);
	TSharedRef<SEffectProfilePreviewPanel> Preview = SNew(SEffectProfilePreviewPanel).Model(Model);
	TestEqual(TEXT("Virtualized library exposes 200 searchable rows"), Library->GetResultCountForTests(), 200);
	const int32 Generated = Library->GenerateRowsForViewportForTests(FVector2D(520.0f, 260.0f));
	TestTrue(TEXT("Constrained viewport generates some rows"), Generated > 0);
	TestTrue(TEXT("Constrained viewport does not eagerly construct all rows"), Generated < 200);

	const int32 RefreshesBeforePlayback = Library->GetRefreshCountForTests();
	Preview->GetPlaybackStateForTests().Play();
	for (int32 Tick = 0; Tick < 60; ++Tick)
	{
		Preview->AdvancePlaybackForTests(1.0f / 60.0f);
	}
	TestEqual(TEXT("Playback ticks never rebuild the library projection"),
		Library->GetRefreshCountForTests(), RefreshesBeforePlayback);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectWorkspaceHeadlessPersistenceFocusTest,
	"Paper2DPlusRender.EffectProfile.Workspace.PanelsSelectionPersistenceAndFieldFocus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectWorkspaceHeadlessPersistenceFocusTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusEffectProfileAsset* Asset = EffectWorkspace_MakeAsset(TEXT("FXPersistence"));
	UPaperFlipbook* Flipbook = EffectWorkspace_MakeFlipbook(TEXT("FX_Persist"));
	EffectWorkspace_AddEntry(Asset, Flipbook, Paper2DPlusEffectTags::Type_Aura.GetTag());
	FEffectProfileEditorModel::ResetSelectionConfigForTests(Asset);
	{
		TSharedRef<FEffectProfileEditorModel> FirstModel = EffectWorkspace_MakeModel(Asset, true);
		FirstModel->SelectEffectPath(EffectWorkspace_Path(Flipbook));
	}
	TSharedRef<FEffectProfileEditorModel> ReopenedModel = EffectWorkspace_MakeModel(Asset, true);
	// Bump this with the toolkit's WorkspaceLayoutId. The v4 form of this ratchet outlived the
	// v5 narrow-Library bump and silently failed, so the key it guards must be named exactly.
	TestTrue(TEXT("Workspace uses the versioned v5 narrow-Library layout"),
		FEffectProfileAssetEditorToolkit::WorkspaceLayoutId.ToString().Contains(TEXT("Layout_v5")));
	TestTrue(TEXT("Reopened model restores path selection"), ReopenedModel->GetSelectedFlipbook() == Flipbook);

	TSharedRef<SEffectProfileLibraryPanel> Library = SNew(SEffectProfileLibraryPanel).Model(ReopenedModel);
	TSharedRef<SEffectProfilePreviewPanel> Preview = SNew(SEffectProfilePreviewPanel).Model(ReopenedModel);
	TSharedRef<SEffectProfileDetailsPanel> Details = SNew(SEffectProfileDetailsPanel).Model(ReopenedModel);
	TestTrue(TEXT("Library is keyboard focusable"), Library->SupportsKeyboardFocus());
	TestTrue(TEXT("Preview is keyboard focusable for frame navigation and Space playback"), Preview->SupportsKeyboardFocus());
	TestTrue(TEXT("Headless-safe Preview receives restored selection"), Preview->HasSelectionForTests());
	TestTrue(TEXT("Headless-safe Details receives restored selection"), Details->HasSelectionForTests());
	Details->FocusField(GET_MEMBER_NAME_CHECKED(FPaper2DPlusEffectProfileEntry, DisplayLabel));
	TestEqual(TEXT("Validation field focus seam records exact active field"),
		Details->GetLastFocusedFieldForTests(),
		GET_MEMBER_NAME_CHECKED(FPaper2DPlusEffectProfileEntry, DisplayLabel));
	if (!FApp::CanEverRender())
	{
		Paper2DPlusTestSkip::Mark(
			*this,
			TEXT("Effect Profile workspace window creation"),
			TEXT("headless built every non-window panel; the editor-open test covers windows"));
	}
	FEffectProfileEditorModel::ResetSelectionConfigForTests(Asset);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPaper2DPlusEffectWorkspaceEmptyIntakeTransitionTest,
	"Paper2DPlus.EffectProfile.Workspace.EmptyIntakeTransitionsAllPanels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPaper2DPlusEffectWorkspaceEmptyIntakeTransitionTest::RunTest(const FString& Parameters)
{
	UPaper2DPlusEffectProfileAsset* Asset = EffectWorkspace_MakeAsset(TEXT("FXEmpty"));
	TSharedRef<FEffectProfileEditorModel> Model = EffectWorkspace_MakeModel(Asset);
	TSharedRef<SEffectProfileLibraryPanel> Library = SNew(SEffectProfileLibraryPanel).Model(Model);
	TSharedRef<SEffectProfilePreviewPanel> Preview = SNew(SEffectProfilePreviewPanel).Model(Model);
	TSharedRef<SEffectProfileDetailsPanel> Details = SNew(SEffectProfileDetailsPanel).Model(Model);
	TestTrue(TEXT("Fresh profile presents dedicated intake state"), Library->IsShowingEmptyStateForTests());
	TestFalse(TEXT("Fresh Preview has intentional no-selection state"), Preview->HasSelectionForTests());
	TestFalse(TEXT("Fresh Details has intentional no-selection state"), Details->HasSelectionForTests());

	UPaperFlipbook* First = EffectWorkspace_MakeFlipbook(TEXT("FX_First"), 1);
	const FEffectProfileIntakeResult Result = Model->AddFlipbooks({ First });
	Details->FlushPendingRefreshForTests();
	TestEqual(TEXT("First intake accepts one flipbook"), Result.AddedCount, 1);
	TestFalse(TEXT("Library leaves empty state without reopening"), Library->IsShowingEmptyStateForTests());
	TestTrue(TEXT("Preview transitions to first selection without reopening"), Preview->HasSelectionForTests());
	TestTrue(TEXT("Details transitions to first selection without reopening"), Details->HasSelectionForTests());
	TestTrue(TEXT("Visible state label reports accepted intake"), Result.BuildSummary().ToString().Contains(TEXT("Added 1")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
