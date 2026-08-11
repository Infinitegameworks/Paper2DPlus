// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "BulkSpriteExtractorWindow.h"
#include "BulkDebakeUtils.h"
#include "BulkFolderOrganizationUtils.h"
#include "CharacterProfileEditorModel.h"
#include "DestructiveActionUtils.h"
#include "PaperZDSequenceAuthoring.h"
#include "ProfileDetailsPanel.h"
#include "SlateShortcutUtils.h"
#include "SpriteExtractorWindow.h"
#include "Paper2DPlusEditorCompat.h" // PAPER2DPLUS_RENAME_TO_TRANSIENT_FLAGS (UE5.8 REN_ForceNoResetLoaders deprecation)
#include "Misc/EngineVersionComparison.h" // ENGINE_*_VERSION guards (FAssetData::AssetClassPath is 5.1+)
#include "Engine/Texture2D.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SButton.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusSettings.h"
#include "Misc/ScopeExit.h"                 // ON_SCOPE_EXIT — open the de-bake window after group creation
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "AssetViewUtils.h" // AssetViewUtils::IsValidFolderName — engine package-path name validation
#include "DesktopPlatformModule.h" // Export/Import Organization file dialogs
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h" // FMenuBuilder — status filter / sort menu
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Widgets/Images/SImage.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Misc/FeedbackContext.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/PackagePath.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "ScopedTransaction.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h" // FCoreStyle::Get()->"WhiteBrush" for the flat status chip
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "CharacterProfileAssetFactory.h"
#include "ObjectTools.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"

/** SBulkSpriteExtractorWindow — Multi-texture batch sprite extraction with grid detection, auto-pad, folder organization, and cross-sheet alignment. */

#include "CharacterSizingFit.h"

#define LOCTEXT_NAMESPACE "BulkSpriteExtractor"

namespace DestructiveActions = Paper2DPlusEditor::DestructiveActionUtils;

namespace BulkSpriteExtractor_Internal
{
	/** The pixels-per-unit extraction actually stamps on every sprite it creates
	 *  (FSpriteExtractionUtils' CreateSpritesFromSheet calls SetPixelsPerUnrealUnit(1.0f)). Named
	 *  here so the size readout reports the value in force instead of hard-coding a second copy of
	 *  it that could silently drift from the extractor. */
	static float ExtractionPixelsPerUnit()
	{
		return 1.0f;
	}

	/** Single-instance tracking — module-level weak ref to the active window.
	 *  OpenBulkExtractor() focuses the existing window if this resolves; otherwise opens a new one. */
	static TWeakPtr<SWindow> ActiveWindow;

	/** The active window's content widget — lets OpenBulkExtractorForDebake() append a group to a
	 *  live session instead of destroy-and-replace (bulk sessions carry state worth keeping). */
	static TWeakPtr<SBulkSpriteExtractorWindow> ActiveContent;

	/** THE single display-name resolution rule for every user-facing surface: the editable
	 *  DisplayName when set, otherwise the underlying asset name. Batch Rename and the row's inline
	 *  rename both write DisplayName, so every label, the search filter and the sort comparator all
	 *  agree the moment a rename commits. Mirrors the CommitBulkExtract fallback. */
	static FString StateLabel(const TSharedPtr<FBulkExtractorTextureState>& State)
	{
		if (!State.IsValid())
		{
			return FString();
		}
		if (!State->DisplayName.IsEmpty())
		{
			return State->DisplayName;
		}
		return State->Texture.IsNull() ? FString() : State->Texture.GetAssetName();
	}

	/** Join up to MaxNames labels for a message, then "and N more". Keeps a 200-row batch's blocker
	 *  text readable instead of printing the whole list. */
	FString JoinLabels(const TArray<FString>& Labels, int32 MaxNames = 4)
	{
		FString Joined;
		const int32 Shown = FMath::Min(Labels.Num(), MaxNames);
		for (int32 Index = 0; Index < Shown; ++Index)
		{
			if (Index > 0) Joined += TEXT(", ");
			Joined += Labels[Index];
		}
		if (Labels.Num() > Shown)
		{
			Joined += FString::Printf(TEXT(", and %d more"), Labels.Num() - Shown);
		}
		return Joined;
	}

	/** The artist's frame-size hint lives on the filename ("Foo_192x128") or its containing folder.
	 *  ParseFrameSizeHint takes the LAST match, so feeding the whole package path lets a per-sheet
	 *  name hint beat a folder-wide one for free. */
	FIntPoint ResolveFrameSizeHint(UTexture2D* Texture)
	{
		if (!Texture || !Texture->GetPackage())
		{
			return FIntPoint::ZeroValue;
		}
		return FSpriteExtractionUtils::ParseFrameSizeHint(Texture->GetPackage()->GetName());
	}

	/** Consensus grouping key: sheets only influence sheets that live beside them on disk. */
	FString ResolveFolderKey(UTexture2D* Texture)
	{
		if (!Texture || !Texture->GetPackage())
		{
			return FString();
		}
		return FPackageName::GetLongPackagePath(Texture->GetPackage()->GetName());
	}

	/** Replace a state's detected sprites with one full-cell rect per grid cell, row-major. Both the
	 *  frame-grid and manual-grid branches feed this, so the canvas overlay and CommitBulkExtract's
	 *  cell math see exactly the same shape either way. */
	void FillFullCellSprites(
		const TSharedPtr<FBulkExtractorTextureState>& State,
		FIntPoint TexDims,
		FIntPoint Grid)
	{
		if (!State.IsValid())
		{
			return;
		}
		State->DetectedSprites.Empty();
		if (Grid.X <= 0 || Grid.Y <= 0 || TexDims.X <= 0 || TexDims.Y <= 0)
		{
			return;
		}
		const int32 CellW = TexDims.X / Grid.X;
		const int32 CellH = TexDims.Y / Grid.Y;
		int32 Index = 0;
		for (int32 Row = 0; Row < Grid.Y; ++Row)
		{
			for (int32 Col = 0; Col < Grid.X; ++Col)
			{
				FDetectedSprite Sprite;
				Sprite.Bounds = FIntRect(Col * CellW, Row * CellH, (Col + 1) * CellW, (Row + 1) * CellH);
				Sprite.OriginalBounds = Sprite.Bounds;
				Sprite.bSelected = true;
				Sprite.Index = Index++;
				State->DetectedSprites.Add(Sprite);
			}
		}
	}

	/** Copy a frame-grid result onto a texture state (grid + readout). */
	void ApplyFrameGridResult(
		const TSharedPtr<FBulkExtractorTextureState>& State,
		const FDetectedFrameGrid& Result)
	{
		if (!State.IsValid())
		{
			return;
		}
		State->InferredGrid = Result.Grid;
		State->OverrideGrid = Result.Grid;
		State->bUserOverridden = false;
		State->FrameGridSource = Result.Source;
		State->FrameGridTrailingBlanks = Result.TrailingBlanks;
		State->FrameGridBlankCells = Result.BlankFrames.Num();
		State->bFrameGridConfident = Result.bConfident;
	}

	/** Clear the frame-grid readout so a stale "gutter, 2 trailing blanks" line cannot survive a
	 *  switch to island or manual-grid detection. */
	void ClearFrameGridReadout(const TSharedPtr<FBulkExtractorTextureState>& State)
	{
		if (!State.IsValid())
		{
			return;
		}
		State->FrameGridSource.Reset();
		State->FrameGridTrailingBlanks = 0;
		State->FrameGridBlankCells = 0;
		State->bFrameGridConfident = false;
	}
}

void SBulkSpriteExtractorWindow::OpenBulkExtractor(
	const TArray<TSoftObjectPtr<UTexture2D>>& InitialTextures,
	TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset> InTargetProfile)
{
	// Focus existing instance if one is already open (single-instance rule).
	if (TSharedPtr<SWindow> Existing = BulkSpriteExtractor_Internal::ActiveWindow.Pin())
	{
		Existing->BringToFront();
		FSlateApplication::Get().SetKeyboardFocus(Existing);
		return;
	}

	if (InitialTextures.Num() == 0)
	{
		FSlateApplication::Get().GetRenderer()->FlushCommands();
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("BulkExtractorNoTextures", "No textures selected. Bulk extraction requires at least one UTexture2D."));
		return;
	}

	const FText Title = LOCTEXT("BulkExtractorTitle", "Extract Sprites to CharacterProfile");

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(Title)
		.ClientSize(FVector2D(1280.0f, 800.0f))
		.MinWidth(800.0f)
		.MinHeight(600.0f)
		.SupportsMaximize(true)
		.SupportsMinimize(true);

	TSharedRef<SBulkSpriteExtractorWindow> Content = SNew(SBulkSpriteExtractorWindow)
		.InitialTextures(InitialTextures)
		.TargetProfile(InTargetProfile);

	Window->SetContent(Content);

	// Non-modal — editor remains interactive underneath.
	FSlateApplication::Get().AddWindow(Window, /*bShowImmediately=*/true);

	BulkSpriteExtractor_Internal::ActiveWindow = Window;
	BulkSpriteExtractor_Internal::ActiveContent = Content;
}

void SBulkSpriteExtractorWindow::OpenBulkExtractorForDebake(const TArray<TSoftObjectPtr<UTexture2D>>& VariantSheets)
{
	if (TSharedPtr<SWindow> Existing = BulkSpriteExtractor_Internal::ActiveWindow.Pin())
	{
		Existing->BringToFront();
		FSlateApplication::Get().SetKeyboardFocus(Existing);
		if (TSharedPtr<SBulkSpriteExtractorWindow> Content = BulkSpriteExtractor_Internal::ActiveContent.Pin())
		{
			Content->AddDebakeGroupFromTextures(VariantSheets);
		}
		return;
	}

	OpenBulkExtractor(VariantSheets);
	if (TSharedPtr<SBulkSpriteExtractorWindow> Content = BulkSpriteExtractor_Internal::ActiveContent.Pin())
	{
		Content->AddDebakeGroupFromTextures(VariantSheets);
	}
}

FReply SBulkSpriteExtractorWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return FReply::Unhandled();
	}

	const FKey Key = InKeyEvent.GetKey();

	// Enter stays on the BUBBLE path so a focused spinbox/combo commits its own value first.
	// Texture navigation lives in OnPreviewKeyDown (see below).
	if (Key == EKeys::Enter)
	{
		return OnAcceptGridClicked();
	}

	return FReply::Unhandled();
}

FReply SBulkSpriteExtractorWindow::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Preview routing TUNNELS window→leaf, so these keys arrive before SListView's own Up/Down row
	// navigation or an SNumericEntryBox/SSpinBox's internals can consume them — that is what makes
	// arrows work from ANYWHERE in the window. The guard is deliberately the NARROW text-entry one:
	// yield to a live caret, not to a merely-focused numeric/combo control.
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreTypingShortcutForFocusedWidget())
	{
		return FReply::Unhandled();
	}

	const FKey Key = InKeyEvent.GetKey();

	if (Key == EKeys::F2)
	{
		return BeginInlineRenameOnSelectedTexture();
	}

	const bool bStepBack = (Key == EKeys::Left || Key == EKeys::Up);
	const bool bStepForward = (Key == EKeys::Right || Key == EKeys::Down);
	if (!bStepBack && !bStepForward)
	{
		return FReply::Unhandled();
	}

	// MODIFIED arrows belong to the list, not to this window. The texture list is
	// ESelectionMode::Multi precisely so "Group Selected as De-bake Set" can act on a range, and
	// SListView implements Shift+Arrow range-extend / Ctrl+Arrow focus-without-select itself.
	// Stepping the single selection here (SelectTextureState -> SetSelection, which clears first)
	// would collapse that range on every press and make multi-select unreachable from the keyboard.
	if (InKeyEvent.IsShiftDown() || InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown())
	{
		return FReply::Unhandled();
	}

	// Walk the VISIBLE rows, not the master array: with a search filter active the master array
	// walks straight out of the result set into rows the list is not showing. FilteredTextureStates
	// is a straight copy of TextureStates when the filter is empty, so one path covers both.
	// Nothing to step through -> yield the key rather than eating it: an unhandled arrow can still
	// reach the list (or a scroll box) instead of becoming a dead key.
	if (FilteredTextureStates.Num() == 0)
	{
		return FReply::Unhandled();
	}

	const int32 CurrentIndex = SelectedTexture.IsValid()
		? FilteredTextureStates.IndexOfByKey(SelectedTexture)
		: INDEX_NONE;

	// Selection sits outside the visible set (or nothing is selected) — jump INTO the results.
	// Without this the first arrow press after typing a filter does nothing.
	if (CurrentIndex == INDEX_NONE)
	{
		SelectTextureState(FilteredTextureStates[0]);
		return FReply::Handled();
	}

	// One visible row: it is already selected, so there is nowhere to step. Yield the key.
	if (FilteredTextureStates.Num() <= 1)
	{
		return FReply::Unhandled();
	}

	const int32 Delta = bStepBack ? -1 : 1;
	const int32 NextIndex = (CurrentIndex + Delta + FilteredTextureStates.Num()) % FilteredTextureStates.Num();
	// Copy before selecting — selection re-entry must not read through a mutated array.
	const TSharedPtr<FBulkExtractorTextureState> Target = FilteredTextureStates[NextIndex];
	SelectTextureState(Target);
	return FReply::Handled();
}

SBulkSpriteExtractorWindow::~SBulkSpriteExtractorWindow()
{
	if (CurrentPadManifests.Num() > 0 && !RestoreCurrentPadSnapshots())
	{
		UE_LOG(LogTemp, Error,
			TEXT("BulkExtractor closed with an incomplete pad restore. Recovery snapshots were retained."));
	}
}

void SBulkSpriteExtractorWindow::SelectTextureByIndex(int32 Index)
{
	if (!TextureStates.IsValidIndex(Index)) return;
	SelectTextureState(TextureStates[Index]);
}

void SBulkSpriteExtractorWindow::SelectTextureState(TSharedPtr<FBulkExtractorTextureState> State)
{
	if (!State.IsValid()) return;
	SelectedTexture = State;
	if (TextureListView.IsValid())
	{
		// SetSelection routes through Private_SignalSelectionChanged, which already runs
		// OnTextureSelectionChanged; only drive it by hand when there is no list to signal.
		TextureListView->SetSelection(SelectedTexture, ESelectInfo::Direct);
	}
	else
	{
		OnTextureSelectionChanged(SelectedTexture, ESelectInfo::Direct);
	}
}

void SBulkSpriteExtractorWindow::InvalidateDerivedCounts()
{
	CachedPadCount = -1;
	bCachedGateValid = false;
}

void SBulkSpriteExtractorWindow::RefreshFilteredTextureList()
{
	// Sort the MASTER array, not just the visible copy: every TextureStates.IndexOfByKey consumer
	// (de-bake member order, accepted-entry landing) then stays coherent with what the user sees,
	// and FilteredTextureStates inherits the order for free. StableSort (merge sort) is what keeps
	// equal names from shuffling between refreshes — they retain their existing relative order.
	const EBulkExtractorSortMode ActiveSort = SortMode;
	TextureStates.StableSort([ActiveSort](const TSharedPtr<FBulkExtractorTextureState>& A,
		const TSharedPtr<FBulkExtractorTextureState>& B)
	{
		if (ActiveSort == EBulkExtractorSortMode::BlockingFirst)
		{
			// Same predicate the blocker text and the "needs attention" filter use.
			const bool bABlocks = !A.IsValid() || !StatusCountsAsConfirmed(A->Status);
			const bool bBBlocks = !B.IsValid() || !StatusCountsAsConfirmed(B->Status);
			if (bABlocks != bBBlocks)
			{
				return bABlocks;
			}
		}
		return BulkSpriteExtractor_Internal::StateLabel(A)
			.Compare(BulkSpriteExtractor_Internal::StateLabel(B), ESearchCase::IgnoreCase) < 0;
	});

	FilteredTextureStates.Empty(TextureStates.Num());
	for (const TSharedPtr<FBulkExtractorTextureState>& State : TextureStates)
	{
		if (!State.IsValid())
		{
			continue;
		}
		// Filter on the SAME resolved label the rows display and the sort orders by, so a row
		// renamed through Batch Rename (or inline) is immediately findable under its new name.
		if (!TextureSearchFilter.IsEmpty()
			&& !BulkSpriteExtractor_Internal::StateLabel(State).Contains(TextureSearchFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (bShowOnlyBlocking && StatusCountsAsConfirmed(State->Status))
		{
			continue;
		}
		if (StatusFilter.Num() > 0 && !StatusFilter.Contains(State->Status))
		{
			continue;
		}
		FilteredTextureStates.Add(State);
	}

	// Drop rename-widget entries for states that have left the batch (rows are virtualized, so the
	// widget itself is already weak — this only keeps the map from growing).
	for (auto It = TextureNameTexts.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !TextureStates.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}

	if (TextureListView.IsValid()) TextureListView->RequestListRefresh();
}

FReply SBulkSpriteExtractorWindow::BeginInlineRenameOnSelectedTexture()
{
	if (!SelectedTexture.IsValid() || !FilteredTextureStates.Contains(SelectedTexture))
	{
		return FReply::Unhandled();
	}

	PendingRenameTexture = SelectedTexture;
	if (TextureListView.IsValid())
	{
		// The row may not be generated yet (virtualization) — bring it into view first.
		TextureListView->RequestScrollIntoView(SelectedTexture);
	}

	// Deferred + retried: the row widget is created during the next paint, and EnterEditingMode is
	// a verified no-op while a mouse captor is live.
	TSharedRef<int32> Attempts = MakeShared<int32>(0);
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
		[this, Attempts](double, float) -> EActiveTimerReturnType
		{
			if (!PendingRenameTexture.IsValid())
			{
				return EActiveTimerReturnType::Stop;
			}
			if (const TWeakPtr<SInlineEditableTextBlock>* Found = TextureNameTexts.Find(PendingRenameTexture))
			{
				if (const TSharedPtr<SInlineEditableTextBlock> NameText = Found->Pin())
				{
					NameText->EnterEditingMode();
					if (NameText->IsInEditMode())
					{
						PendingRenameTexture.Reset();
						return EActiveTimerReturnType::Stop;
					}
				}
			}
			if (++(*Attempts) >= 8)
			{
				PendingRenameTexture.Reset();
				return EActiveTimerReturnType::Stop;
			}
			return EActiveTimerReturnType::Continue;
		}));

	return FReply::Handled();
}

void SBulkSpriteExtractorWindow::ReRunDetectionOnSelected()
{
	if (!SelectedTexture.IsValid()) return;
	if ((SelectedTexture->Status == EBulkExtractorTextureStatus::Padded
		|| SelectedTexture->Status == EBulkExtractorTextureStatus::SkippedNoPad)
		&& !InvalidatePaddingForGridEdit())
	{
		SelectedTexture->Status = EBulkExtractorTextureStatus::Error;
		return;
	}
	SelectedTexture->bDetectionRun = false;
	SelectedTexture->Status = EBulkExtractorTextureStatus::Pending;
	InvalidateDerivedCounts();
	RunDetectionAndInference(SelectedTexture);
	if (CenterCanvas.IsValid())
	{
		CenterCanvas->SetDetectedSprites(SelectedTexture->DetectedSprites);
		CenterCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	}
	// Through the funnel, not RequestListRefresh: the status just changed, and the list can now be
	// filtered/sorted BY status — a row that stops blocking must leave the "needs attention" view.
	RefreshFilteredTextureList();
}

void SBulkSpriteExtractorWindow::Construct(const FArguments& InArgs)
{
	TargetProfile = InArgs._TargetProfile;
	bPaperZDAuthoringAvailable =
		Paper2DPlus::PaperZDSequenceAuthoring::IsOptionalAuthoringAvailable();

	// Build per-texture state entries.
	TextureStates.Reserve(InArgs._InitialTextures.Num());
	for (const TSoftObjectPtr<UTexture2D>& SoftTex : InArgs._InitialTextures)
	{
		TSharedPtr<FBulkExtractorTextureState> State = MakeShared<FBulkExtractorTextureState>();
		State->Texture = SoftTex;
		State->Status = EBulkExtractorTextureStatus::Pending;
		if (UTexture2D* Tex = SoftTex.LoadSynchronous()) State->DisplayName = Tex->GetName();
		TextureStates.Add(State);
	}
	// Initialize sort + filtered list (Unit 8: TASK-34 — texture search). Runs BEFORE the initial
	// selection so the first selected row is the first VISIBLE row, not the pre-sort first entry.
	RefreshFilteredTextureList();

	if (FilteredTextureStates.Num() > 0)
	{
		SelectedTexture = FilteredTextureStates[0];
	}

	// ----- Left pane: texture list -----
	// Multi-select enables "Group Selected as De-bake Set"; SelectedTexture tracks the LAST-selected
	// item so every existing single-select flow (canvas, grid pane, arrow nav) behaves unchanged.
	TextureListView = SNew(SListView<TSharedPtr<FBulkExtractorTextureState>>)
		.ListItemsSource(&FilteredTextureStates)
		.OnGenerateRow(this, &SBulkSpriteExtractorWindow::GenerateTextureRow)
		.OnSelectionChanged(this, &SBulkSpriteExtractorWindow::OnTextureSelectionChanged)
		.SelectionMode(ESelectionMode::Multi);

	TSharedRef<SVerticalBox> LeftPane = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("TexturesHeader", "TEXTURES"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("TexturesNavHint", "\x2190 \x2192"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
				.ToolTipText(LOCTEXT("TexturesNavHintTip", "Left/Right arrow keys to navigate textures"))
			]
		]
		// Search bar (Unit 8: TASK-34)
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 0, 4, 4)
		[
			SNew(SEditableTextBox)
			.HintText(LOCTEXT("TextureSearchHint", "Search textures..."))
			.OnTextChanged_Lambda([this](const FText& NewText)
			{
				TextureSearchFilter = NewText.ToString();
				RefreshFilteredTextureList();
			})
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		]
		// Status filter + sort. In a 200+ batch the unconfirmed rows are otherwise unfindable: the
		// "Needs attention" toggle uses EXACTLY the predicate the extraction blocker counts.
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 0, 4, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ButtonColorAndOpacity_Lambda([this]()
				{
					return bShowOnlyBlocking ? FLinearColor(0.85f, 0.55f, 0.15f) : FLinearColor(0.15f, 0.15f, 0.15f);
				})
				.ToolTipText(LOCTEXT("NeedsAttentionTip", "Show only the rows that block Extract All (anything not Confirmed / Skipped / Padded / Debaked). Click again to show everything."))
				.OnClicked_Lambda([this]() -> FReply
				{
					bShowOnlyBlocking = !bShowOnlyBlocking;
					RefreshFilteredTextureList();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						int32 Blocking = 0;
						for (const TSharedPtr<FBulkExtractorTextureState>& S : TextureStates)
						{
							if (!S.IsValid() || !StatusCountsAsConfirmed(S->Status)) ++Blocking;
						}
						return FText::Format(LOCTEXT("NeedsAttentionFmt", "Needs attention ({0})"), FText::AsNumber(Blocking));
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SComboButton)
				.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
				.ToolTipText(LOCTEXT("StatusFilterTip", "Filter the list by status, and choose the row order."))
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return StatusFilter.Num() > 0
							? FText::Format(LOCTEXT("StatusFilterActiveFmt", "Status ({0})"), FText::AsNumber(StatusFilter.Num()))
							: LOCTEXT("StatusFilterAll", "Status");
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				.OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
				{
					// Weak self: the popup's FUIAction delegates live in the menu stack, which is not
					// owned by this widget. Same guard SAnimationsPanel's view menu uses.
					const TWeakPtr<SBulkSpriteExtractorWindow> WeakSelf = SharedThis(this);
					FMenuBuilder Menu(/*bInShouldCloseWindowAfterMenuSelection=*/false, nullptr);

					Menu.BeginSection(NAME_None, LOCTEXT("StatusFilterSection", "Show statuses"));
					{
						Menu.AddMenuEntry(
							LOCTEXT("StatusFilterShowAll", "All statuses"),
							LOCTEXT("StatusFilterShowAllTip", "Clear the status filter."),
							FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateLambda([WeakSelf]()
								{
									if (const TSharedPtr<SBulkSpriteExtractorWindow> Self = WeakSelf.Pin())
									{
										Self->StatusFilter.Reset();
										Self->RefreshFilteredTextureList();
									}
								}),
								FCanExecuteAction(),
								FIsActionChecked::CreateLambda([WeakSelf]()
								{
									const TSharedPtr<SBulkSpriteExtractorWindow> Self = WeakSelf.Pin();
									return Self.IsValid() && Self->StatusFilter.Num() == 0;
								})),
							NAME_None,
							EUserInterfaceActionType::RadioButton);

						static const EBulkExtractorTextureStatus AllStatuses[] = {
							EBulkExtractorTextureStatus::Pending,
							EBulkExtractorTextureStatus::Inferred,
							EBulkExtractorTextureStatus::Overridden,
							EBulkExtractorTextureStatus::Confirmed,
							EBulkExtractorTextureStatus::SkippedNoPad,
							EBulkExtractorTextureStatus::Padded,
							EBulkExtractorTextureStatus::DebakeSource,
							EBulkExtractorTextureStatus::Error
						};
						for (const EBulkExtractorTextureStatus Status : AllStatuses)
						{
							int32 Count = 0;
							for (const TSharedPtr<FBulkExtractorTextureState>& S : TextureStates)
							{
								if (S.IsValid() && S->Status == Status) ++Count;
							}
							Menu.AddMenuEntry(
								FText::Format(LOCTEXT("StatusFilterEntryFmt", "{0} ({1})"), GetStatusLabel(Status), FText::AsNumber(Count)),
								FText::GetEmpty(),
								FSlateIcon(),
								FUIAction(
									FExecuteAction::CreateLambda([WeakSelf, Status]()
									{
										const TSharedPtr<SBulkSpriteExtractorWindow> Self = WeakSelf.Pin();
										if (!Self.IsValid()) return;
										if (Self->StatusFilter.Contains(Status)) Self->StatusFilter.Remove(Status);
										else Self->StatusFilter.Add(Status);
										Self->RefreshFilteredTextureList();
									}),
									FCanExecuteAction(),
									FIsActionChecked::CreateLambda([WeakSelf, Status]()
									{
										const TSharedPtr<SBulkSpriteExtractorWindow> Self = WeakSelf.Pin();
										return Self.IsValid() && Self->StatusFilter.Contains(Status);
									})),
								NAME_None,
								EUserInterfaceActionType::ToggleButton);
						}
					}
					Menu.EndSection();

					Menu.BeginSection(NAME_None, LOCTEXT("StatusSortSection", "Sort"));
					{
						auto AddSort = [WeakSelf, &Menu](EBulkExtractorSortMode Mode, const FText& Label, const FText& Tip)
						{
							Menu.AddMenuEntry(Label, Tip, FSlateIcon(),
								FUIAction(
									FExecuteAction::CreateLambda([WeakSelf, Mode]()
									{
										if (const TSharedPtr<SBulkSpriteExtractorWindow> Self = WeakSelf.Pin())
										{
											Self->SortMode = Mode;
											Self->RefreshFilteredTextureList();
										}
									}),
									FCanExecuteAction(),
									FIsActionChecked::CreateLambda([WeakSelf, Mode]()
									{
										const TSharedPtr<SBulkSpriteExtractorWindow> Self = WeakSelf.Pin();
										return Self.IsValid() && Self->SortMode == Mode;
									})),
								NAME_None,
								EUserInterfaceActionType::RadioButton);
						};
						AddSort(EBulkExtractorSortMode::Name,
							LOCTEXT("SortByName", "Name"),
							LOCTEXT("SortByNameTip", "Alphabetical by display name."));
						AddSort(EBulkExtractorSortMode::BlockingFirst,
							LOCTEXT("SortBlockingFirst", "Blocking rows first"),
							LOCTEXT("SortBlockingFirstTip", "Rows that block Extract All first, alphabetical within each half."));
					}
					Menu.EndSection();

					return Menu.MakeWidget();
				})
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4, 0, 4, 4)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			[
				TextureListView.ToSharedRef()
			]
		];

	// ----- Center pane: canvas (live detection + grid overlay) -----
	// This canvas shows the SELECTED TEXTURE and nothing else. De-bake previews render in the
	// de-bake window's own canvas.
	CenterCanvas = SNew(SSpriteExtractorCanvas)
		.bShowGridOverlay_Lambda([this]() -> bool
		{
			return SelectedTexture.IsValid()
				&& SelectedTexture->bDetectionRun
				&& SelectedTexture->GetEffectiveGrid().X > 0
				&& SelectedTexture->GetEffectiveGrid().Y > 0;
		})
		.GridDims_Lambda([this]() -> FIntPoint
		{
			return SelectedTexture.IsValid() ? SelectedTexture->GetEffectiveGrid() : FIntPoint::ZeroValue;
		})
		.GridState_Lambda([this]() { return GetGridState(); });

	TSharedRef<SVerticalBox> CenterPane = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 4)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() -> FText
			{
				if (!SelectedTexture.IsValid() || SelectedTexture->Texture.IsNull())
				{
					return LOCTEXT("NoTextureSelected", "No texture selected");
				}
				// Same resolved label as the list row, so a renamed texture does not keep reading
				// under its old asset name here.
				return FText::FromString(BulkSpriteExtractor_Internal::StateLabel(SelectedTexture));
			})
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Clipping(EWidgetClipping::ClipToBounds)
			[
				CenterCanvas.ToSharedRef()
			]
		];

	// ----- Right pane: profile + grid confirmation + settings -----
	TSharedRef<SVerticalBox> RightPane = SNew(SVerticalBox)
		// Character Profile section (extract mode only)
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ProfileHeader", "CHARACTER PROFILE"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))

		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 0, 4, 4)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(6)

			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bLinkToProfile ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bLinkToProfile = (NewState == ECheckBoxState::Checked); })
					[
						SNew(STextBlock).Text(LOCTEXT("LinkProfileLabel", "Link to Character Profile"))
					]
				]
				// Profile picker (visible when checkbox is checked)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 2)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UPaper2DPlusCharacterProfileAsset::StaticClass())
					.ObjectPath_Lambda([this]() { return TargetProfile.ToString(); })
					.OnObjectChanged(FOnSetObject::CreateLambda([this](const FAssetData& AssetData)
					{
						UPaper2DPlusCharacterProfileAsset* Profile =
							Cast<UPaper2DPlusCharacterProfileAsset>(
								AssetData.GetAsset());
						const FSoftObjectPath SelectedPath =
							Profile
								? FSoftObjectPath(Profile)
								: FSoftObjectPath();
						if (TargetProfile.ToSoftObjectPath()
							!= SelectedPath)
						{
							TargetProfile = Profile;
							bCreatePaperZDSequencesAfterExtract = false;
							PaperZDConfiguredProfile.Reset();
						}
					}))
					.Visibility_Lambda([this]() { return bLinkToProfile ? EVisibility::Visible : EVisibility::Collapsed; })
				]
				// Warning when profile is linked but none selected
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoProfileWarning", "No profile selected \x2014 extracted flipbooks will not be attached to a profile"))
					.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.8f, 0.2f)))
					.AutoWrapText(true)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.Visibility_Lambda([this]()
					{
						return (bLinkToProfile && TargetProfile.IsNull()) ? EVisibility::Visible : EVisibility::Collapsed;
					})
				]
				// Create New Profile button
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("CreateNewProfile", "Create New Profile"))
					.OnClicked_Lambda([this]() -> FReply
					{
						UCharacterProfileAssetFactory* Factory = NewObject<UCharacterProfileAssetFactory>();
						if (Factory)
						{
							IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
							FString DefaultPath = TEXT("/Game");
							if (TextureStates.Num() > 0 && TextureStates[0].IsValid())
							{
								UTexture2D* Tex = TextureStates[0]->Texture.LoadSynchronous();
								if (Tex) DefaultPath = FPackageName::GetLongPackagePath(Tex->GetPackage()->GetName());
							}
							UObject* NewAsset = AssetTools.CreateAssetWithDialog(
								TEXT("NewCharacterProfile"), DefaultPath,
								UPaper2DPlusCharacterProfileAsset::StaticClass(), Factory);
							if (UPaper2DPlusCharacterProfileAsset* NewProfile = Cast<UPaper2DPlusCharacterProfileAsset>(NewAsset))
							{
								TargetProfile = NewProfile;
								bCreatePaperZDSequencesAfterExtract = false;
								PaperZDConfiguredProfile.Reset();
							}
						}
						return FReply::Handled();
					})
					.Visibility_Lambda([this]() { return bLinkToProfile ? EVisibility::Visible : EVisibility::Collapsed; })
				]
			]
		]
		// Auto-create groups checkbox (visible when profile linked + folder organizer used)
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
		[
			SNew(SCheckBox)
			.Visibility_Lambda([this]() { return (bLinkToProfile && HasAnyFolderAssignment()) ? EVisibility::Visible : EVisibility::Collapsed; })
			.IsChecked_Lambda([this]() { return bAutoCreateGroups ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bAutoCreateGroups = (NewState == ECheckBoxState::Checked); })
			.ToolTipText(LOCTEXT("AutoCreateGroupsTip", "Create flipbook groups on the profile matching the folder organizer structure"))
			[
				SNew(STextBlock).Text(LOCTEXT("AutoCreateGroupsLabel", "Auto-create groups from folders"))
			]
		]
		// De-bake groups live in their own window (Advanced ▸ De-bake), not this panel.
		// Grid confirmation section
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("GridHeader", "GRID"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 0, 4, 4)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(6)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("ColsLabel", "Cols"))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SNumericEntryBox<int32>)
						.MinValue(1).MinSliderValue(1)
						.MaxValue(64).MaxSliderValue(32)
						.AllowSpin(true)
						.Value(this, &SBulkSpriteExtractorWindow::GetColsValue)
						.OnValueChanged(this, &SBulkSpriteExtractorWindow::OnColsChanged)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("RowsLabel", "Rows"))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SNumericEntryBox<int32>)
						.MinValue(1).MinSliderValue(1)
						.MaxValue(64).MaxSliderValue(32)
						.AllowSpin(true)
						.Value(this, &SBulkSpriteExtractorWindow::GetRowsValue)
						.OnValueChanged(this, &SBulkSpriteExtractorWindow::OnRowsChanged)
					]
				]
				// Divisibility warning (amber, visible when grid doesn't divide cleanly)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return GetDivisibilityWarning(); })
					.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.75f, 0.2f)))
					.Visibility_Lambda([this]()
					{
						return GetDivisibilityWarning().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
					})
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("AcceptGridButton", "Accept Grid  (Enter)"))
					.OnClicked(this, &SBulkSpriteExtractorWindow::OnAcceptGridClicked)
					.IsEnabled_Lambda([this]()
					{
						return SelectedTexture.IsValid()
							&& SelectedTexture->bDetectionRun
							&& SelectedTexture->Status != EBulkExtractorTextureStatus::Confirmed
							&& SelectedTexture->Status != EBulkExtractorTextureStatus::Padded;
					})
				]
			]
		]
		// Detection settings section
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 8, 4, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DetectionHeader", "DETECTION"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 0, 4, 4)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(6)
			[
				SNew(SVerticalBox)
				// Mode toggle. Frames (the gutter rule) is the primary detector; Island is the
				// historical "where is the art" flood fill; Grid is a plain manual division.
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("DetectionModeLabel", "Mode"))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
							.Text(LOCTEXT("FramesMode", "Frames"))
							.ToolTipText(LOCTEXT("FramesModeTip", "Find where the animation FRAMES are using the gutter rule: a cell width is valid only when every internal boundary column is empty. Survives scattered particle VFX that shatter island detection, honours an artist's \"Foo_192x128\" size hint, and allows a capped run of trailing blank frames."))
							.ButtonColorAndOpacity_Lambda([this]() { return DetectionMode == EBulkDetectionMode::Frames ? FLinearColor(0.2f, 0.4f, 0.7f) : FLinearColor(0.15f, 0.15f, 0.15f); })
							.OnClicked_Lambda([this]() -> FReply { DetectionMode = EBulkDetectionMode::Frames; ReRunDetectionOnSelected(); return FReply::Handled(); })
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
							.Text(LOCTEXT("IslandMode", "Island"))
							.ToolTipText(LOCTEXT("IslandModeTip", "Find where the ART is by flood-filling opaque islands, then infer a grid from their layout."))
							.ButtonColorAndOpacity_Lambda([this]() { return DetectionMode == EBulkDetectionMode::Island ? FLinearColor(0.2f, 0.4f, 0.7f) : FLinearColor(0.15f, 0.15f, 0.15f); })
							.OnClicked_Lambda([this]() -> FReply { DetectionMode = EBulkDetectionMode::Island; ReRunDetectionOnSelected(); return FReply::Handled(); })
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0, 0, 0)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
							.Text(LOCTEXT("GridMode", "Grid"))
							.ToolTipText(LOCTEXT("GridModeTip", "Divide the sheet by the columns/rows you type. No pixels are consulted."))
							.ButtonColorAndOpacity_Lambda([this]() { return DetectionMode == EBulkDetectionMode::ManualGrid ? FLinearColor(0.2f, 0.4f, 0.7f) : FLinearColor(0.15f, 0.15f, 0.15f); })
							.OnClicked_Lambda([this]() -> FReply { DetectionMode = EBulkDetectionMode::ManualGrid; ReRunDetectionOnSelected(); return FReply::Handled(); })
						]
					]
				]
				// Frame-grid readout: WHICH rule decided the grid, whether the sheet has trailing
				// blank frames (legitimate VFX length padding), and whether the answer wants review.
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 2)
				[
					SNew(STextBlock)
					.Visibility_Lambda([this]()
					{
						return (DetectionMode == EBulkDetectionMode::Frames
							&& SelectedTexture.IsValid()
							&& !SelectedTexture->FrameGridSource.IsEmpty())
							? EVisibility::Visible : EVisibility::Collapsed;
					})
					.Text_Lambda([this]() -> FText
					{
						if (!SelectedTexture.IsValid()) return FText::GetEmpty();
						const FIntPoint Grid = SelectedTexture->GetEffectiveGrid();
						FString Line = FString::Printf(TEXT("Frames: %d\x00D7%d \x2014 %s"),
							Grid.X, Grid.Y, *SelectedTexture->FrameGridSource);
						if (SelectedTexture->FrameGridTrailingBlanks > 0)
						{
							Line += FString::Printf(TEXT(" \x00B7 %d trailing blank frame(s)"),
								SelectedTexture->FrameGridTrailingBlanks);
						}
						const int32 InteriorBlanks =
							SelectedTexture->FrameGridBlankCells - SelectedTexture->FrameGridTrailingBlanks;
						if (InteriorBlanks > 0)
						{
							Line += FString::Printf(TEXT(" \x00B7 %d blank column(s)"), InteriorBlanks);
						}
						if (!SelectedTexture->bFrameGridConfident)
						{
							Line += TEXT("  \x2014  low confidence, review before extracting");
						}
						return FText::FromString(Line);
					})
					.ColorAndOpacity_Lambda([this]()
					{
						const bool bLowConfidence = SelectedTexture.IsValid() && !SelectedTexture->bFrameGridConfident;
						return bLowConfidence
							? FSlateColor(FLinearColor(1.0f, 0.75f, 0.2f))
							: FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f));
					})
					.ToolTipText(LOCTEXT("FrameGridReadoutTip", "Which rule decided this sheet's grid. \"gutter\" means every internal frame boundary is an empty column (strongest signal); \"hint\" came from the filename/folder size label; \"sibling\" was adopted from the folder's agreed frame format; anything else is a weaker fallback and is flagged low confidence. Trailing blank frames are normal on VFX strips padded to match an animation's length."))
					.AutoWrapText(true)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				// Frames mode: smallest cell the search may consider.
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]() { return DetectionMode == EBulkDetectionMode::Frames ? EVisibility::Visible : EVisibility::Collapsed; })
					.ToolTipText(LOCTEXT("MinCellTip", "Smallest frame width/height the gutter search may propose. Raise it when a sheet is being over-fragmented into tiny cells. Default: 16"))
					+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("MinCellLabel", "Min Cell Size"))
					]
					+ SHorizontalBox::Slot().FillWidth(0.5f)
					[
						SNew(SNumericEntryBox<int32>)
						.Value_Lambda([this]() -> TOptional<int32> { return FrameGridMinCell; })
						.OnValueCommitted_Lambda([this](int32 V, ETextCommit::Type)
						{
							FrameGridMinCell = FMath::Clamp(V, 1, 4096);
							ReRunDetectionOnSelected();
						})
					]
				]
				// Frames mode: whole-batch pass. Folder consensus needs every sheet's occupancy at
				// once, so it cannot run from the lazy per-texture path.
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 2)
				[
					SNew(SButton)
					.Visibility_Lambda([this]() { return DetectionMode == EBulkDetectionMode::Frames ? EVisibility::Visible : EVisibility::Collapsed; })
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("DetectAllButton", "Detect Frames on All"))
					.ToolTipText(LOCTEXT("DetectAllTip", "Run frame-grid detection across the whole batch, then let each folder's agreed frame FORMAT rescue its odd sheets. Consensus is per cell width AND height, so a 64-tall VFX strip never inherits a 128-tall character grid, and a sheet only adopts a width its own pixels admit. Rows already confirmed or padded are left alone."))
					.IsEnabled_Lambda([this]() { return TextureStates.Num() > 0; })
					.OnClicked_Lambda([this]() -> FReply
					{
						RunFrameGridDetectionForAll();
						return FReply::Handled();
					})
				]
				// Grid mode: columns
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]() { return DetectionMode == EBulkDetectionMode::ManualGrid ? EVisibility::Visible : EVisibility::Collapsed; })
					.ToolTipText(LOCTEXT("GridCountTip", "Number of columns (sprites per row)"))
					+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("GridCountLabel", "Columns"))
					]
					+ SHorizontalBox::Slot().FillWidth(0.5f)
					[
						SNew(SNumericEntryBox<int32>)
						.Value_Lambda([this]() -> TOptional<int32> { return GridSpriteCount; })
						.OnValueCommitted_Lambda([this](int32 V, ETextCommit::Type)
						{
							GridSpriteCount = FMath::Max(1, V);
							ReRunDetectionOnSelected();
						})
					]
				]
				// Grid mode: rows
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]() { return DetectionMode == EBulkDetectionMode::ManualGrid ? EVisibility::Visible : EVisibility::Collapsed; })
					.ToolTipText(LOCTEXT("GridRowTip", "Number of rows in the sprite sheet"))
					+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("GridRowLabel", "Rows"))
					]
					+ SHorizontalBox::Slot().FillWidth(0.5f)
					[
						SNew(SNumericEntryBox<int32>)
						.Value_Lambda([this]() -> TOptional<int32> { return GridRowCount; })
						.OnValueCommitted_Lambda([this](int32 V, ETextCommit::Type)
						{
							GridRowCount = FMath::Max(1, V);
							ReRunDetectionOnSelected();
						})
					]
				]
				// Island mode settings
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]() { return DetectionMode == EBulkDetectionMode::Island ? EVisibility::Visible : EVisibility::Collapsed; })
					.ToolTipText(LOCTEXT("AlphaThresholdTip", "Min alpha (0-255) for a pixel to count as opaque. Default: 1"))
					+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("AlphaLabel", "Alpha Threshold"))
					]
					+ SHorizontalBox::Slot().FillWidth(0.5f)
					[
						SNew(SNumericEntryBox<int32>)
						.Value_Lambda([this]() -> TOptional<int32> { return DetectionParams.AlphaThreshold; })
						.OnValueCommitted_Lambda([this](int32 V, ETextCommit::Type)
						{
							DetectionParams.AlphaThreshold = FMath::Clamp(V, 0, 255);
							ReRunDetectionOnSelected();
						})
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]() { return DetectionMode == EBulkDetectionMode::Island ? EVisibility::Visible : EVisibility::Collapsed; })
					.ToolTipText(LOCTEXT("MinSpriteSizeTip", "Regions smaller than this on either axis are discarded. Default: 4"))
					+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("MinSizeLabel", "Min Sprite Size"))
					]
					+ SHorizontalBox::Slot().FillWidth(0.5f)
					[
						SNew(SNumericEntryBox<int32>)
						.Value_Lambda([this]() -> TOptional<int32> { return DetectionParams.MinSpriteSize; })
						.OnValueCommitted_Lambda([this](int32 V, ETextCommit::Type)
						{
							DetectionParams.MinSpriteSize = FMath::Max(1, V);
							ReRunDetectionOnSelected();
						})
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]() { return DetectionMode == EBulkDetectionMode::Island ? EVisibility::Visible : EVisibility::Collapsed; })
					.ToolTipText(LOCTEXT("MergeDistTip", "Islands within this many pixels are merged into one sprite. Default: 2"))
					+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("MergeDistLabel", "Merge Distance"))
					]
					+ SHorizontalBox::Slot().FillWidth(0.5f)
					[
						SNew(SNumericEntryBox<int32>)
						.Value_Lambda([this]() -> TOptional<int32> { return DetectionParams.IslandMergeDistance; })
						.OnValueCommitted_Lambda([this](int32 V, ETextCommit::Type)
						{
							DetectionParams.IslandMergeDistance = FMath::Max(0, V);
							ReRunDetectionOnSelected();
						})
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text_Lambda([this]()
					{
						return DetectionMode == EBulkDetectionMode::ManualGrid
							? LOCTEXT("ApplyGridBtn", "Apply Grid")
							: LOCTEXT("ReDetectButton", "Re-Detect");
					})
					.OnClicked_Lambda([this]() -> FReply
					{
						ReRunDetectionOnSelected();
						return FReply::Handled();
					})
					.IsEnabled_Lambda([this]() { return SelectedTexture.IsValid(); })
				]
			]
		]
		// Options / summary section
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 8, 4, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("OptionsHeader", "OPTIONS"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))

		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4, 0, 4, 4)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(6)
			[
				SNew(SVerticalBox)
				// Trim checkbox (extract mode only)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SCheckBox)
		
					.IsChecked_Lambda([this]() { return bTrimSprites ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bTrimSprites = (NewState == ECheckBoxState::Checked); })
					.ToolTipText(LOCTEXT("TrimTooltip", "Trim sprites to tight content bounds and store alignment offsets. Produces smaller textures while preserving cross-sheet alignment."))
					[
						SNew(STextBlock).Text(LOCTEXT("TrimLabel", "Trim sprites to content"))
					]
				]
			]
		];

	// ----- Bottom bar: Batch Rename / Organize / PaperZD / De-bake / Auto-Pad / Extract All -----
	const FText ExtractLabel = LOCTEXT("ExtractAllButton", "Extract All");

	TSharedRef<SHorizontalBox> BottomBar = SNew(SHorizontalBox)
		// WHY Extract All is disabled, in the bar right beside it. With 200+ textures a greyed
		// button and no reason is unactionable; this names the condition and the counts.
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(4, 0)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return GetExtractionBlockerText(); })
			.Visibility_Lambda([this]()
			{
				return GetExtractionBlockerText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
			})
			.ToolTipText_Lambda([this]() { return GetExtractionBlockerText(); })
			.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.75f, 0.2f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text(LOCTEXT("BatchRenameButton", "Batch Rename"))
			.ToolTipText(LOCTEXT("BatchRenameTooltip", "Open the batch rename window to rename all textures before extraction."))
			.OnClicked(this, &SBulkSpriteExtractorWindow::OnBatchRenameClicked)

		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text(LOCTEXT("FolderOrganizerButton", "Organize Folders"))
			.ToolTipText(LOCTEXT("FolderOrganizerTooltip", "Open the folder organizer to arrange textures into a custom folder hierarchy."))
			.OnClicked(this, &SBulkSpriteExtractorWindow::OnFolderOrganizerClicked)

		]
		// ADVANCED. Export/Import, De-bake and PaperZD are occasional, expert operations; keeping them
		// as peers of Extract All pushed the bar to eight buttons and buried the everyday four. They
		// live behind one menu, which also gives each a real explanation of why it is disabled instead
		// of a silently greyed button.
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
		[
			SNew(SComboButton)
			.ComboButtonStyle(FAppStyle::Get(), "ComboButton")
			.ButtonContent()
			[
				SNew(STextBlock).Text(LOCTEXT("AdvancedMenuButton", "Advanced"))
			]
			.OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
			{
				FMenuBuilder Menu(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

				Menu.BeginSection(NAME_None, LOCTEXT("AdvancedOrganizationSection", "Organization"));
				Menu.AddMenuEntry(
					LOCTEXT("ExportOrganizationButton", "Export Organization…"),
					LOCTEXT("ExportOrganizationTooltip", "Write the current folder organization - naming settings, the nested folder tree, and every texture's display name and assigned folder - to a JSON file."),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([this]() { OnExportOrganizationClicked(); }),
						FCanExecuteAction::CreateLambda([this]() { return TextureStates.Num() > 0; })));
				Menu.AddMenuEntry(
					LOCTEXT("ImportOrganizationButton", "Import Organization…"),
					LOCTEXT("ImportOrganizationTooltip", "Read a folder organization back from JSON. Textures are matched by display name (then by asset path, then through the file's rename map); anything that cannot be placed is reported rather than dropped silently."),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([this]() { OnImportOrganizationClicked(); }),
						FCanExecuteAction::CreateLambda([this]() { return TextureStates.Num() > 0; })));
				Menu.AddMenuEntry(
					LOCTEXT("AddToCatalogEntry", "Add Character to Catalog"),
					TAttribute<FText>::Create([this]() -> FText
					{
						if (!bLinkToProfile || TargetProfile.IsNull())
						{
							return LOCTEXT("AddToCatalogNeedsProfile", "Link a Character Profile first.");
						}
						if (!ResolveTargetCatalog())
						{
							return LOCTEXT("AddToCatalogNeedsCatalog", "No default Character Catalog is configured (Project Settings > Plugins > Paper2DPlus).");
						}
						if (!CanAddToCatalog())
						{
							return LOCTEXT("AddToCatalogPresent", "This profile is already in the catalog.");
						}
						return LOCTEXT("AddToCatalogTip", "Register the linked Character Profile in the project's Character Catalog.");
					}),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([this]() { OnAddToCatalogClicked(); }),
						FCanExecuteAction::CreateLambda([this]() { return CanAddToCatalog(); })));
				Menu.EndSection();

				Menu.BeginSection(NAME_None, LOCTEXT("AdvancedToolsSection", "Tools"));
				Menu.AddMenuEntry(
					LOCTEXT("DebakeMenuEntry", "De-bake Shared Base…"),
					TAttribute<FText>::Create([this]() -> FText
					{
						const int32 Ungrouped = CountUngroupedSelectedTextures();
						if (Ungrouped >= 2)
						{
							return LOCTEXT("DebakeButtonTooltip", "Group the selected textures as variant sheets of one shared base, then preview the recovered base and per-variant overlays.");
						}
						return LOCTEXT("DebakeNeedsSelection", "Select two or more ungrouped variant sheets first - de-bake recovers one shared base from several baked variants.");
					}),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([this]() { OnCreateDebakeGroupClicked(); }),
						FCanExecuteAction::CreateLambda([this]() { return CountUngroupedSelectedTextures() >= 2; })));
				Menu.AddMenuEntry(
					LOCTEXT("DebakeManageMenuEntry", "Manage De-bake Groups…"),
					LOCTEXT("DebakeManageTooltip", "Open the De-bake window to edit, preview and accept existing groups."),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([this]() { OpenDebakeWindow(); }),
						FCanExecuteAction::CreateLambda([this]() { return DebakeGroups.Num() > 0; })));

				if (bPaperZDAuthoringAvailable)
				{
					Menu.AddMenuEntry(
						LOCTEXT("PaperZDSequencesButton", "PaperZD Sequences…"),
						TAttribute<FText>::Create([this]() -> FText
						{
							if (!bLinkToProfile || TargetProfile.IsNull())
							{
								return LOCTEXT("PaperZDNeedsProfile", "Link a Character Profile first - sequences are created against that profile's PaperZD Anim Source.");
							}
							return LOCTEXT("PaperZDSequencesTooltip", "Choose the linked profile's PaperZD Anim Source, inspect existing matches, and opt into creating sequences for flipbooks produced by this extraction.");
						}),
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda([this]() { OnPaperZDSequencesClicked(); }),
							FCanExecuteAction::CreateLambda([this]()
							{
								return bPaperZDAuthoringAvailable && bLinkToProfile && !TargetProfile.IsNull();
							})));
				}
				Menu.EndSection();

				return Menu.MakeWidget();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text_Lambda([this]() -> FText
			{
				if (!AreAllGridsConfirmed()) return LOCTEXT("AutoPadButton", "Auto-Pad if Needed");
				if (CachedPadCount < 0)
				{
					const TMap<UTexture2D*, FIntPoint> CellMap = BuildPerTextureCellSizeMap();
					const FIntPoint MaxCell = FSpriteExtractionUtils::ComputeGroupMaxCellSize(CellMap);
					int32 Count = 0;
					for (const auto& Entry : CellMap)
					{
						if (Entry.Value.X < MaxCell.X || Entry.Value.Y < MaxCell.Y) Count++;
					}
					CachedPadCount = Count;
				}
				return FText::Format(LOCTEXT("AutoPadButtonFmt", "Auto-Pad if Needed ({0})"), FText::AsNumber(CachedPadCount));
			})
			.ToolTipText(LOCTEXT("AutoPadTooltip", "Compute group-max cell size, detect shared textures, snapshot originals, then pad in-place with rollback on failure."))
			.OnClicked(this, &SBulkSpriteExtractorWindow::OnAutoPadClicked)
			.IsEnabled_Lambda([this]() { return AreAllGridsConfirmed(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text(ExtractLabel)
			// The tooltip names the blocking condition when there is one — a disabled button's own
			// tooltip is the one surface a user reaches for after it greys out.
			.ToolTipText_Lambda([this]() -> FText
			{
				const FText Blocker = GetExtractionBlockerText();
				return Blocker.IsEmpty()
					? LOCTEXT("ExtractAllTooltip", "Extract sprites + flipbooks from all textures. If a CharacterProfile is set, attaches flipbooks to it.")
					: FText::Format(LOCTEXT("ExtractAllBlockedTooltip", "Cannot extract yet \x2014 {0}"), Blocker);
			})
			.OnClicked(this, &SBulkSpriteExtractorWindow::OnCommitClicked)
			.IsEnabled_Lambda([this]() -> bool
			{
				return IsReadyForExtraction();
			})
		];

	// ----- Assemble -----
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			+ SSplitter::Slot().Value(0.13f) [ LeftPane ]
			+ SSplitter::Slot().Value(0.57f) [ CenterPane ]
			// Right pane scrolls: the de-bake group editor can outgrow short windows.
			+ SSplitter::Slot().Value(0.22f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					RightPane
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			BottomBar
		]
	];

	// Deferred auto-select: fire after one frame so the SListView has geometry.
	if (TextureStates.Num() > 0)
	{
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
			[this](double, float) -> EActiveTimerReturnType
			{
				SelectTextureByIndex(0);
				// Seat keyboard focus on this widget so the arrow keys work on a freshly opened
				// window: OnPreviewKeyDown only tunnels along the path to the FOCUSED widget, and
				// with focus still on the bare SWindow this widget is not on that path.
				FSlateApplication::Get().SetKeyboardFocus(SharedThis(this));
				return EActiveTimerReturnType::Stop;
			}));
	}
}

TSharedRef<ITableRow> SBulkSpriteExtractorWindow::GenerateTextureRow(
	TSharedPtr<FBulkExtractorTextureState> InItem,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	using FTextureRow = STableRow<TSharedPtr<FBulkExtractorTextureState>>;

	// The row is built first so the inline rename widget can ask it whether it is the sole selection
	// (click-a-selected-row-again to rename, the engine's own asset-view idiom).
	TSharedRef<FTextureRow> Row = SNew(FTextureRow, OwnerTable);

	TSharedPtr<SInlineEditableTextBlock> NameText;

	TSharedRef<SWidget> RowContent =
		SNew(SHorizontalBox)
		// Status chip. The fill is a FLAT white brush tinted by the status color — the old
		// ToolPanel.GroupBorder is a DARK brush, so tinting it multiplied every status into the same
		// narrow dark band and the hard-black label on top was unreadable.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 2)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor_Lambda([InItem]()
			{
				return InItem.IsValid() ? FSlateColor(GetStatusColor(InItem->Status)) : FSlateColor(FLinearColor::Gray);
			})
			.ToolTipText_Lambda([InItem]()
			{
				if (!InItem.IsValid())
				{
					return FText::GetEmpty();
				}
				return StatusCountsAsConfirmed(InItem->Status)
					? FText::Format(LOCTEXT("StatusChipReadyTip", "{0} \x2014 this row does not block Extract All."), GetStatusLabel(InItem->Status))
					: FText::Format(LOCTEXT("StatusChipBlockingTip", "{0} \x2014 this row BLOCKS Extract All. Select it and press Accept Grid (Enter)."), GetStatusLabel(InItem->Status));
			})
			.Padding(FMargin(6, 2))
			[
				SNew(STextBlock)
				.Text_Lambda([InItem]()
				{
					return InItem.IsValid() ? GetStatusLabel(InItem->Status) : FText::FromString(TEXT("?"));
				})
				.ColorAndOpacity_Lambda([InItem]()
				{
					return InItem.IsValid() ? FSlateColor(GetStatusTextColor(InItem->Status)) : FSlateColor(FLinearColor::Black);
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
			]
		]
		// De-bake group chip (colored dot; tooltip names the group). Resolves live so grouping a
		// texture never requires a row rebuild.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 2, 2, 2)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor_Lambda([this, InItem]()
			{
				const TSharedPtr<FDebakeGroupState> Group = FindGroupContaining(InItem);
				return FSlateColor(Group.IsValid() ? Group->GroupColor : FLinearColor::Transparent);
			})
			.Visibility_Lambda([this, InItem]()
			{
				return FindGroupContaining(InItem).IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.ToolTipText_Lambda([this, InItem]()
			{
				const TSharedPtr<FDebakeGroupState> Group = FindGroupContaining(InItem);
				return Group.IsValid()
					? FText::Format(LOCTEXT("DebakeGroupChipTip", "De-bake group: {0}"), FText::FromString(Group->GroupName))
					: FText::GetEmpty();
			})
			.Padding(FMargin(4, 6))
			[
				SNew(SBox).WidthOverride(0.0f).HeightOverride(0.0f)
			]
		]
		// Low-confidence frame-grid marker. A 200-row batch needs the questionable sheets to be
		// findable from the list itself, not only from the selected texture's readout.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 2, 2, 2)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("\x25B2")))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.75f, 0.2f)))
			.Visibility_Lambda([InItem]()
			{
				return (InItem.IsValid()
					&& InItem->bDetectionRun
					&& !InItem->FrameGridSource.IsEmpty()
					&& !InItem->bFrameGridConfident)
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			.ToolTipText_Lambda([InItem]()
			{
				if (!InItem.IsValid())
				{
					return FText::GetEmpty();
				}
				return FText::Format(
					LOCTEXT("LowConfidenceRowTip", "Frame grid decided by a weak signal (\"{0}\") \x2014 check the cell size before extracting."),
					FText::FromString(InItem->FrameGridSource));
			})
		]
		// Texture name - inline renameable (F2, or click an already-selected row). Writes the same
		// DisplayName field Batch Rename writes, so the two paths cannot produce different names.
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(4, 2)
		[
			SAssignNew(NameText, SInlineEditableTextBlock)
			.Text_Lambda([InItem]() -> FText
			{
				if (!InItem.IsValid())
				{
					return LOCTEXT("NullTextureRow", "(null texture)");
				}
				const FString Label = BulkSpriteExtractor_Internal::StateLabel(InItem);
				return Label.IsEmpty() ? LOCTEXT("NullTextureRow", "(null texture)") : FText::FromString(Label);
			})
			.IsSelected(FIsSelected::CreateSP(Row, &FTextureRow::IsSelectedExclusively))
			.OnTextCommitted_Lambda([this, InItem](const FText& NewText, ETextCommit::Type)
			{
				if (!InItem.IsValid())
				{
					return;
				}
				FString NewName = NewText.ToString();
				// Same normalisation Batch Rename Apply performs, so both routes feed
				// CommitBulkExtract identical asset names.
				FSpriteExtractionUtils::SanitizeAssetName(NewName);
				if (NewName.IsEmpty() || NewName == InItem->DisplayName)
				{
					return;
				}
				InItem->DisplayName = NewName;
				// Re-sorts + re-filters: the row jumps to its alphabetical home and is immediately
				// findable under the new name. RequestListRefresh is deferred to the next tick, so
				// this is safe from inside the committing widget's own callback.
				RefreshFilteredTextureList();
			})
			.ToolTipText(LOCTEXT("TextureRowRenameTip", "Rename for extraction (F2, or click again when selected). This is the batch name only \x2014 the source texture asset is untouched."))
		]
		// Remove button
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.OnClicked_Lambda([this, InItem]() -> FReply
			{
				// Drop the row from any de-bake group first (the group compacts at preview time,
				// but stale weak-refs should not linger in the editor UI).
				if (TSharedPtr<FDebakeGroupState> Group = FindGroupContaining(InItem))
				{
					Group->Members.RemoveAll([&InItem](const FDebakeGroupMember& M)
					{
						return M.State.Pin() == InItem;
					});
					MarkGroupStale(Group);
					RefreshDebakeSection();
				}

				// Capture the neighbour in VISIBLE order BEFORE the removal. Resetting to
				// TextureStates[0] jumped to "the first texture", which is not even the first
				// visible row while a search filter is active.
				const int32 VisibleIndex = FilteredTextureStates.IndexOfByKey(InItem);
				const bool bWasSelected = (SelectedTexture == InItem);

				TextureStates.Remove(InItem);
				TextureNameTexts.Remove(InItem);
				// Removing a row changes both the pad count and the extraction gate.
				InvalidateDerivedCounts();
				RefreshFilteredTextureList();

				if (!bWasSelected)
				{
					return FReply::Handled();
				}

				if (FilteredTextureStates.Num() == 0)
				{
					// Nothing left to land on. Clear the list selection and the canvas - the old
					// code left both showing the texture that had just been removed.
					SelectedTexture = nullptr;
					if (TextureListView.IsValid())
					{
						TextureListView->ClearSelection();
					}
					if (CenterCanvas.IsValid() && !IsDebakePreviewActive())
					{
						CenterCanvas->SetTexture(nullptr);
						CenterCanvas->SetDetectedSprites(TArray<FDetectedSprite>());
						CenterCanvas->Invalidate(EInvalidateWidgetReason::Paint);
					}
					return FReply::Handled();
				}

				// Keep the row that slid into the deleted row's slot, falling back to the new last.
				const int32 NeighbourIndex = FMath::Clamp(
					VisibleIndex == INDEX_NONE ? 0 : VisibleIndex,
					0,
					FilteredTextureStates.Num() - 1);
				SelectTextureState(FilteredTextureStates[NeighbourIndex]);
				return FReply::Handled();
			})
			.ToolTipText(LOCTEXT("RemoveTextureTooltip", "Remove this texture from the batch"))
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("\u00D7")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.3f, 0.3f)))
			]
		];

	Row->SetContent(RowContent);

	if (InItem.IsValid() && NameText.IsValid())
	{
		// Keyed by state identity - rows are virtualized, so an index key would go stale.
		TextureNameTexts.Add(InItem, NameText);
	}

	return Row;
}

void SBulkSpriteExtractorWindow::OnTextureSelectionChanged(
	TSharedPtr<FBulkExtractorTextureState> InItem,
	ESelectInfo::Type SelectInfo)
{
	SelectedTexture = InItem;
	if (!SelectedTexture.IsValid()) return;

	// De-bake follow rules: selecting a member of a group selects that group (and, when previewed,
	// jumps the preview to that member's variant); selecting an ungrouped texture leaves the
	// preview and shows the real texture.
	if (TSharedPtr<FDebakeGroupState> Group = FindGroupContaining(SelectedTexture))
	{
		if (SelectedDebakeGroup != Group)
		{
			SelectDebakeGroup(Group);
		}
		const int32 MemberIndex = Group->Members.IndexOfByPredicate([this](const FDebakeGroupMember& M)
		{
			return M.State.Pin() == SelectedTexture;
		});
		if (MemberIndex != INDEX_NONE)
		{
			DebakePreviewVariantIndex = MemberIndex;
		}
		bShowDebakePreview = (Group->Status == EDebakeGroupStatus::Previewed || Group->Status == EDebakeGroupStatus::Accepted);
	}
	else
	{
		bShowDebakePreview = false;
	}

	// Restore detection params from the selected texture if it was already confirmed.
	if (SelectedTexture->bDetectionRun)
	{
		DetectionParams = SelectedTexture->ConfirmedDetectionParams;
	}

	// Lazy-run detection the first time a texture is focused. Keeps window open fast
	// when a user has many textures — pay the detection cost only when reviewing each.
	if (!SelectedTexture->bDetectionRun)
	{
		RunDetectionAndInference(SelectedTexture);
	}

	// Point the canvas at the new texture + sprite array (the de-bake preview overrides below).
	if (CenterCanvas.IsValid())
	{
		CenterCanvas->SetTexture(SelectedTexture->Texture.LoadSynchronous());
		CenterCanvas->SetDetectedSprites(SelectedTexture->DetectedSprites);
		CenterCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	}
	UpdateCanvasForDebakePreview();

	// Auto-scroll the left-panel list to keep the selected texture visible.
	if (TextureListView.IsValid() && SelectedTexture.IsValid())
	{
		TextureListView->RequestScrollIntoView(SelectedTexture);
	}
}

void SBulkSpriteExtractorWindow::RunDetectionAndInference(TSharedPtr<FBulkExtractorTextureState> State)
{
	if (!State.IsValid()) return;
	UTexture2D* Texture = State->Texture.LoadSynchronous();
	if (!Texture)
	{
		State->Status = EBulkExtractorTextureStatus::Error;
		State->bDetectionRun = true;
		return;
	}

	// Ensure Paper2D settings + CPU access before detection — matches the single-texture
	// extractor's preflight.
	if (FSpriteExtractionUtils::NeedsPaper2DSettings(Texture))
	{
		FSpriteExtractionUtils::ApplyPaper2DSettings(Texture);
	}

	const FIntPoint TexDims = FSpriteExtractionUtils::GetDetectionDimensions(Texture);

	if (DetectionMode == EBulkDetectionMode::Frames)
	{
		// The gutter rule. Answers "where are the FRAMES", which is the question this window is
		// actually asking — island detection answers "where is the ART" and fragments on VFX.
		const FDetectedFrameGrid Result = FSpriteExtractionUtils::DetectFrameGrid(
			Texture,
			BulkSpriteExtractor_Internal::ResolveFrameSizeHint(Texture),
			FrameGridMinCell);

		State->ConfirmedDetectionParams = DetectionParams;
		State->bDetectionRun = true;

		if (!Result.IsValid())
		{
			// Unreadable source data — the same failure island detection reports as Error.
			BulkSpriteExtractor_Internal::ClearFrameGridReadout(State);
			State->DetectedSprites.Empty();
			State->Status = EBulkExtractorTextureStatus::Error;
			return;
		}

		BulkSpriteExtractor_Internal::ApplyFrameGridResult(State, Result);
		// Synthesize the SAME full-cell rects the manual-grid branch produces, so the canvas overlay
		// and CommitBulkExtract's cell math are byte-identical across the two grid-shaped modes.
		BulkSpriteExtractor_Internal::FillFullCellSprites(State, TexDims, Result.Grid);
		State->Status = EBulkExtractorTextureStatus::Inferred;
		return;
	}

	BulkSpriteExtractor_Internal::ClearFrameGridReadout(State);

	if (DetectionMode == EBulkDetectionMode::ManualGrid)
	{
		// F14: clamp the grid to texture dimensions so integer division can never yield a
		// zero-size cell (cols/rows exceeding tex dims would otherwise produce degenerate sprites).
		const int32 Cols = FMath::Clamp(GridSpriteCount, 1, FMath::Max(1, TexDims.X));
		const int32 Rows = FMath::Clamp(GridRowCount, 1, FMath::Max(1, TexDims.Y));

		BulkSpriteExtractor_Internal::FillFullCellSprites(State, TexDims, FIntPoint(Cols, Rows));

		State->InferredGrid = FIntPoint(Cols, Rows);
		State->OverrideGrid = State->InferredGrid;
		State->ConfirmedDetectionParams = DetectionParams;
		State->bDetectionRun = true;
		State->Status = EBulkExtractorTextureStatus::Inferred;
	}
	else
	{
		State->DetectedSprites = FSpriteExtractionUtils::DetectSpriteBounds(Texture, DetectionParams);
		State->ConfirmedDetectionParams = DetectionParams;
		State->bDetectionRun = true;

		if (State->DetectedSprites.Num() < 1)
		{
			State->Status = EBulkExtractorTextureStatus::Error;
			return;
		}

		State->InferredGrid = (State->DetectedSprites.Num() == 1)
			? FIntPoint(1, 1)
			: FSpriteExtractionUtils::InferGridDimensions(State->DetectedSprites);
		State->OverrideGrid = State->InferredGrid;
		State->Status = EBulkExtractorTextureStatus::Inferred;
	}
}

void SBulkSpriteExtractorWindow::RunFrameGridDetectionForAll()
{
	if (TextureStates.Num() == 0)
	{
		return;
	}

	// Candidates cache each sheet's per-axis occupancy, so the consensus pass re-decides without
	// re-reading a single pixel.
	TArray<FFrameGridCandidate> Candidates;
	TArray<TSharedPtr<FBulkExtractorTextureState>> Rows;
	TArray<FString> Unreadable;
	int32 LeftAlone = 0;

	FScopedSlowTask Task(static_cast<float>(TextureStates.Num()),
		LOCTEXT("DetectAllProgress", "Detecting frame grids..."));
	Task.MakeDialog();

	for (const TSharedPtr<FBulkExtractorTextureState>& State : TextureStates)
	{
		Task.EnterProgressFrame(1.0f);
		if (!State.IsValid())
		{
			continue;
		}
		// Never re-decide a grid the user (or the pad pipeline) has already committed to: a padded
		// texture's cell size is load-bearing for every other sheet in the batch.
		if (State->Status == EBulkExtractorTextureStatus::Confirmed
			|| State->Status == EBulkExtractorTextureStatus::SkippedNoPad
			|| State->Status == EBulkExtractorTextureStatus::Padded
			|| State->Status == EBulkExtractorTextureStatus::DebakeSource
			|| State->bUserOverridden)
		{
			++LeftAlone;
			continue;
		}

		UTexture2D* Texture = State->Texture.LoadSynchronous();
		if (!Texture)
		{
			State->Status = EBulkExtractorTextureStatus::Error;
			State->bDetectionRun = true;
			Unreadable.Add(BulkSpriteExtractor_Internal::StateLabel(State));
			continue;
		}
		if (FSpriteExtractionUtils::NeedsPaper2DSettings(Texture))
		{
			FSpriteExtractionUtils::ApplyPaper2DSettings(Texture);
		}

		Candidates.Add(FSpriteExtractionUtils::MakeFrameGridCandidate(
			Texture,
			BulkSpriteExtractor_Internal::ResolveFolderKey(Texture),
			BulkSpriteExtractor_Internal::ResolveFrameSizeHint(Texture),
			FrameGridMinCell));
		Rows.Add(State);
	}

	// Second pass: a folder's agreed frame FORMAT (cell width AND height) rescues its odd sheets.
	FSpriteExtractionUtils::ApplyFolderConsensus(Candidates);

	int32 Detected = 0;
	int32 LowConfidence = 0;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const TSharedPtr<FBulkExtractorTextureState>& State = Rows[Index];
		const FDetectedFrameGrid& Result = Candidates[Index].Result;
		UTexture2D* Texture = State->Texture.Get();
		State->bDetectionRun = true;
		State->ConfirmedDetectionParams = DetectionParams;

		if (!Result.IsValid() || !Texture)
		{
			BulkSpriteExtractor_Internal::ClearFrameGridReadout(State);
			State->DetectedSprites.Empty();
			State->Status = EBulkExtractorTextureStatus::Error;
			Unreadable.Add(BulkSpriteExtractor_Internal::StateLabel(State));
			continue;
		}

		BulkSpriteExtractor_Internal::ApplyFrameGridResult(State, Result);
		BulkSpriteExtractor_Internal::FillFullCellSprites(
			State, FSpriteExtractionUtils::GetDetectionDimensions(Texture), Result.Grid);
		State->Status = EBulkExtractorTextureStatus::Inferred;
		++Detected;
		if (!Result.bConfident)
		{
			++LowConfidence;
		}
	}

	// After the loop, never inside it — the funnel re-sorts TextureStates.
	InvalidateDerivedCounts();
	RefreshFilteredTextureList();
	if (CenterCanvas.IsValid() && SelectedTexture.IsValid())
	{
		CenterCanvas->SetDetectedSprites(SelectedTexture->DetectedSprites);
		CenterCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	}

	FString Summary = FString::Printf(TEXT("Detected frame grids on %d sheet(s)."), Detected);
	if (LowConfidence > 0)
	{
		Summary += FString::Printf(TEXT(" %d need a review (low confidence)."), LowConfidence);
	}
	if (LeftAlone > 0)
	{
		Summary += FString::Printf(TEXT(" %d left alone (already confirmed, padded, or manually overridden)."), LeftAlone);
	}
	if (Unreadable.Num() > 0)
	{
		Summary += FString::Printf(TEXT(" %d could not be read: %s."),
			Unreadable.Num(), *BulkSpriteExtractor_Internal::JoinLabels(Unreadable));
	}
	FNotificationInfo Info(FText::FromString(Summary));
	Info.ExpireDuration = 8.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

FReply SBulkSpriteExtractorWindow::OnAcceptGridClicked()
{
	if (!SelectedTexture.IsValid() || !SelectedTexture->bDetectionRun) return FReply::Handled();

	SelectedTexture->Status = EBulkExtractorTextureStatus::Confirmed;
	SelectedTexture->ConfirmedDetectionParams = DetectionParams;
	InvalidateDerivedCounts();

	// Force the left-pane row + canvas overlay to reflect the new state. Through the funnel so a row
	// that just stopped blocking leaves an active status filter / re-sorts.
	RefreshFilteredTextureList();
	if (CenterCanvas.IsValid()) CenterCanvas->Invalidate(EInvalidateWidgetReason::Paint);

	return FReply::Handled();
}

void SBulkSpriteExtractorWindow::OnColsChanged(int32 NewValue)
{
	if (!SelectedTexture.IsValid() || NewValue <= 0) return;
	if ((SelectedTexture->Status == EBulkExtractorTextureStatus::Padded
		|| SelectedTexture->Status == EBulkExtractorTextureStatus::SkippedNoPad)
		&& !InvalidatePaddingForGridEdit())
	{
		SelectedTexture->Status = EBulkExtractorTextureStatus::Error;
		return;
	}
	SelectedTexture->OverrideGrid.X = NewValue;
	SelectedTexture->bUserOverridden = true;
	InvalidateDerivedCounts();
	if (SelectedTexture->Status == EBulkExtractorTextureStatus::Confirmed
		|| SelectedTexture->Status == EBulkExtractorTextureStatus::Inferred
		|| SelectedTexture->Status == EBulkExtractorTextureStatus::SkippedNoPad
		|| SelectedTexture->Status == EBulkExtractorTextureStatus::Padded)
	{
		SelectedTexture->Status = EBulkExtractorTextureStatus::Overridden;
	}
	if (CenterCanvas.IsValid()) CenterCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	RefreshFilteredTextureList();
}

void SBulkSpriteExtractorWindow::OnRowsChanged(int32 NewValue)
{
	if (!SelectedTexture.IsValid() || NewValue <= 0) return;
	if ((SelectedTexture->Status == EBulkExtractorTextureStatus::Padded
		|| SelectedTexture->Status == EBulkExtractorTextureStatus::SkippedNoPad)
		&& !InvalidatePaddingForGridEdit())
	{
		SelectedTexture->Status = EBulkExtractorTextureStatus::Error;
		return;
	}
	SelectedTexture->OverrideGrid.Y = NewValue;
	SelectedTexture->bUserOverridden = true;
	InvalidateDerivedCounts();
	if (SelectedTexture->Status == EBulkExtractorTextureStatus::Confirmed
		|| SelectedTexture->Status == EBulkExtractorTextureStatus::Inferred
		|| SelectedTexture->Status == EBulkExtractorTextureStatus::SkippedNoPad
		|| SelectedTexture->Status == EBulkExtractorTextureStatus::Padded)
	{
		SelectedTexture->Status = EBulkExtractorTextureStatus::Overridden;
	}
	if (CenterCanvas.IsValid()) CenterCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	RefreshFilteredTextureList();
}

TOptional<int32> SBulkSpriteExtractorWindow::GetColsValue() const
{
	if (!SelectedTexture.IsValid()) return TOptional<int32>();
	return SelectedTexture->GetEffectiveGrid().X;
}

TOptional<int32> SBulkSpriteExtractorWindow::GetRowsValue() const
{
	if (!SelectedTexture.IsValid()) return TOptional<int32>();
	return SelectedTexture->GetEffectiveGrid().Y;
}

FText SBulkSpriteExtractorWindow::GetDivisibilityWarning() const
{
	if (!SelectedTexture.IsValid() || !SelectedTexture->bDetectionRun) return FText::GetEmpty();
	UTexture2D* Tex = SelectedTexture->Texture.LoadSynchronous();
	if (!Tex) return FText::GetEmpty();
	const FIntPoint Dims = FSpriteExtractionUtils::GetDetectionDimensions(Tex);
	const FIntPoint Grid = SelectedTexture->GetEffectiveGrid();
	if (Grid.X <= 0 || Grid.Y <= 0) return FText::GetEmpty();
	// F14: a grid larger than the texture truncates the cell size to 0 — surface this as a hard
	// warning (the texture is skipped at commit rather than emitting degenerate sprites).
	if (Grid.X > Dims.X || Grid.Y > Dims.Y)
	{
		return FText::Format(
			LOCTEXT("GridExceedsTexWarning", "Grid {0}x{1} exceeds texture {2}x{3} \x2014 cells collapse to 0px and this texture will be skipped. Reduce columns/rows."),
			Grid.X, Grid.Y, Dims.X, Dims.Y);
	}
	const int32 RemX = Dims.X % Grid.X;
	const int32 RemY = Dims.Y % Grid.Y;
	if (RemX == 0 && RemY == 0) return FText::GetEmpty();
	return FText::Format(
		LOCTEXT("DivisibilityWarning", "Grid doesn't divide texture cleanly: {0}x{1} tex ÷ {2} cols, {3} rows → {4}px × {5}px remainder. Usually intentional for bleed margins."),
		Dims.X, Dims.Y, Grid.X, Grid.Y, RemX, RemY);
}

bool SBulkSpriteExtractorWindow::AreAllGridsConfirmed() const
{
	if (TextureStates.Num() == 0) return false;
	for (const TSharedPtr<FBulkExtractorTextureState>& S : TextureStates)
	{
		if (!S.IsValid()) return false;
		if (!StatusCountsAsConfirmed(S->Status))
		{
			return false;
		}
	}
	return true;
}

bool SBulkSpriteExtractorWindow::IsReadyForExtraction() const
{
	// The gate IS the reason. Deriving one from the other is what stops the greyed-out button and
	// the explanation beside it from ever disagreeing.
	return GetExtractionBlockerText().IsEmpty();
}

FText SBulkSpriteExtractorWindow::GetExtractionBlockerText() const
{
	// Memoized: this walks every row and resolves every soft texture pointer, and it is read from
	// paint-time lambdas (button enablement, button tooltip, the bottom-bar status line). The memo
	// drops through InvalidateDerivedCounts(), the same seam CachedPadCount uses.
	if (!bCachedGateValid)
	{
		CachedBlockerText = ComputeExtractionBlockerText();
		bCachedGateValid = true;
	}
	return CachedBlockerText;
}

FText SBulkSpriteExtractorWindow::ComputeExtractionBlockerText() const
{
	if (TextureStates.Num() == 0)
	{
		return LOCTEXT("GateNoTextures", "No textures loaded.");
	}

	// 1. Unconfirmed grids. Counted over the FULL batch, never FilteredTextureStates — a search or
	//    status filter must not understate how many rows are actually blocking.
	int32 Unconfirmed = 0;
	int32 Errored = 0;
	TArray<FString> UnconfirmedLabels;
	for (const TSharedPtr<FBulkExtractorTextureState>& State : TextureStates)
	{
		if (!State.IsValid() || !StatusCountsAsConfirmed(State->Status))
		{
			++Unconfirmed;
			UnconfirmedLabels.Add(BulkSpriteExtractor_Internal::StateLabel(State));
		}
		if (State.IsValid() && State->Status == EBulkExtractorTextureStatus::Error)
		{
			++Errored;
		}
	}
	if (Unconfirmed > 0)
	{
		const FText Base = FText::Format(
			LOCTEXT("GateUnconfirmed", "{0} of {1} unconfirmed \x2014 select a row and press Accept Grid (Enter). {2}"),
			FText::AsNumber(Unconfirmed),
			FText::AsNumber(TextureStates.Num()),
			FText::FromString(BulkSpriteExtractor_Internal::JoinLabels(UnconfirmedLabels)));
		return Errored > 0
			? FText::Format(
				LOCTEXT("GateUnconfirmedWithErrors", "{0}  ({1} failed detection.)"),
				Base,
				FText::AsNumber(Errored))
			: Base;
	}

	// 2. Rows the cell-size map had to drop. These previously vanished with nothing but a LogTemp
	//    line, so the button greyed out with no visible cause anywhere in the UI.
	TArray<FString> Skipped;
	const TMap<UTexture2D*, FIntPoint> CellSizes = BuildPerTextureCellSizeMap(&Skipped);
	if (Skipped.Num() > 0)
	{
		return FText::Format(
			LOCTEXT("GateSkippedRows", "{0} row(s) produce no usable frame cell \x2014 the grid does not divide the texture, the texture will not load, or it is listed twice: {1}"),
			FText::AsNumber(Skipped.Num()),
			FText::FromString(BulkSpriteExtractor_Internal::JoinLabels(Skipped)));
	}

	// 3. Nothing extractable at all. AreCellSizesUniform reports an empty map as non-uniform, so
	//    this case has to be named before the size comparison or it reads as "sizes differ: ".
	if (CellSizes.Num() == 0)
	{
		return LOCTEXT("GateNothingToExtract", "Nothing to extract \x2014 every row is an accepted de-bake source. Its recovered outputs carry the animation instead.");
	}

	// 4. Cell sizes disagree. Name the sizes and how many rows sit on each, biggest group first —
	//    that is what tells the user whether to pad or to fix a handful of outliers.
	if (!FSpriteExtractionUtils::AreCellSizesUniform(CellSizes))
	{
		TMap<FIntPoint, int32> SizeCounts;
		for (const TPair<UTexture2D*, FIntPoint>& Entry : CellSizes)
		{
			++SizeCounts.FindOrAdd(Entry.Value);
		}
		TArray<TPair<FIntPoint, int32>> Sorted;
		Sorted.Reserve(SizeCounts.Num());
		for (const TPair<FIntPoint, int32>& Entry : SizeCounts)
		{
			Sorted.Add(Entry);
		}
		Sorted.Sort([](const TPair<FIntPoint, int32>& A, const TPair<FIntPoint, int32>& B)
		{
			if (A.Value != B.Value) return A.Value > B.Value;
			if (A.Key.X != B.Key.X) return A.Key.X > B.Key.X;
			return A.Key.Y > B.Key.Y;
		});

		FString Summary;
		const int32 Shown = FMath::Min(Sorted.Num(), 4);
		for (int32 Index = 0; Index < Shown; ++Index)
		{
			if (Index > 0) Summary += TEXT(", ");
			Summary += FString::Printf(TEXT("%dx%d (%d)"), Sorted[Index].Key.X, Sorted[Index].Key.Y, Sorted[Index].Value);
		}
		if (Sorted.Num() > Shown)
		{
			Summary += FString::Printf(TEXT(", and %d more"), Sorted.Num() - Shown);
		}
		return FText::Format(
			LOCTEXT("GateCellSizesDiffer", "Cell sizes differ: {0} \x2014 run Auto-Pad if Needed."),
			FText::FromString(Summary));
	}

	return FText::GetEmpty();
}

bool SBulkSpriteExtractorWindow::StatusCountsAsConfirmed(EBulkExtractorTextureStatus Status)
{
	// DebakeSource satisfies the gate: a consumed source no longer needs a confirmed grid (its
	// de-baked outputs carry their own).
	return Status == EBulkExtractorTextureStatus::Confirmed
		|| Status == EBulkExtractorTextureStatus::SkippedNoPad
		|| Status == EBulkExtractorTextureStatus::Padded
		|| Status == EBulkExtractorTextureStatus::DebakeSource;
}

bool FDebakeGroupState::ConsumesTexturePath(const FSoftObjectPath& Path) const
{
	if (Path.IsNull())
	{
		return false;
	}

	for (const FDebakeGroupMember& Member : Members)
	{
		if (const TSharedPtr<FBulkExtractorTextureState> State = Member.State.Pin())
		{
			if (State->Texture.ToSoftObjectPath() == Path)
			{
				return true;
			}
		}
		if (Member.VfxFront.ToSoftObjectPath() == Path || Member.VfxBack.ToSoftObjectPath() == Path)
		{
			return true;
		}
	}
	return false;
}

bool SBulkSpriteExtractorWindow::StatusExcludedFromExtract(EBulkExtractorTextureStatus Status)
{
	return Status == EBulkExtractorTextureStatus::DebakeSource;
}

TMap<UTexture2D*, FIntPoint> SBulkSpriteExtractorWindow::BuildPerTextureCellSizeMap(TArray<FString>* OutSkipped) const
{
	TMap<UTexture2D*, FIntPoint> Out;
	if (OutSkipped)
	{
		OutSkipped->Reset();
	}
	// Every early-out below shrinks the map relative to the batch, which silently blocks Extract All.
	// Reporting the row here is what lets GetExtractionBlockerText name it instead of leaving the
	// user with a greyed button and a LogTemp line they never see.
	auto ReportSkipped = [OutSkipped](const TSharedPtr<FBulkExtractorTextureState>& S)
	{
		if (OutSkipped)
		{
			OutSkipped->Add(BulkSpriteExtractor_Internal::StateLabel(S));
		}
	};

	for (const TSharedPtr<FBulkExtractorTextureState>& S : TextureStates)
	{
		if (!S.IsValid()) continue;
		// Consumed de-bake sources must not drag the group-max cell size (they are not extracted).
		if (StatusExcludedFromExtract(S->Status)) continue;
		UTexture2D* Tex = S->Texture.LoadSynchronous();
		if (!Tex)
		{
			ReportSkipped(S);
			continue;
		}
		const FIntPoint Dims = FSpriteExtractionUtils::GetDetectionDimensions(Tex);
		const FIntPoint Grid = S->GetEffectiveGrid();
		if (Grid.X <= 0 || Grid.Y <= 0)
		{
			ReportSkipped(S);
			continue;
		}
		if (Dims.X % Grid.X != 0 || Dims.Y % Grid.Y != 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("BulkExtractor CellSizeMap: skipping %s — grid %dx%d does not divide texture %dx%d exactly."),
				*Tex->GetName(), Grid.X, Grid.Y, Dims.X, Dims.Y);
			ReportSkipped(S);
			continue;
		}
		const FIntPoint CellSize(Dims.X / Grid.X, Dims.Y / Grid.Y);
		// F14: an oversized grid yields a 0-size cell — exclude it so it can't pollute the group-max
		// cell size or the auto-pad pipeline (the commit path skips such textures as well).
		if (CellSize.X <= 0 || CellSize.Y <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("BulkExtractor CellSizeMap: skipping %s \x2014 grid %dx%d exceeds texture %dx%d (degenerate cell)."),
				*Tex->GetName(), Grid.X, Grid.Y, Dims.X, Dims.Y);
			ReportSkipped(S);
			continue;
		}
		// Two rows sharing one source texture collapse onto a single map entry, which the extraction
		// gate reads as "a row went missing". Report the duplicate rather than let it block silently.
		if (Out.Contains(Tex))
		{
			ReportSkipped(S);
			continue;
		}
		Out.Add(Tex, CellSize);
	}
	return Out;
}

bool SBulkSpriteExtractorWindow::RestoreCurrentPadSnapshots()
{
	if (CurrentPadManifests.Num() == 0)
	{
		PerTextureSourceCellSizes.Empty();
		PerTexturePasteOffsets.Empty();
		if (CurrentPadRunGuid.IsValid())
		{
			const FString RunDir = FPaths::Combine(
				FPaths::ProjectSavedDir(), TEXT("Paper2DPlus"), TEXT("PadBackups"), CurrentPadRunGuid.ToString());
			IFileManager::Get().DeleteDirectory(*RunDir, /*RequireExists=*/false, /*Tree=*/true);
			CurrentPadRunGuid.Invalidate();
		}
		bLastPadRollbackComplete = true;
		return true;
	}

	TArray<FPadSnapshotManifest> FailedManifests;
	for (const FPadSnapshotManifest& Manifest : CurrentPadManifests)
	{
		if (FSpriteExtractionUtils::RestoreTextureSnapshot(Manifest))
		{
			IFileManager::Get().Delete(*Manifest.SidecarPath);
		}
		else
		{
			FailedManifests.Add(Manifest);
			UE_LOG(LogTemp, Error, TEXT("BulkExtractor: failed to restore pad snapshot '%s'."),
				*Manifest.SidecarPath);
		}
	}

	CurrentPadManifests = MoveTemp(FailedManifests);
	bLastPadRollbackComplete = CurrentPadManifests.Num() == 0;
	if (bLastPadRollbackComplete)
	{
		PerTextureSourceCellSizes.Empty();
		PerTexturePasteOffsets.Empty();
		if (CurrentPadRunGuid.IsValid())
		{
			const FString RunDir = FPaths::Combine(
				FPaths::ProjectSavedDir(), TEXT("Paper2DPlus"), TEXT("PadBackups"), CurrentPadRunGuid.ToString());
			IFileManager::Get().DeleteDirectory(*RunDir, /*RequireExists=*/false, /*Tree=*/true);
		}
		CurrentPadRunGuid.Invalidate();
	}
	return bLastPadRollbackComplete;
}

void SBulkSpriteExtractorWindow::DiscardCurrentPadSnapshots()
{
	for (const FPadSnapshotManifest& Manifest : CurrentPadManifests)
	{
		IFileManager::Get().Delete(*Manifest.SidecarPath);
	}
	if (CurrentPadRunGuid.IsValid())
	{
		const FString RunDir = FPaths::Combine(
			FPaths::ProjectSavedDir(), TEXT("Paper2DPlus"), TEXT("PadBackups"), CurrentPadRunGuid.ToString());
		IFileManager::Get().DeleteDirectory(*RunDir, /*RequireExists=*/false, /*Tree=*/true);
	}
	CurrentPadManifests.Empty();
	PerTextureSourceCellSizes.Empty();
	PerTexturePasteOffsets.Empty();
	CurrentPadRunGuid.Invalidate();
	bLastPadRollbackComplete = true;
}

bool SBulkSpriteExtractorWindow::InvalidatePaddingForGridEdit()
{
	const bool bRestored = RestoreCurrentPadSnapshots();
	for (const TSharedPtr<FBulkExtractorTextureState>& State : TextureStates)
	{
		if (!State.IsValid())
		{
			continue;
		}
		if (State->Status == EBulkExtractorTextureStatus::Padded
			|| State->Status == EBulkExtractorTextureStatus::SkippedNoPad)
		{
			State->Status = bRestored
				? EBulkExtractorTextureStatus::Confirmed
				: EBulkExtractorTextureStatus::Error;
		}
	}
	InvalidateDerivedCounts();
	if (!bRestored)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("PadRestoreFailedOnGridEdit", "The padded textures could not all be restored, so the requested change was blocked. Snapshot files were retained for recovery."));
	}
	return bRestored;
}

FReply SBulkSpriteExtractorWindow::OnAutoPadClicked()
{
	// Auto-pad is a whole-group calculation. If this window already owns a transient pad run, first
	// restore it and recompute from the original dimensions; otherwise a second run would build its
	// ToPad list from a mixture of padded and unpadded sheets.
	if (CurrentPadManifests.Num() > 0 && !InvalidatePaddingForGridEdit())
	{
		return FReply::Handled();
	}

	// 1. Compute per-axis group max from confirmed per-texture cell sizes.
	const TMap<UTexture2D*, FIntPoint> CellSizeMap = BuildPerTextureCellSizeMap();
	const FIntPoint MaxCell = FSpriteExtractionUtils::ComputeGroupMaxCellSize(CellSizeMap);
	int32 IncludedTextureCount = 0;
	for (const TSharedPtr<FBulkExtractorTextureState>& State : TextureStates)
	{
		if (State.IsValid() && !StatusExcludedFromExtract(State->Status))
		{
			++IncludedTextureCount;
		}
	}
	if (CellSizeMap.Num() != IncludedTextureCount || MaxCell.X <= 0 || MaxCell.Y <= 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("AutoPadInvalidGrid", "Auto-Pad cannot run because at least one confirmed grid does not divide its texture into positive whole-pixel cells. Correct that grid first."));
		return FReply::Handled();
	}

	// 2. Build ToPadList — textures whose cell is smaller than the group max on either axis.
	TArray<UTexture2D*> ToPadList;
	for (const TPair<UTexture2D*, FIntPoint>& Entry : CellSizeMap)
	{
		if (Entry.Value.X < MaxCell.X || Entry.Value.Y < MaxCell.Y)
		{
			ToPadList.Add(Entry.Key);
		}
	}

	if (ToPadList.Num() == 0)
	{
		for (const TSharedPtr<FBulkExtractorTextureState>& S : TextureStates)
		{
			if (S.IsValid() && S->Status == EBulkExtractorTextureStatus::Confirmed)
			{
				S->Status = EBulkExtractorTextureStatus::SkippedNoPad;
			}
		}
		// After the loop, never inside it: the funnel re-sorts TextureStates.
		InvalidateDerivedCounts();
		RefreshFilteredTextureList();
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoPadNeeded", "All textures already match the group maximum cell size. No pad needed."));
		return FReply::Handled();
	}

	// 3. Shared-texture guard: find sister profiles that also reference each target.
	TMap<UTexture2D*, TArray<UPaper2DPlusCharacterProfileAsset*>> SisterProfilesByTexture;
	UPaper2DPlusCharacterProfileAsset* InitiatingProfile = TargetProfile.LoadSynchronous();
	for (UTexture2D* Tex : ToPadList)
	{
		TArray<UPaper2DPlusCharacterProfileAsset*> All = FSpriteExtractionUtils::FindProfilesReferencingTexture(Tex);
		All.RemoveAll([InitiatingProfile](UPaper2DPlusCharacterProfileAsset* P) { return P == InitiatingProfile; });
		if (All.Num() > 0) SisterProfilesByTexture.Add(Tex, MoveTemp(All));
	}

	// 3b. Always show confirmation popup with removable texture rows.
	{
		bool bPadConfirmed = false;
		TSharedRef<SWindow> PadConfirmWindow = SNew(SWindow)
			.Title(LOCTEXT("PadConfirmTitle", "Confirm Auto-Pad"))
			.ClientSize(FVector2D(550, 400))
			.SupportsMinimize(false).SupportsMaximize(false);

		TSharedPtr<SVerticalBox> PadListBox;

		TFunction<void()> RebuildPadRows;
		RebuildPadRows = [&ToPadList, &CellSizeMap, &MaxCell, &SisterProfilesByTexture, &PadListBox, &PadConfirmWindow, &RebuildPadRows]()
		{
			if (!PadListBox.IsValid()) return;
			PadListBox->ClearChildren();
			for (int32 i = 0; i < ToPadList.Num(); i++)
			{
				UTexture2D* Tex = ToPadList[i];
				const FIntPoint* CellSize = CellSizeMap.Find(Tex);
				const FString TexName = Tex->GetName();
				// Passive pixel-to-world readout (TASK-156 U8). Extraction hard-codes
				// pixels-per-unit to 1, so a padded cell's world size is its pixel size -- but the
				// readout NAMES the value it used rather than assuming it, because that is a
				// property of this extractor and not of sprites in general. Purely descriptive:
				// nothing here changes what gets extracted.
				FString SizeInfo = CellSize
					? FString::Printf(TEXT("%dx%d -> %dx%d   (%s)"),
						CellSize->X, CellSize->Y, MaxCell.X, MaxCell.Y,
						*Paper2DPlusCharacterSizing::FormatPixelSizeReadout(
							MaxCell, BulkSpriteExtractor_Internal::ExtractionPixelsPerUnit()))
					: TEXT("");
				FString SharedWarning;
				if (const TArray<UPaper2DPlusCharacterProfileAsset*>* Sisters = SisterProfilesByTexture.Find(Tex))
				{
					TArray<FString> Names;
					for (auto* P : *Sisters) Names.Add(P->GetName());
					SharedWarning = FString::Printf(TEXT("Shared: %s"), *FString::Join(Names, TEXT(", ")));
				}
				PadListBox->AddSlot()
				.AutoHeight()
				.Padding(0, 1)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
					.Padding(FMargin(6, 3))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f)
								[
									SNew(STextBlock)
									.Text(FText::FromString(TexName))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
								]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SNew(STextBlock)
									.Text(FText::FromString(SizeInfo))
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
									.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(SharedWarning))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.5f, 0.2f)))
								.Visibility(SharedWarning.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.OnClicked_Lambda([i, &ToPadList, &RebuildPadRows]() -> FReply
							{
								if (ToPadList.IsValidIndex(i))
								{
									ToPadList.RemoveAt(i);
									RebuildPadRows();
								}
								return FReply::Handled();
							})
							[
								SNew(SImage)
								.Image(FAppStyle::Get().GetBrush("Icons.X"))
								.DesiredSizeOverride(FVector2D(12, 12))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.2f, 0.2f)))
							]
						]
					]
				];
			}
		};

		PadConfirmWindow->SetContent(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
			[
				SNew(STextBlock)
				.Text_Lambda([&ToPadList, MaxCell]() -> FText
				{
					return FText::Format(LOCTEXT("PadConfirmHeader", "The following {0} texture(s) will be padded to {1}x{2}:"),
						FText::AsNumber(ToPadList.Num()), FText::AsNumber(MaxCell.X), FText::AsNumber(MaxCell.Y));
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8, 0)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(PadListBox, SVerticalBox)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("PadConfirmOk", "Pad"))
					.OnClicked_Lambda([&bPadConfirmed, PadConfirmWindow]() -> FReply
					{
						bPadConfirmed = true;
						PadConfirmWindow->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("PadConfirmCancel", "Cancel"))
					.OnClicked_Lambda([PadConfirmWindow]() -> FReply
					{
						PadConfirmWindow->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		);

		RebuildPadRows();
		FSlateApplication::Get().AddModalWindow(PadConfirmWindow, AsShared());

		if (!bPadConfirmed || ToPadList.Num() == 0)
		{
			return FReply::Handled();
		}
	}

	// 4. Run the three-phase pad pipeline. Failures rollback all snapshots and abort.
	// There is no ground-plane step: the pad is midpoint-to-midpoint, so nothing about the anchor
	// depends on where any cell's content bottom sits. The retired step loaded EVERY already-max-size
	// reference texture's pixels and ran a per-cell tight-bounds scan purely to produce a median
	// PadTextureInPlace then discarded — the slowest button in the window paying for a dead number.
	const bool bOk = RunThreePhasePadPipeline(ToPadList, MaxCell);
	if (!bOk)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			bLastPadRollbackComplete
				? LOCTEXT("PadFailed", "Auto-pad failed and every changed texture was restored.")
				: LOCTEXT("PadRestoreIncomplete", "Auto-pad failed and at least one texture could not be restored. Extraction is blocked and the remaining snapshot files were retained for recovery."));
	}
	InvalidateDerivedCounts();
	RefreshFilteredTextureList();
	if (CenterCanvas.IsValid() && SelectedTexture.IsValid())
	{
		CenterCanvas->SetDetectedSprites(SelectedTexture->DetectedSprites);
		CenterCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	}
	return FReply::Handled();
}

bool SBulkSpriteExtractorWindow::RunThreePhasePadPipeline(
	const TArray<UTexture2D*>& ToPadList,
	FIntPoint MaxCellDims)
{
	// One active pad run per window. Never discard the only restore authority: a replacement run
	// starts only after the previous snapshots restore successfully.
	if (CurrentPadManifests.Num() > 0 && !RestoreCurrentPadSnapshots())
	{
		return false;
	}
	PerTextureSourceCellSizes.Empty();
	PerTexturePasteOffsets.Empty();
	bLastPadRollbackComplete = true;
	CurrentPadRunGuid = FGuid::NewGuid();

	const FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(),
		TEXT("Paper2DPlus"), TEXT("PadBackups"), CurrentPadRunGuid.ToString());

	FScopedSlowTask SlowTask(static_cast<float>(ToPadList.Num() * 3),
		LOCTEXT("PadProgress", "Auto-padding textures..."));
	SlowTask.MakeDialog(/*bShowCancelButton=*/true);

	// Phase 6a — preflight: can we read each texture's Source data?
	for (UTexture2D* Tex : ToPadList)
	{
		SlowTask.EnterProgressFrame(1.0f, FText::Format(
			LOCTEXT("PadPreflight", "Preflight {0}"), FText::FromString(Tex->GetName())));
		if (SlowTask.ShouldCancel())
		{
			RestoreCurrentPadSnapshots();
			return false;
		}

		TArray<FColor> DummyPixels;
		int32 DummyW = 0, DummyH = 0;
		if (!FSpriteExtractionUtils::LoadTextureData(Tex, DummyPixels, DummyW, DummyH))
		{
			UE_LOG(LogTemp, Error, TEXT("BulkExtractor: preflight failed on %s — Source data unavailable."), *Tex->GetName());
			RestoreCurrentPadSnapshots();
			return false;
		}
	}

	// Phase 6b — snapshot acquire. On any failure, delete already-written snapshots + abort.
	for (UTexture2D* Tex : ToPadList)
	{
		SlowTask.EnterProgressFrame(1.0f, FText::Format(
			LOCTEXT("PadSnapshot", "Snapshot {0}"), FText::FromString(Tex->GetName())));
		if (SlowTask.ShouldCancel())
		{
			// Clean up any snapshots already written.
			for (const FPadSnapshotManifest& Done : CurrentPadManifests)
			{
				IFileManager::Get().Delete(*Done.SidecarPath);
			}
			CurrentPadManifests.Empty();
			RestoreCurrentPadSnapshots();
			return false;
		}

		FPadSnapshotManifest Manifest = FSpriteExtractionUtils::SnapshotTexture(
			Tex, CurrentPadRunGuid, SaveDir, TargetProfile);
		if (!Manifest.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("BulkExtractor: snapshot failed on %s."), *Tex->GetName());
			for (const FPadSnapshotManifest& Done : CurrentPadManifests)
			{
				IFileManager::Get().Delete(*Done.SidecarPath);
			}
			CurrentPadManifests.Empty();
			RestoreCurrentPadSnapshots();
			return false;
		}
		CurrentPadManifests.Add(Manifest);
	}

	// Phase 6c — pad apply. Cancel disabled once writes begin; failures trigger rollback.
	SlowTask.MakeDialog(/*bShowCancelButton=*/false);
	for (int32 i = 0; i < ToPadList.Num(); i++)
	{
		UTexture2D* Tex = ToPadList[i];
		SlowTask.EnterProgressFrame(1.0f, FText::Format(
			LOCTEXT("PadApply", "Padding {0} ({1}/{2})"),
			FText::FromString(Tex->GetName()), i + 1, ToPadList.Num()));

		// Find this texture's grid from the state list.
		FIntPoint TexGrid = FIntPoint(1, 1);
		for (const TSharedPtr<FBulkExtractorTextureState>& S : TextureStates)
		{
			if (S.IsValid() && S->Texture.LoadSynchronous() == Tex)
			{
				TexGrid = S->GetEffectiveGrid();
				break;
			}
		}

		const FIntPoint PrePadDims = FSpriteExtractionUtils::GetDetectionDimensions(Tex);
		UE_LOG(LogTemp, Log, TEXT("BulkExtractor Pad: %s — PrePadDims=%dx%d Grid=%dx%d MaxCellDims=%dx%d"),
			*Tex->GetName(), PrePadDims.X, PrePadDims.Y, TexGrid.X, TexGrid.Y, MaxCellDims.X, MaxCellDims.Y);

		TArray<FIntPoint> CellPasteDeltas;
		// The ground-plane parameter is deprecated and ignored — the pad is midpoint-to-midpoint.
		const FIntPoint FirstPasteDelta = FSpriteExtractionUtils::PadTextureInPlace(
			Tex, MaxCellDims, TexGrid, /*GroundPlaneOffset=*/0, &CellPasteDeltas);

		const FIntPoint PostPadDims = FSpriteExtractionUtils::GetDetectionDimensions(Tex);
		UE_LOG(LogTemp, Log, TEXT("BulkExtractor Pad: %s — PostPadDims=%dx%d FirstPasteDelta=%d,%d Cells=%d"),
			*Tex->GetName(), PostPadDims.X, PostPadDims.Y,
			FirstPasteDelta.X, FirstPasteDelta.Y, CellPasteDeltas.Num());

		const FIntPoint ExpectedTexDims(TexGrid.X * MaxCellDims.X, TexGrid.Y * MaxCellDims.Y);
		if (PostPadDims != ExpectedTexDims || CellPasteDeltas.Num() != TexGrid.X * TexGrid.Y)
		{
			UE_LOG(LogTemp, Error, TEXT("BulkExtractor: pad failed on %s — rolling back."), *Tex->GetName());
			bLastPadRollbackComplete = RestoreCurrentPadSnapshots();
			return false;
		}
		PerTextureSourceCellSizes.Add(
			Tex,
			FIntPoint(PrePadDims.X / TexGrid.X, PrePadDims.Y / TexGrid.Y));
		PerTexturePasteOffsets.Add(Tex, MoveTemp(CellPasteDeltas));

		// Update per-texture state chip and re-run detection on the padded texture
		// so the canvas shows tight content bounds within the new cells.
		for (const TSharedPtr<FBulkExtractorTextureState>& S : TextureStates)
		{
			if (S.IsValid() && S->Texture.LoadSynchronous() == Tex)
			{
				const bool bWasUserOverridden = S->bUserOverridden;
				// Restore per-texture detection params before re-running so the result
				// matches what the user confirmed during manual review.
				DetectionParams = S->ConfirmedDetectionParams;
				RunDetectionAndInference(S);
				// Detection updates preview islands, but the user's confirmed grid remains the
				// authority. RunDetectionAndInference otherwise replaces Padded with Inferred.
				S->InferredGrid = TexGrid;
				S->OverrideGrid = TexGrid;
				S->bUserOverridden = bWasUserOverridden;
				S->Status = EBulkExtractorTextureStatus::Padded;
				break;
			}
		}
	}

	// Only naturally-max textures advance to SkippedNoPad. A row removed from the confirmation
	// dialog stays Confirmed, so the live uniform-cell extraction gate remains closed.
	const TMap<UTexture2D*, FIntPoint> CurrentCellSizes = BuildPerTextureCellSizeMap();
	const FIntPoint CurrentMaxCell = FSpriteExtractionUtils::ComputeGroupMaxCellSize(CurrentCellSizes);
	for (const TSharedPtr<FBulkExtractorTextureState>& S : TextureStates)
	{
		if (S.IsValid() && S->Status == EBulkExtractorTextureStatus::Confirmed)
		{
			UTexture2D* Texture = S->Texture.LoadSynchronous();
			const FIntPoint* CellSize = CurrentCellSizes.Find(Texture);
			if (CellSize && *CellSize == CurrentMaxCell)
			{
				S->Status = EBulkExtractorTextureStatus::SkippedNoPad;
			}
		}
	}
	InvalidateDerivedCounts();

	return true;
}

FReply SBulkSpriteExtractorWindow::OnPaperZDSequencesClicked()
{
	// Re-check on action as well as at construction. This keeps the optional boundary fail-closed if
	// the plugin is disabled or its reflected schema becomes unavailable during the editor session.
	bPaperZDAuthoringAvailable =
		Paper2DPlus::PaperZDSequenceAuthoring::IsOptionalAuthoringAvailable();
	if (!bPaperZDAuthoringAvailable
		|| !bLinkToProfile
		|| TargetProfile.IsNull())
	{
		return FReply::Handled();
	}

	UPaper2DPlusCharacterProfileAsset* Profile = TargetProfile.LoadSynchronous();
	if (!Profile)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT(
				"PaperZDProfileUnavailable",
				"The linked Character Profile could not be loaded."));
		return FReply::Handled();
	}
	if (PaperZDConfiguredProfile.Get() != Profile)
	{
		bCreatePaperZDSequencesAfterExtract = true;
		PaperZDConfiguredProfile.Reset(Profile);
	}

	TSharedRef<FCharacterProfileEditorModel> ProfileModel =
		MakeShared<FCharacterProfileEditorModel>();
	ProfileModel->InitializeFromAsset(Profile);

	// One snapshot of the SHARED sequence-work summary — the same answer the creator's own terminal
	// message reads, so the pre-check and the extract path can no longer describe different worlds.
	int32 PendingFromRun = 0;
	for (const TSharedPtr<FBulkExtractorTextureState>& State : TextureStates)
	{
		if (State.IsValid() && !StatusExcludedFromExtract(State->Status))
		{
			++PendingFromRun;
		}
	}
	const Paper2DPlus::PaperZDSequenceAuthoring::FSequenceWorkSummary OpeningSummary =
		Paper2DPlus::PaperZDSequenceAuthoring::SummarizeSequenceWork(
			*Profile, /*bDiscoverExistingSequences=*/true, /*FlipbookScope=*/nullptr);
	const FText OpeningCountsText = FText::Format(
		LOCTEXT(
			"BulkPaperZDCounts",
			"This profile currently has {0} flipbook(s), {1} of them without a sequence. This extraction will add {2} more \x2014 create sequences for those with the checkbox below, after Extract All."),
		FText::AsNumber(OpeningSummary.CandidateFlipbooks),
		FText::AsNumber(OpeningSummary.FlipbooksMissingSequences),
		FText::AsNumber(PendingFromRun));

	TSharedRef<SWindow> PaperZDWindow = SNew(SWindow)
		.Title(LOCTEXT(
			"BulkPaperZDTitle",
			"PaperZD Source and Sequences"))
		.ClientSize(FVector2D(720.0f, 560.0f))
		.SupportsMinimize(false)
		.SupportsMaximize(false);
	const TWeakPtr<SWindow> WeakPaperZDWindow = PaperZDWindow;

	PaperZDWindow->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12, 10, 12, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT(
				"BulkPaperZDIntro",
				"Configure the linked profile's optional PaperZD source and sequence matches."))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12, 0, 12, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT(
				"BulkPaperZDScopedHint",
				"After Extract All succeeds, sequence creation is limited to flipbooks produced by that run; failed or skipped textures are excluded."))
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		// Counts, resolved once when the window opens. Without these, pressing "Create" on a brand-new
		// profile reported that all flipbooks already had sequences — the profile simply had none
		// yet, and the panel's own "scoped to this run" promise made that reading look like a
		// contradiction. Deliberately NOT a paint-time lambda: the summary walks and resolves every
		// flipbook reference on the profile.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12, 0, 12, 8)
		[
			SNew(STextBlock)
			.Text(OpeningCountsText)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12, 0, 12, 8)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this]()
			{
				return bCreatePaperZDSequencesAfterExtract
					? ECheckBoxState::Checked
					: ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, Profile](ECheckBoxState State)
			{
				bCreatePaperZDSequencesAfterExtract =
					State == ECheckBoxState::Checked;
				if (bCreatePaperZDSequencesAfterExtract)
				{
					PaperZDConfiguredProfile.Reset(Profile);
				}
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT(
					"BulkPaperZDAutoCreate",
					"Create and link missing sequences after this extraction"))
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8, 0)
		[
			SNew(SBorder)
			.BorderImage(
				FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SProfileDetailsPanel)
				.Model(ProfileModel)
				.PaneMode(EProfileDetailsPaneMode::PaperZDSequences)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(12, 8)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text(LOCTEXT("BulkPaperZDDone", "Done"))
			.OnClicked_Lambda([WeakPaperZDWindow]()
			{
				if (const TSharedPtr<SWindow> Window =
					WeakPaperZDWindow.Pin())
				{
					Window->RequestDestroyWindow();
				}
				return FReply::Handled();
			})
		]);

	FSlateApplication::Get().AddModalWindow(PaperZDWindow, AsShared());
	return FReply::Handled();
}

FReply SBulkSpriteExtractorWindow::OnCommitClicked()
{
	const FText ExtractionBlocker = GetExtractionBlockerText();
	if (!ExtractionBlocker.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("BulkExtractPaddingRequired", "Every current grid must be confirmed and all frame cells must share the same width and height.\n\n{0}"),
			ExtractionBlocker));
		return FReply::Handled();
	}

	UPaper2DPlusCharacterProfileAsset* Profile = TargetProfile.LoadSynchronous();

	if (!bLinkToProfile)
	{
		Profile = nullptr;
	}

	TOptional<TArray<UPaperFlipbook*>> CommitResult = CommitBulkExtract();
	if (!CommitResult.IsSet())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("CommitFailed", "Commit failed. See Output Log for details."));
		return FReply::Handled();
	}
	const TArray<UPaperFlipbook*>& CommittedFlipbooks =
		CommitResult.GetValue();

	if (Profile)
	{
		Profile->LastAlignmentCheckTimestamp = FDateTime::UtcNow();
		Profile->AlignmentStatus = EAlignmentStatus::Completed;
		Profile->MarkPackageDirty();
	}

	if (Profile
		&& bCreatePaperZDSequencesAfterExtract
		&& bPaperZDAuthoringAvailable
		&& PaperZDConfiguredProfile.Get() == Profile
		&& CommittedFlipbooks.Num() > 0)
	{
		Paper2DPlus::PaperZDSequenceAuthoring::
			CreateAndLinkMissingSequencesForFlipbooks(
				*Profile,
				AsShared(),
				CommittedFlipbooks);
	}

	// Unit 7: offer to delete source textures after successful trim extraction.
	// Only in trim mode — full-cell sprites reference the original texture directly.
	if (bTrimSprites && TextureStates.Num() > 0)
	{
		TArray<UObject*> TexturesToDelete;
		TArray<FString> TextureNames;
		for (const TSharedPtr<FBulkExtractorTextureState>& S : TextureStates)
		{
			if (!S.IsValid()) continue;
			// De-bake sources were never extracted — deliberately conservative: superseded baked
			// sheets are the user's to delete manually.
			if (StatusExcludedFromExtract(S->Status)) continue;
			UTexture2D* Tex = S->Texture.LoadSynchronous();
			if (!Tex) continue;
			TexturesToDelete.Add(Tex);
			TextureNames.Add(Tex->GetName());
		}

		if (TexturesToDelete.Num() > 0)
		{
			DestructiveActions::FDestructiveActionPrompt Prompt;
			Prompt.Title = LOCTEXT("DeleteSourceTexturesTitle", "Delete Source Textures");
			Prompt.Body = FText::Format(
				LOCTEXT("DeleteSourceTexturesPrompt", "Delete {0} source texture asset(s)?"),
				FText::AsNumber(TexturesToDelete.Num()));
			Prompt.Consequence = LOCTEXT("DeleteSourceTexturesConsequence", "Trimmed sprites use packed textures and no longer reference these originals.");
			Prompt.AffectedItems = MoveTemp(TextureNames);
			Prompt.bCanUndo = false;
			if (DestructiveActions::Confirm(Prompt))
			{
				const int32 Deleted = ObjectTools::ForceDeleteObjects(TexturesToDelete, /*bShowConfirmation=*/false);
				if (Deleted < TexturesToDelete.Num())
				{
					UE_LOG(LogTemp, Warning, TEXT("BulkExtractor: Deleted %d of %d source textures. Some may still be referenced."),
						Deleted, TexturesToDelete.Num());
				}
			}
		}
	}

	TSharedPtr<SWindow> Host = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (Host.IsValid()) Host->RequestDestroyWindow();

	return FReply::Handled();
}

TOptional<TArray<UPaperFlipbook*>>
SBulkSpriteExtractorWindow::CommitBulkExtract()
{
	TArray<UPaperFlipbook*> CommittedFlipbooks;
	UPaper2DPlusCharacterProfileAsset* Profile = bLinkToProfile ? TargetProfile.LoadSynchronous() : nullptr;

	FScopedTransaction Transaction(LOCTEXT("BulkExtractTxn", "Bulk Extract Sprites"));
	if (Profile) Profile->Modify();

	FString BaseOutputPath;
	if (!OutputPathOverride.IsEmpty())
	{
		BaseOutputPath = OutputPathOverride;
	}
	else if (Profile)
	{
		BaseOutputPath = FPackageName::GetLongPackagePath(Profile->GetPackage()->GetName()) / TEXT("Sprites");
	}
	else if (TextureStates.Num() > 0 && TextureStates[0].IsValid())
	{
		UTexture2D* FirstTex = TextureStates[0]->Texture.LoadSynchronous();
		if (FirstTex) BaseOutputPath = FPackageName::GetLongPackagePath(FirstTex->GetPackage()->GetName());
	}
	if (BaseOutputPath.IsEmpty())
	{
		return TOptional<TArray<UPaperFlipbook*>>();
	}

	const FString Prefix = NamePrefix;
	const FIntPoint MaxCell = FSpriteExtractionUtils::ComputeGroupMaxCellSize(BuildPerTextureCellSizeMap());
	const int32 PreexistingProfileEntryCount = Profile ? Profile->Flipbooks.Num() : 0;

	int32 TotalSpritesCreated = 0;
	int32 TotalFlipbooksCreated = 0;
	TMap<int32, FString> FlipbookIndexToFolderPath;

	// F12: dedup against the resolved flipbook package path (OutputPath/FlipbookName), not the
	// bare base name. Two textures resolving to the SAME folder + name would otherwise produce
	// identical asset/package paths and silently clobber each other (and duplicate-named profile
	// entries); a numeric suffix on the base keeps FlipbookName / TrimmedTexName / SpriteName /
	// FrameName / profile-entry name all in sync. Same-named textures routed to DIFFERENT folders
	// (e.g. via the folder organizer) resolve to distinct paths and are deliberately left untouched.
	TSet<FString> SeenPackagePaths;
	// F14: textures whose grid collapses to a zero-size cell are skipped (not written as garbage);
	// collect their names to report to the user instead of failing silently.
	TArray<FString> SkippedDegenerateTextures;

	// --- Main loop: create textures, sprites, flipbooks, profile entries ---
	for (const TSharedPtr<FBulkExtractorTextureState>& State : TextureStates)
	{
		if (!State.IsValid()) continue;
		// Accepted de-bake sources stay listed (violet chip) but are never extracted — their
		// recovered base/overlay entries carry the animation instead.
		if (StatusExcludedFromExtract(State->Status)) continue;
		UTexture2D* Tex = State->Texture.LoadSynchronous();
		if (!Tex) continue;

		FString TexBase = State->DisplayName.IsEmpty() ? Tex->GetName() : State->DisplayName;

		// Resolves the output package folder for a candidate base name.
		//
		// A texture filed by the folder organizer already carries its own leaf in SubfolderPath
		// (FBulkFolderNode::GetPath includes the texture node's own name), so the organized and
		// unorganized shapes agree: EVERY texture lands in its own folder, named after it. The old
		// "Create subfolders" flag chose between that shape and a flat dump, but the organizer's
		// Apply force-cleared it two lines after the checkbox that set it, so post-Apply it could
		// not be reached at all. It is gone; the per-texture folder is now the single behaviour.
		auto BuildOutputPath = [&](const FString& InTexBase) -> FString
		{
			if (!State->SubfolderPath.IsEmpty())
			{
				return BaseOutputPath / State->SubfolderPath;
			}
			FString FolderBase = InTexBase;
			if (!FolderRemoveStr.IsEmpty()) FolderBase = FolderBase.Replace(*FolderRemoveStr, TEXT(""));
			if (!FolderFindStr.IsEmpty()) FolderBase = FolderBase.Replace(*FolderFindStr, *FolderReplaceStr);
			FString FolderName;
			if (!FolderCustomPrefix.IsEmpty()) FolderName += FolderCustomPrefix;
			if (bFolderIncludePrefix && !Prefix.IsEmpty()) FolderName += Prefix + TEXT("_");
			FolderName += FolderBase;
			if (bFolderIncludeBatchSuffix && !LastBatchSuffix.IsEmpty()) FolderName += LastBatchSuffix;
			if (!FolderCustomSuffix.IsEmpty()) FolderName += FolderCustomSuffix;
			// A naming rule that erases the whole base (e.g. Remove == the texture name) would put
			// this texture straight into BaseOutputPath and collide with its siblings.
			return FolderName.IsEmpty() ? BaseOutputPath : (BaseOutputPath / FolderName);
		};
		auto BuildFlipbookName = [&Prefix](const FString& InTexBase) -> FString
		{
			return Prefix.IsEmpty() ? InTexBase : FString::Printf(TEXT("%s_%s"), *Prefix, *InTexBase);
		};

		// F12: bump the base name only while the RESOLVED package path collides with one already
		// claimed this batch — so same-name/same-folder textures get _2/_3 (preventing the silent
		// clobber) while same-name/different-folder textures keep their name.
		{
			const FString RawTexBase = TexBase;
			int32 DedupSuffix = 2;
			while (SeenPackagePaths.Contains(BuildOutputPath(TexBase) / BuildFlipbookName(TexBase)))
			{
				TexBase = FString::Printf(TEXT("%s_%d"), *RawTexBase, DedupSuffix++);
			}
			SeenPackagePaths.Add(BuildOutputPath(TexBase) / BuildFlipbookName(TexBase));
		}

		const FString FlipbookName = BuildFlipbookName(TexBase);
		const FString OutputPath = BuildOutputPath(TexBase);

		const FIntPoint Grid = State->GetEffectiveGrid();
		const FIntPoint TexDims = FSpriteExtractionUtils::GetDetectionDimensions(Tex);
		const int32 NumSprites = Grid.X * Grid.Y;
		const int32 CellW = (Grid.X > 0) ? (TexDims.X / Grid.X) : TexDims.X;
		const int32 CellH = (Grid.Y > 0) ? (TexDims.Y / Grid.Y) : TexDims.Y;

		// F14: a grid larger than the texture truncates the cell size to 0. Skip rather than
		// emit zero-dimension sprites / a 0x0 packed texture into the content folder.
		if (CellW <= 0 || CellH <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("BulkExtractor: skipping '%s' \x2014 grid %dx%d exceeds texture %dx%d (cell size %dx%d is degenerate)."),
				*TexBase, Grid.X, Grid.Y, TexDims.X, TexDims.Y, CellW, CellH);
			SkippedDegenerateTextures.Add(TexBase);
			continue;
		}

		// --- Build cell bounds for grid ---
		TArray<FIntRect> CellBoundsArray;
		CellBoundsArray.Reserve(NumSprites);
		for (int32 i = 0; i < NumSprites; i++)
		{
			const int32 Col = i % Grid.X;
			const int32 Row = i / Grid.X;
			CellBoundsArray.Add(FIntRect(Col * CellW, Row * CellH, (Col + 1) * CellW, (Row + 1) * CellH));
		}

		TArray<UPaperSprite*> CreatedSprites;
		CreatedSprites.Reserve(NumSprites);
		UTexture2D* SpriteSourceTex = Tex; // Updated to TrimmedTex in trim path

		TArray<FIntRect> TightRegions;
		TArray<FIntRect> UniformRegions;
		int32 TrimMaxExtLeft = 0, TrimMaxExtRight = 0, TrimMaxExtTop = 0, TrimMaxExtBottom = 0;
		FIntPoint TrimUniformSize = FIntPoint::ZeroValue;

		if (bTrimSprites)
		{
			// Shared midpoint/max-extent planner: blank frames receive the final uniform region but do
			// not contribute a full-cell fallback that disables trimming for the whole animation.
			const int32 MidX = CellW / 2;
			const int32 MidY = CellH / 2;

			TArray<FColor> Pixels;
			int32 PxW = 0, PxH = 0;
			if (!FSpriteExtractionUtils::LoadTextureData(Tex, Pixels, PxW, PxH)) continue;

			TArray<FSpriteMaxExtentFrame> ExtentFrames;
			ExtentFrames.Reserve(NumSprites);
			TightRegions.Reserve(NumSprites);
			for (int32 i = 0; i < NumSprites; i++)
			{
				const FIntRect Tight = FSpriteExtractionUtils::FindTightContentBounds(
					Pixels, PxW, PxH, CellBoundsArray[i], State->ConfirmedDetectionParams.AlphaThreshold);
				TightRegions.Add(Tight);

				FSpriteMaxExtentFrame& ExtentFrame = ExtentFrames.AddDefaulted_GetRef();
				ExtentFrame.ContainerBounds = CellBoundsArray[i];
				ExtentFrame.TightBounds = Tight;
				ExtentFrame.Anchor = FIntPoint(
					CellBoundsArray[i].Min.X + MidX,
					CellBoundsArray[i].Min.Y + MidY);
			}

			const FSpriteMaxExtentResult ExtentResult =
				FSpriteExtractionUtils::ComputeMaxExtentTargets(ExtentFrames);
			UniformRegions.Reserve(NumSprites);
			if (ExtentResult.IsValid())
			{
				if (ExtentResult.FramesOutsideContainers > 0)
				{
					UE_LOG(LogTemp, Error,
						TEXT("BulkExtract Trim[%s]: uniform max extents exceed %d source cell(s); refusing a bleed-prone extraction."),
						*TexBase, ExtentResult.FramesOutsideContainers);
					continue;
				}
				TrimMaxExtLeft = ExtentResult.MaxLeft;
				TrimMaxExtRight = ExtentResult.MaxRight;
				TrimMaxExtTop = ExtentResult.MaxTop;
				TrimMaxExtBottom = ExtentResult.MaxBottom;
				TrimUniformSize = ExtentResult.GetUniformSize();
				for (const FSpriteMaxExtentFrame& ExtentFrame : ExtentFrames)
				{
					UniformRegions.Add(ExtentFrame.TargetBounds);
				}
			}
			else
			{
				// An entirely blank animation has no content maximum. Keep uniform source cells for that
				// animation only; it no longer disables trimming on mixed blank/non-empty animations.
				// The extents must still name the cell midpoint so the baked pivot below lands there.
				UniformRegions = CellBoundsArray;
				TrimUniformSize = FIntPoint(CellW, CellH);
				TrimMaxExtLeft = MidX;
				TrimMaxExtRight = CellW - MidX;
				TrimMaxExtTop = MidY;
				TrimMaxExtBottom = CellH - MidY;
			}
			const int32 UniformW = TrimUniformSize.X;
			const int32 UniformH = TrimUniformSize.Y;

			const FString TrimmedTexName = Prefix.IsEmpty()
				? FString::Printf(TEXT("%s_Tex"), *TexBase)
				: FString::Printf(TEXT("%s_%s_Tex"), *Prefix, *TexBase);
			UTexture2D* TrimmedTex = FSpriteExtractionUtils::CreatePackedTexture(
				Tex, UniformRegions, TrimmedTexName, OutputPath);
			if (!TrimmedTex) continue;
			SpriteSourceTex = TrimmedTex;

			FSpriteExtractionUtils::ApplyPaper2DSettings(TrimmedTex);

			UE_LOG(LogTemp, Log, TEXT("BulkExtract Trim[%s]: %d sprites → %dx%d (midline extents L=%d R=%d T=%d B=%d), source cells %dx%d, pivot at (%d,%d), blank=%d"),
				*TexBase, NumSprites, UniformW, UniformH,
				TrimMaxExtLeft, TrimMaxExtRight, TrimMaxExtTop, TrimMaxExtBottom,
				CellW, CellH, TrimMaxExtLeft, TrimMaxExtTop, ExtentResult.EmptyFrames);

			for (int32 i = 0; i < NumSprites; i++)
			{
				const FIntRect SpriteBounds(i * UniformW, 0, (i + 1) * UniformW, UniformH);
				const FString SpriteName = Prefix.IsEmpty()
					? FString::Printf(TEXT("%s_%02d"), *TexBase, i)
					: FString::Printf(TEXT("%s_%s_%02d"), *Prefix, *TexBase, i);
				UPaperSprite* NewSprite = FSpriteExtractionUtils::CreateSpriteFromBounds(
					TrimmedTex, SpriteBounds, SpriteName, OutputPath);
				if (NewSprite)
				{
					// Set pivot at the grid midline within the packed sprite. This is
					// (MaxExtLeft, MaxExtTop) — the point that corresponds to the padded cell
					// center. All textures extracted with the same cell size share this
					// reference, so trimmed flipbooks act as though they still occupy the full
					// cell in ANY renderer — no Profile TrimOffset application required.
					const FVector2D PivotInPacked(
						SpriteBounds.Min.X + TrimMaxExtLeft,
						SpriteBounds.Min.Y + TrimMaxExtTop);
					NewSprite->SetPivotMode(ESpritePivotMode::Custom, PivotInPacked, true);
					CreatedSprites.Add(NewSprite);
				}
			}
		}
		else
		{
			// --- Full-cell path: sprites reference the (possibly padded) source texture directly ---
			for (int32 i = 0; i < NumSprites; i++)
			{
				const FString SpriteName = Prefix.IsEmpty()
					? FString::Printf(TEXT("%s_%02d"), *TexBase, i)
					: FString::Printf(TEXT("%s_%s_%02d"), *Prefix, *TexBase, i);
				UPaperSprite* NewSprite = FSpriteExtractionUtils::CreateSpriteFromBounds(
					Tex, CellBoundsArray[i], SpriteName, OutputPath);
				if (NewSprite) CreatedSprites.Add(NewSprite);
			}
		}

		if (CreatedSprites.Num() != NumSprites)
		{
			UE_LOG(LogTemp, Error,
				TEXT("BulkExtractor: created %d of %d sprites for '%s'; refusing a partial flipbook whose frame metadata would be mis-indexed."),
				CreatedSprites.Num(), NumSprites, *TexBase);
			continue;
		}
		TotalSpritesCreated += CreatedSprites.Num();

		// --- Build flipbook ---
		const FString FBPkgPath = OutputPath / FlipbookName;
		UPackage* FBPackage = CreatePackage(*FBPkgPath);
		if (!FBPackage) continue;
		// F12: guard the NewObject against an existing same-name object — reuse a same-class
		// flipbook in place, evict any different-class/redirector occupant to the transient
		// package. Without this an intentional re-extract orphans the prior asset (or a
		// different-class collision faults inside StaticAllocateObject). Mirrors the canonical
		// guard in CreateSpriteFromBounds / CreatePackedTexture / SpriteExtractorWindow.cpp.
		UPaperFlipbook* Flipbook = FindObject<UPaperFlipbook>(FBPackage, *FlipbookName);
		if (!Flipbook)
		{
			if (UObject* Existing = StaticFindObject(UObject::StaticClass(), FBPackage, *FlipbookName))
			{
				Existing->Rename(nullptr, GetTransientPackage(), PAPER2DPLUS_RENAME_TO_TRANSIENT_FLAGS);
			}
			Flipbook = NewObject<UPaperFlipbook>(FBPackage, *FlipbookName, RF_Public | RF_Standalone);
		}
		if (!Flipbook) continue;

		{
			FScopedFlipbookMutator Mutator(Flipbook);
			Mutator.FramesPerSecond = 10.0f;
			Mutator.KeyFrames.Empty();
			for (UPaperSprite* S : CreatedSprites)
			{
				FPaperFlipbookKeyFrame KF;
				KF.Sprite = S;
				KF.FrameRun = 1;
				Mutator.KeyFrames.Add(KF);
			}
		}
		FBPackage->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Flipbook);

		// --- Build profile entry ---
		FFlipbookProfileEntry NewEntry;
		NewEntry.Identity.FlipbookName = (bIncludePrefixInProfileName && !Prefix.IsEmpty())
			? FString::Printf(TEXT("%s_%s"), *Prefix, *TexBase) : TexBase;
		NewEntry.Identity.Flipbook = Flipbook;
		NewEntry.SourceTexture = SpriteSourceTex;
		NewEntry.SpritesOutputPath = OutputPath;

		for (int32 i = 0; i < CreatedSprites.Num(); i++)
		{
			FFrameHitboxData FrameData;
			FrameData.FrameName = FString::Printf(TEXT("%s_%02d"), *FlipbookName, i);
			NewEntry.CombatData.Frames.Add(FrameData);

			const int32 Col = i % Grid.X;
			const int32 Row = i / Grid.X;

			FSpriteExtractionInfo Info;
			Info.AlphaThreshold = State->ConfirmedDetectionParams.AlphaThreshold;
			Info.ExtractionTime = FDateTime::Now();

			if (bTrimSprites
				&& TightRegions.IsValidIndex(i)
				&& CreatedSprites.IsValidIndex(i))
			{
				// Alignment is baked into the sprite's custom pivot (the shared cell-midpoint
				// reference), so TrimOffset stays zero — nothing needs to re-apply it at render
				// time. SourceOffset records the tight content origin in the SOURCE sheet.
				Info.SourceOffset = TightRegions[i].Min;
				Info.CachedPivotLocal =
					CreatedSprites[i]->GetPivotPosition() - CreatedSprites[i]->GetSourceUV();
			}
			else
			{
				Info.SourceOffset = FIntPoint(Col * CellW, Row * CellH);
			}

			NewEntry.CombatData.FrameExtractionInfo.Add(Info);
		}

		NewEntry.AlignmentData.GridDims = Grid;
		NewEntry.AlignmentData.OriginalCellSize = FIntPoint(CellW, CellH);
		NewEntry.AlignmentData.UniformCellSize = MaxCell;
		// The pad is midpoint-to-midpoint, so there is no ground plane to record. Writing the old
		// median here left every entry carrying a number that described nothing about the sheet.
		NewEntry.AlignmentData.GroundPlaneOffset = 0;
		NewEntry.AlignmentData.NumSprites = NumSprites;
		NewEntry.AlignmentData.AlphaThreshold = State->ConfirmedDetectionParams.AlphaThreshold;
		NewEntry.AlignmentData.MinSpriteSize = State->ConfirmedDetectionParams.MinSpriteSize;
		NewEntry.AlignmentData.IslandMergeDistance = State->ConfirmedDetectionParams.IslandMergeDistance;

		if (Profile)
		{
			int32 NewIdx = Profile->Flipbooks.Add(NewEntry);
			if (!State->SubfolderPath.IsEmpty())
			{
				FlipbookIndexToFolderPath.Add(NewIdx, State->SubfolderPath);
			}
		}
		CommittedFlipbooks.Add(Flipbook);
		TotalFlipbooksCreated++;
	}

	// Auto-create flipbook groups from folder organizer structure.
	// SubfolderPath includes the texture name as the last segment (from FBulkFolderNode::GetPath()).
	// Strip it so groups are created only for folders, not individual textures.
	// This ensures all textures in the same folder share a single leaf group.
	if (bAutoCreateGroups && Profile && FlipbookIndexToFolderPath.Num() > 0)
	{
		TSet<FString> CreatedGroups;
		for (const auto& Pair : FlipbookIndexToFolderPath)
		{
			// Parse folder path segments (skip "Root" prefix if present)
			TArray<FString> Segments;
			Pair.Value.ParseIntoArray(Segments, TEXT("/"));
			if (Segments.Num() > 0 && Segments[0] == TEXT("Root")) Segments.RemoveAt(0);

			// Last segment is the texture node name -- remove it to keep only folder segments
			if (Segments.Num() > 1)
			{
				Segments.Pop();
			}
			else
			{
				// Single segment means the texture is directly under root with no real folder --
				// skip group creation, flipbook stays Ungrouped
				continue;
			}

			FName ParentGroup = NAME_None;
			for (const FString& Seg : Segments)
			{
				FName GroupName(*Seg);
				if (!CreatedGroups.Contains(Seg))
				{
					if (!Profile->HasFlipbookGroup(GroupName))
					{
						Profile->AddFlipbookGroup(GroupName, ParentGroup);
					}
					CreatedGroups.Add(Seg);
				}
				ParentGroup = GroupName;
			}

			FName LeafGroup(*Segments.Last());
			Profile->MoveFlipbookToFlipbookGroup(Pair.Key, LeafGroup);
		}
	}

	if (TotalFlipbooksCreated == 0)
	{
		// A failed commit never accepts an in-place pad. Keep the original texture authoritative and
		// retain failed manifests if recovery itself cannot complete.
		bLastPadRollbackComplete = RestoreCurrentPadSnapshots();
		return TOptional<TArray<UPaperFlipbook*>>();
	}

	if (bTrimSprites)
	{
		// Trim path: restore original textures from pad snapshots (padded state was transient).
		if (!RestoreCurrentPadSnapshots())
		{
			UE_LOG(LogTemp, Error,
				TEXT("BulkExtractor: trimmed assets were created, but source-texture restore was incomplete. Source deletion and success completion are blocked."));
			return TOptional<TArray<UPaperFlipbook*>>();
		}
	}
	else
	{
		// Every Profile that references an in-place padded sheet still uses the sheet's old coordinates,
		// including the sister Profiles surfaced by the confirmation dialog. Migrate them together so a
		// shared sprite asset moves once while each Profile's frame metadata/hitboxes follow it. Limit only
		// the initiating Profile to its pre-commit row count: newly-created full-cell rows already use the
		// padded coordinate space and must not receive a paste delta twice.
		if (PerTexturePasteOffsets.Num() > 0)
		{
			TArray<UPaper2DPlusCharacterProfileAsset*> ExistingProfiles;
			TSet<UPaper2DPlusCharacterProfileAsset*> SeenProfiles;
			if (Profile)
			{
				SeenProfiles.Add(Profile);
				ExistingProfiles.Add(Profile);
			}
			for (const TPair<UTexture2D*, TArray<FIntPoint>>& Pair : PerTexturePasteOffsets)
			{
				for (UPaper2DPlusCharacterProfileAsset* ReferencingProfile :
					FSpriteExtractionUtils::FindProfilesReferencingTexture(Pair.Key))
				{
					if (ReferencingProfile && !SeenProfiles.Contains(ReferencingProfile))
					{
						SeenProfiles.Add(ReferencingProfile);
						ExistingProfiles.Add(ReferencingProfile);
					}
				}
			}

			TMap<UPaper2DPlusCharacterProfileAsset*, int32> EntryLimits;
			if (Profile)
			{
				EntryLimits.Add(Profile, PreexistingProfileEntryCount);
			}
			FSpriteExtractionUtils::ApplyCrossSheetAlignment(
				ExistingProfiles,
				MaxCell,
				PerTextureSourceCellSizes,
				PerTexturePasteOffsets,
				EntryLimits);
		}
		// Full-cell sprites keep referencing the padded sheets. The pad is now accepted; remove the
		// recovery sidecars so window destruction cannot restore underneath the committed sprites.
		DiscardCurrentPadSnapshots();
	}

	const FText NotifMsg = bTrimSprites
		? FText::Format(LOCTEXT("BulkCommitTrimNotif", "Created {0} trimmed sprites + {1} flipbook(s) with alignment offsets."),
			TotalSpritesCreated, TotalFlipbooksCreated)
		: FText::Format(LOCTEXT("BulkCommitFullNotif", "Created {0} sprites + {1} flipbook(s) with cross-sheet alignment."),
			TotalSpritesCreated, TotalFlipbooksCreated);
	FNotificationInfo Notif(NotifMsg);
	Notif.bFireAndForget = true;
	Notif.ExpireDuration = 6.0f;
	FSlateNotificationManager::Get().AddNotification(Notif);

	// F14: surface any textures skipped for a degenerate (oversized) grid so the user knows
	// they were not extracted, rather than silently producing fewer flipbooks than expected.
	if (SkippedDegenerateTextures.Num() > 0)
	{
		FNotificationInfo SkipNotif(FText::Format(
			LOCTEXT("BulkCommitSkippedNotif", "Skipped {0} texture(s) whose grid exceeds the texture size (cells would be 0px): {1}. Reduce columns/rows and re-extract."),
			SkippedDegenerateTextures.Num(), FText::FromString(FString::Join(SkippedDegenerateTextures, TEXT(", ")))));
		SkipNotif.bFireAndForget = true;
		SkipNotif.ExpireDuration = 10.0f;
		FSlateNotificationManager::Get().AddNotification(SkipNotif);
	}

	return CommittedFlipbooks;
}

ESpriteCanvasGridState SBulkSpriteExtractorWindow::GetGridState() const
{
	if (!SelectedTexture.IsValid()) return ESpriteCanvasGridState::Inferred;
	switch (SelectedTexture->Status)
	{
	case EBulkExtractorTextureStatus::Overridden: return ESpriteCanvasGridState::Overridden;
	case EBulkExtractorTextureStatus::Confirmed:
	case EBulkExtractorTextureStatus::SkippedNoPad:
	case EBulkExtractorTextureStatus::Padded:
		return ESpriteCanvasGridState::Confirmed;
	default:
		return ESpriteCanvasGridState::Inferred;
	}
}

FText SBulkSpriteExtractorWindow::GetStatusLabel(EBulkExtractorTextureStatus Status)
{
	switch (Status)
	{
	case EBulkExtractorTextureStatus::Pending:      return LOCTEXT("StatusPending", "Pending");
	case EBulkExtractorTextureStatus::Inferred:     return LOCTEXT("StatusInferred", "Inferred");
	case EBulkExtractorTextureStatus::Overridden:   return LOCTEXT("StatusOverridden", "Overridden");
	case EBulkExtractorTextureStatus::Confirmed:    return LOCTEXT("StatusConfirmed", "Confirmed");
	case EBulkExtractorTextureStatus::SkippedNoPad: return LOCTEXT("StatusSkipped", "Skipped");
	case EBulkExtractorTextureStatus::Padded:       return LOCTEXT("StatusPadded", "Padded");
	case EBulkExtractorTextureStatus::DebakeSource: return LOCTEXT("StatusDebakeSource", "Debaked");
	case EBulkExtractorTextureStatus::Error:        return LOCTEXT("StatusError", "Error");
	}
	return FText::GetEmpty();
}

FLinearColor SBulkSpriteExtractorWindow::GetStatusColor(EBulkExtractorTextureStatus Status)
{
	switch (Status)
	{
	case EBulkExtractorTextureStatus::Pending:      return FLinearColor(0.75f, 0.75f, 0.75f);
	case EBulkExtractorTextureStatus::Inferred:     return FLinearColor(1.0f, 0.95f, 0.5f);
	case EBulkExtractorTextureStatus::Overridden:   return FLinearColor(0.45f, 0.82f, 1.0f);
	case EBulkExtractorTextureStatus::Confirmed:    return FLinearColor(0.55f, 1.0f, 0.55f);
	// Distinct from Confirmed on purpose: the two used the SAME green, so "already at the group max
	// cell size" and "grid accepted" were indistinguishable chips differing only by their label.
	case EBulkExtractorTextureStatus::SkippedNoPad: return FLinearColor(0.45f, 0.78f, 0.5f);
	case EBulkExtractorTextureStatus::Padded:       return FLinearColor(0.4f, 0.9f, 0.9f);
	case EBulkExtractorTextureStatus::DebakeSource: return FLinearColor(0.75f, 0.55f, 1.0f);
	case EBulkExtractorTextureStatus::Error:        return FLinearColor(1.0f, 0.5f, 0.5f);
	}
	return FLinearColor::Gray;
}

FLinearColor SBulkSpriteExtractorWindow::GetStatusTextColor(EBulkExtractorTextureStatus Status)
{
	// The chip is a flat fill of GetStatusColor, so the label has to follow the fill's luminance
	// instead of being hard-coded black — black on the darker statuses was effectively unreadable.
	const FLinearColor Fill = GetStatusColor(Status);
	const float Luminance = 0.299f * Fill.R + 0.587f * Fill.G + 0.114f * Fill.B;
	return Luminance > 0.55f ? FLinearColor(0.03f, 0.03f, 0.03f) : FLinearColor(0.97f, 0.97f, 0.97f);
}

FReply SBulkSpriteExtractorWindow::OnBatchRenameClicked()
{
	TSharedRef<SWindow> RenameWindow = SNew(SWindow)
		.Title(LOCTEXT("BatchRenameTitle", "Batch Rename Textures"))
		.ClientSize(FVector2D(700, 500))
		.SupportsMinimize(false).SupportsMaximize(false);

	// Editable copies of display names for live preview
	TSharedRef<TArray<FString>> PreviewNames = MakeShared<TArray<FString>>();
	for (const TSharedPtr<FBulkExtractorTextureState>& S : TextureStates)
	{
		PreviewNames->Add(S.IsValid() ? S->DisplayName : TEXT(""));
	}
	auto ResetPreview = [this, PreviewNames]()
	{
		for (int32 i = 0; i < TextureStates.Num() && i < PreviewNames->Num(); i++)
		{
			(*PreviewNames)[i] = TextureStates[i]->DisplayName;
		}
	};

	// Shared rename state — initialized from persistent members so values survive dialog reopens
	TSharedRef<FString> FindStr = MakeShared<FString>(PersistentBatchFindStr);
	TSharedRef<FString> ReplaceStr = MakeShared<FString>(PersistentBatchReplaceStr);
	TSharedRef<FString> RemoveStr = MakeShared<FString>(PersistentBatchRemoveStr);
	TSharedRef<FString> SuffixStr = MakeShared<FString>(PersistentBatchSuffixStr);

	// Preview-row filter. Deliberately NOT persisted: it is a per-session view aid, and Apply always
	// writes every row (see the hint text) so a stale filter must never look like a scope.
	TSharedRef<FString> RenameSearch = MakeShared<FString>();

	auto ApplyPreview = [this, PreviewNames, FindStr, ReplaceStr, RemoveStr, SuffixStr]()
	{
		for (int32 i = 0; i < TextureStates.Num() && i < PreviewNames->Num(); i++)
		{
			FString Name = TextureStates[i]->DisplayName;
			if (!RemoveStr->IsEmpty()) Name = Name.Replace(**RemoveStr, TEXT(""));
			if (!FindStr->IsEmpty()) Name = Name.Replace(**FindStr, **ReplaceStr);
			if (!SuffixStr->IsEmpty()) Name = Name + *SuffixStr;
			FSpriteExtractionUtils::SanitizeAssetName(Name);
			(*PreviewNames)[i] = Name;
		}
	};

	// Apply persisted rename fields to preview on dialog open
	if (!PersistentBatchFindStr.IsEmpty() || !PersistentBatchRemoveStr.IsEmpty() || !PersistentBatchSuffixStr.IsEmpty())
	{
		ApplyPreview();
	}

	// Built BEFORE SetContent. It used to be a null TSharedPtr captured by value into the left
	// column's lambdas while SAssignNew filled it in the right column of the SAME full-expression,
	// so whether those captured copies were valid depended on unspecified operand evaluation order.
	TSharedRef<SVerticalBox> NameListBox = SNew(SVerticalBox);

	RenameWindow->SetContent(
		SNew(SSplitter).Orientation(Orient_Horizontal)

		// Left: rename tools
		+ SSplitter::Slot().Value(0.45f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot().Padding(8)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
				[
					SNew(STextBlock).Text(LOCTEXT("RenameTools", "RENAME TOOLS"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]

				// Asset prefix (used for flipbook/sprite/texture naming)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 4)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("AssetPrefixLabel", "Prefix"))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SEditableTextBox)
						.Text_Lambda([this]() { return FText::FromString(NamePrefix); })
						.OnTextCommitted_Lambda([this, NameListBox](const FText& NewText, ETextCommit::Type)
						{
							NamePrefix = NewText.ToString();
							FSpriteExtractionUtils::SanitizeAssetName(NamePrefix);
							NameListBox->Invalidate(EInvalidateWidgetReason::Paint);
						})
						.HintText(LOCTEXT("AssetPrefixHint", "e.g. KnightProfile"))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
				[
					SNew(SSeparator)
				]

				// Find & Replace
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
				[ SNew(STextBlock).Text(LOCTEXT("FindReplaceLabel", "Find & Replace")) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0)
					[
						SNew(SEditableTextBox)
						.Text(FText::FromString(*FindStr))
						.HintText(LOCTEXT("FindHint", "Find..."))
						.OnTextChanged_Lambda([FindStr, ApplyPreview, NameListBox](const FText& T)
						{
							*FindStr = T.ToString();
							ApplyPreview();
							NameListBox->Invalidate(EInvalidateWidgetReason::Paint);
						})
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SEditableTextBox)
						.Text(FText::FromString(*ReplaceStr))
						.HintText(LOCTEXT("ReplaceHint", "Replace..."))
						.OnTextChanged_Lambda([ReplaceStr, ApplyPreview, NameListBox](const FText& T)
						{
							*ReplaceStr = T.ToString();
							ApplyPreview();
							NameListBox->Invalidate(EInvalidateWidgetReason::Paint);
						})
					]
				]

				// Remove
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 2)
				[ SNew(STextBlock).Text(LOCTEXT("RemoveLabel", "Remove Text")) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SEditableTextBox)
					.Text(FText::FromString(*RemoveStr))
					.HintText(LOCTEXT("RemoveHint", "Text to remove..."))
					.OnTextChanged_Lambda([RemoveStr, ApplyPreview, NameListBox](const FText& T)
					{
						*RemoveStr = T.ToString();
						ApplyPreview();
						NameListBox->Invalidate(EInvalidateWidgetReason::Paint);
					})
				]

				// Add Suffix
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 2)
				[ SNew(STextBlock).Text(LOCTEXT("AddSuffixLabel", "Add Suffix")) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SEditableTextBox)
					.Text(FText::FromString(*SuffixStr))
					.HintText(LOCTEXT("SuffixHint", "Suffix..."))
					.OnTextChanged_Lambda([SuffixStr, ApplyPreview, NameListBox](const FText& T)
					{
						*SuffixStr = T.ToString();
						ApplyPreview();
						NameListBox->Invalidate(EInvalidateWidgetReason::Paint);
					})
				]

				// === Profile Name ===
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 4)
				[
					SNew(SSeparator)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 4)
				[
					SNew(STextBlock).Text(LOCTEXT("ProfileNameSection", "PROFILE NAME"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bIncludePrefixInProfileName ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this, NameListBox](ECheckBoxState S)
					{
						bIncludePrefixInProfileName = (S == ECheckBoxState::Checked);
						NameListBox->Invalidate(EInvalidateWidgetReason::Paint);
					})
					[
						SNew(STextBlock).Text(LOCTEXT("IncludePrefixProfile", "Include prefix in profile name"))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
					.Padding(6)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ProfileNamePreview", "Profile entry example:"))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
						[
							SNew(STextBlock)
							.Text_Lambda([this, PreviewNames]() -> FText
							{
								FString ExName = PreviewNames->Num() > 0 ? (*PreviewNames)[0] : TEXT("TextureName");
								if (bIncludePrefixInProfileName && !NamePrefix.IsEmpty())
								{
									return FText::FromString(NamePrefix + TEXT("_") + ExName);
								}
								return FText::FromString(ExName);
							})
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						]
					]
				]

				// Apply / Cancel buttons
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.Text(LOCTEXT("ApplyRename", "Apply"))
						.OnClicked_Lambda([this, PreviewNames, RenameWindow, FindStr, ReplaceStr, RemoveStr, SuffixStr]() -> FReply
						{
							for (int32 i = 0; i < TextureStates.Num() && i < PreviewNames->Num(); i++)
							{
								FString SanitizedName = (*PreviewNames)[i];
								FSpriteExtractionUtils::SanitizeAssetName(SanitizedName);
								TextureStates[i]->DisplayName = SanitizedName;
							}
							// Persist rename field values across dialog reopens
							PersistentBatchFindStr = *FindStr;
							PersistentBatchReplaceStr = *ReplaceStr;
							PersistentBatchRemoveStr = *RemoveStr;
							PersistentBatchSuffixStr = *SuffixStr;
							LastBatchSuffix = *SuffixStr;
							RefreshFilteredTextureList();
							RenameWindow->RequestDestroyWindow();
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.Text(LOCTEXT("CancelRename", "Cancel"))
						.OnClicked_Lambda([RenameWindow]() -> FReply
						{
							RenameWindow->RequestDestroyWindow();
							return FReply::Handled();
						})
					]
				]
			]
		]

		// Right: name preview list
		+ SSplitter::Slot().Value(0.55f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
			[
				SNew(STextBlock).Text(LOCTEXT("PreviewHeader", "NAME PREVIEW"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
			// Row filter. A view aid only — the rows keep their index parity with PreviewNames
			// because non-matching rows COLLAPSE (no rebuild, no re-index).
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 4)
			[
				SNew(SEditableTextBox)
				.HintText(LOCTEXT("RenamePreviewSearchHint", "Filter rows (Apply still renames all)..."))
				.ToolTipText(LOCTEXT("RenamePreviewSearchTip", "Matches the current name OR the previewed new name. This only hides rows \x2014 Apply always writes every texture in the batch."))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.OnTextChanged_Lambda([RenameSearch, NameListBox](const FText& NewText)
				{
					*RenameSearch = NewText.ToString();
					NameListBox->Invalidate(EInvalidateWidgetReason::Layout);
				})
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8, 0)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					NameListBox
				]
			]
		]
	);

	// Build the name preview rows
	for (int32 i = 0; i < TextureStates.Num(); i++)
	{
		NameListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 1)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6, 3))
			.Visibility_Lambda([this, PreviewNames, RenameSearch, i]() -> EVisibility
			{
				if (RenameSearch->IsEmpty())
				{
					return EVisibility::Visible;
				}
				// Match EITHER side of the arrow so a user can filter by what a row is called
				// today or by what it is about to become.
				const bool bMatchesCurrent = TextureStates.IsValidIndex(i)
					&& BulkSpriteExtractor_Internal::StateLabel(TextureStates[i]).Contains(*RenameSearch, ESearchCase::IgnoreCase);
				const bool bMatchesPreview = PreviewNames->IsValidIndex(i)
					&& (*PreviewNames)[i].Contains(*RenameSearch, ESearchCase::IgnoreCase);
				return (bMatchesCurrent || bMatchesPreview) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(BulkSpriteExtractor_Internal::StateLabel(TextureStates[i])))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.45f, 0.45f)))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("\u2192")))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.35f, 0.35f, 0.35f)))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() -> FText
					{
						return NamePrefix.IsEmpty() ? FText::GetEmpty() : FText::FromString(NamePrefix + TEXT("_"));
					})
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.7f, 0.9f, 0.7f)))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
				+ SHorizontalBox::Slot().FillWidth(0.6f).VAlign(VAlign_Center).Padding(0, 0, 0, 0)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([PreviewNames, i]() -> FText
					{
						return PreviewNames->IsValidIndex(i) ? FText::FromString((*PreviewNames)[i]) : FText::GetEmpty();
					})
					.OnTextCommitted_Lambda([PreviewNames, i](const FText& NewText, ETextCommit::Type)
					{
						if (PreviewNames->IsValidIndex(i))
						{
							FString Sanitized = NewText.ToString();
							FSpriteExtractionUtils::SanitizeAssetName(Sanitized);
							(*PreviewNames)[i] = Sanitized;
						}
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
			]
		];
	}

	FSlateApplication::Get().AddModalWindow(RenameWindow, AsShared());
	return FReply::Handled();
}

bool SBulkSpriteExtractorWindow::HasAnyFolderAssignment() const
{
	for (const TSharedPtr<FBulkExtractorTextureState>& S : TextureStates)
	{
		if (S.IsValid() && !S->SubfolderPath.IsEmpty()) return true;
	}
	return false;
}

// ---- Folder Organizer ----

struct FBulkFolderNode
{
	FString Name;
	int32 TextureIndex = INDEX_NONE;
	TArray<TSharedPtr<FBulkFolderNode>> Children;
	TWeakPtr<FBulkFolderNode> Parent;

	/** Stable identity for this organizer session. The collapse set is keyed by this, NOT by
	 *  GetPath(): a path key goes stale on every rename or move of the node OR of any ancestor, and
	 *  the collapse restore then force-expanded every folder whose key it could no longer find —
	 *  which is why folders unfolded as soon as something was dropped into them. Node identities
	 *  survive reparenting, so the set stays valid across add / drop / move / delete / create. */
	FGuid NodeId = FGuid::NewGuid();

	bool IsFolder() const { return TextureIndex == INDEX_NONE; }

	/** Path INCLUDING this node's own name and EXCLUDING the synthetic Root. For a TEXTURE node the
	 *  last segment is the texture itself (that is what makes SubfolderPath carry a per-texture
	 *  leaf folder), so folder-only consumers must strip the leaf — see the organizer's
	 *  reconstruction split and CommitBulkExtract's flipbook-group auto-create. */
	FString GetPath() const
	{
		FString Path = Name;
		TSharedPtr<FBulkFolderNode> P = Parent.Pin();
		while (P.IsValid() && P->Parent.IsValid())
		{
			Path = P->Name / Path;
			P = P->Parent.Pin();
		}
		return Path;
	}

	void Remove(TSharedPtr<FBulkFolderNode> Child)
	{
		Children.Remove(Child);
	}
};

class FFolderNodeDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FFolderNodeDragDropOp, FDragDropOperation)

	/** The whole dragged selection. Index 0 is the row the drag started on; a drag that begins on a
	 *  row outside the current selection carries only that row. */
	TArray<TSharedPtr<FBulkFolderNode>> DraggedNodes;
	bool bFromUnassigned = false;

	static TSharedRef<FFolderNodeDragDropOp> New(const TArray<TSharedPtr<FBulkFolderNode>>& InNodes, bool bUnassigned)
	{
		TSharedRef<FFolderNodeDragDropOp> Op = MakeShared<FFolderNodeDragDropOp>();
		for (const TSharedPtr<FBulkFolderNode>& Node : InNodes)
		{
			if (Node.IsValid())
			{
				Op->DraggedNodes.Add(Node);
			}
		}
		Op->bFromUnassigned = bUnassigned;
		Op->Construct();
		return Op;
	}
};

/** Pure organizer helpers. File-local names are prefixed so unity grouping cannot collide them. */
namespace BulkFolderOrganizer_Internal
{
	/**
	 * Whether CandidateName may be committed as Node's folder name under Parent.
	 * Delegates the package-path charset/length rules to the engine's own helper rather than a
	 * hand-rolled charset: AssetViewUtils::IsValidFolderName rejects everything in
	 * INVALID_LONGPACKAGE_CHARACTERS plus "/[]" — spaces, periods, slashes, colons, quotes,
	 * brackets, '&', '!', '~', '@', '#' — over-cook-length names, names IAssetTools disallows, and
	 * names the filesystem cannot save. Sibling uniqueness is organizer-specific and is checked here,
	 * because two same-named siblings would resolve to one GetPath()/SubfolderPath and one output
	 * directory. Node may be null when validating a name that is about to be created.
	 */
	bool IsFolderNameAcceptable(
		const FString& CandidateName,
		const TSharedPtr<FBulkFolderNode>& Node,
		const TSharedPtr<FBulkFolderNode>& Parent,
		FText& OutError)
	{
		const FString Trimmed = CandidateName.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			OutError = LOCTEXT("OrgNameEmpty", "Folder name cannot be empty.");
			return false;
		}

		FText Reason;
		if (!AssetViewUtils::IsValidFolderName(Trimmed, Reason))
		{
			OutError = Reason;
			return false;
		}

		if (Parent.IsValid())
		{
			for (const TSharedPtr<FBulkFolderNode>& Sibling : Parent->Children)
			{
				if (!Sibling.IsValid() || Sibling == Node || !Sibling->IsFolder())
				{
					continue;
				}
				if (Sibling->Name.Equals(Trimmed, ESearchCase::IgnoreCase))
				{
					OutError = FText::Format(
						LOCTEXT("OrgNameDuplicate", "\"{0}\" already exists in this folder."),
						FText::FromString(Trimmed));
					return false;
				}
			}
		}
		return true;
	}

	/**
	 * Derive a package-path-legal folder segment from an arbitrary imported string.
	 *
	 * Returns false when no legal name can be derived, so a caller can REFUSE the row instead of
	 * storing a path CreatePackage will reject at the very end of a long batch. The charset/length
	 * rules stay with the engine (AssetViewUtils::IsValidFolderName, as everywhere else in this
	 * organizer); the repair pass is a conservative ALLOWLIST — letters, digits, '_', '-', '+' —
	 * so nothing in INVALID_LONGPACKAGE_CHARACTERS (':' '.' '[' ']' '&' '!' '~' '@' '#' '"' '|' '*'
	 * '?' '<' '>') can survive and ".." cannot become a path-traversal segment.
	 *
	 * FSpriteExtractionUtils::SanitizeAssetName is NOT a package-path sanitizer — it replaces spaces
	 * and nothing else — so it is used here only for the friendly space→underscore step.
	 */
	bool DeriveLegalFolderSegment(const FString& InName, FString& OutName)
	{
		OutName = InName.TrimStartAndEnd();
		FSpriteExtractionUtils::SanitizeAssetName(OutName);

		FText Reason;
		if (!OutName.IsEmpty() && AssetViewUtils::IsValidFolderName(OutName, Reason))
		{
			return true;
		}

		FString Repaired;
		Repaired.Reserve(OutName.Len());
		for (const TCHAR Ch : OutName)
		{
			const bool bKeep = FChar::IsAlnum(Ch) || Ch == TEXT('_') || Ch == TEXT('-') || Ch == TEXT('+');
			Repaired.AppendChar(bKeep ? Ch : TEXT('_'));
		}
		Repaired.TrimStartAndEndInline();

		// Still rejected (empty, over-length, or a name IAssetTools disallows) — fail closed.
		if (Repaired.IsEmpty() || !AssetViewUtils::IsValidFolderName(Repaired, Reason))
		{
			return false;
		}
		OutName = Repaired;
		return true;
	}

	/** A sibling-unique variant of BaseName that already satisfies IsFolderNameAcceptable, so a
	 *  freshly created folder never starts life holding a name its own rename validator rejects. */
	FString MakeUniqueFolderName(const TSharedPtr<FBulkFolderNode>& Parent, const FString& BaseName)
	{
		FString Base = BaseName.TrimStartAndEnd();
		FSpriteExtractionUtils::SanitizeAssetName(Base);

		FText Reason;
		if (Base.IsEmpty() || !AssetViewUtils::IsValidFolderName(Base, Reason))
		{
			Base = TEXT("NewFolder");
		}

		FText Unused;
		if (IsFolderNameAcceptable(Base, nullptr, Parent, Unused))
		{
			return Base;
		}
		for (int32 Suffix = 2; Suffix < 1000; ++Suffix)
		{
			// '_' and digits are legal package-path characters, so only sibling uniqueness can fail.
			const FString Candidate = FString::Printf(TEXT("%s_%d"), *Base, Suffix);
			if (IsFolderNameAcceptable(Candidate, nullptr, Parent, Unused))
			{
				return Candidate;
			}
		}
		return Base + FGuid::NewGuid().ToString(EGuidFormats::Short);
	}

	/** Drop nodes that already have an ancestor in the set. Moving or deleting a parent covers its
	 *  children, so processing both would move a node twice (or delete an already-detached node). */
	void PruneNestedNodes(TArray<TSharedPtr<FBulkFolderNode>>& Nodes)
	{
		TSet<const FBulkFolderNode*> Selected;
		for (const TSharedPtr<FBulkFolderNode>& Node : Nodes)
		{
			if (Node.IsValid())
			{
				Selected.Add(Node.Get());
			}
		}
		Nodes.RemoveAll([&Selected](const TSharedPtr<FBulkFolderNode>& Node)
		{
			if (!Node.IsValid())
			{
				return true;
			}
			for (TSharedPtr<FBulkFolderNode> Ancestor = Node->Parent.Pin();
				Ancestor.IsValid();
				Ancestor = Ancestor->Parent.Pin())
			{
				if (Selected.Contains(Ancestor.Get()))
				{
					return true;
				}
			}
			return false;
		});
	}
}

/** Thin SBorder subclass that accepts drag-drop via an OnDrop callback. Used for drop-to-unassign (Unit 9: TASK-35). */
class SDropTargetBorder : public SBorder
{
public:
	using FOnDropCallback = TFunction<FReply(const FGeometry&, const FDragDropEvent&)>;

	SLATE_BEGIN_ARGS(SDropTargetBorder)
		: _OnDropCallback()
	{}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(const FSlateBrush*, BorderImage)
		SLATE_ARGUMENT(FMargin, Padding)
		SLATE_ARGUMENT(FOnDropCallback, OnDropCallback)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OnDropDelegate = InArgs._OnDropCallback;
		SBorder::Construct(SBorder::FArguments()
			.BorderImage(InArgs._BorderImage)
			.Padding(InArgs._Padding)
			[
				InArgs._Content.Widget
			]
		);
	}

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		if (OnDropDelegate)
		{
			return OnDropDelegate(MyGeometry, DragDropEvent);
		}
		return FReply::Unhandled();
	}

private:
	FOnDropCallback OnDropDelegate;
};

/**
 * SBorder host for the organizer's folder tree that takes keyboard focus and routes Delete /
 * Backspace to the same handler as the "- Delete" button. The obvious hook,
 * STreeView::FArguments::OnKeyDownHandler, only exists from UE 5.3 and Paper2DPlus must build
 * 5.0-5.8, so the key handler lives on the host border — the same SBorder-subclass precedent as
 * SDropTargetBorder above.
 */
class SFolderTreeHostBorder : public SBorder
{
public:
	/** Returns true when the request actually deleted something (so an inert key stays unhandled). */
	using FOnDeleteRequested = TFunction<bool()>;

	SLATE_BEGIN_ARGS(SFolderTreeHostBorder)
		: _OnDeleteRequested()
	{}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(const FSlateBrush*, BorderImage)
		SLATE_ARGUMENT(FMargin, Padding)
		SLATE_ARGUMENT(FOnDeleteRequested, OnDeleteRequested)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OnDeleteRequestedCallback = InArgs._OnDeleteRequested;
		SBorder::Construct(SBorder::FArguments()
			.BorderImage(InArgs._BorderImage)
			.Padding(InArgs._Padding)
			[
				InArgs._Content.Widget
			]
		);
	}

	virtual bool SupportsKeyboardFocus() const override { return true; }

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		// Mandatory: every tree row hosts an inline rename widget, so Delete must never be hijacked
		// out from under a live rename.
		if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
		{
			return FReply::Unhandled();
		}

		const FKey Key = InKeyEvent.GetKey();
		if ((Key == EKeys::Delete || Key == EKeys::BackSpace) && OnDeleteRequestedCallback)
		{
			return OnDeleteRequestedCallback() ? FReply::Handled() : FReply::Unhandled();
		}
		return SBorder::OnKeyDown(MyGeometry, InKeyEvent);
	}

private:
	FOnDeleteRequested OnDeleteRequestedCallback;
};

FReply SBulkSpriteExtractorWindow::OnFolderOrganizerClicked()
{
	TSharedRef<SWindow> OrgWindow = SNew(SWindow)
		.Title(LOCTEXT("FolderOrganizerTitle", "Organize Folders"))
		.ClientSize(FVector2D(1050, 550))
		.SupportsMinimize(false).SupportsMaximize(false);

	// Folder naming state
	TSharedRef<bool> bDlgFolderIncPrefix = MakeShared<bool>(bFolderIncludePrefix);
	TSharedRef<bool> bDlgFolderIncSuffix = MakeShared<bool>(bFolderIncludeSuffix);
	TSharedRef<FString> DlgFolderPrefix = MakeShared<FString>(FolderCustomPrefix);
	TSharedRef<FString> DlgFolderSuffix = MakeShared<FString>(FolderCustomSuffix);
	TSharedRef<FString> DlgFolderFind = MakeShared<FString>(FolderFindStr);
	TSharedRef<FString> DlgFolderReplace = MakeShared<FString>(FolderReplaceStr);
	TSharedRef<FString> DlgFolderRemove = MakeShared<FString>(FolderRemoveStr);
	TSharedRef<bool> bDlgFolderIncBatchSuffix = MakeShared<bool>(bFolderIncludeBatchSuffix);

	// Build tree root
	TSharedRef<FBulkFolderNode> Root = MakeShared<FBulkFolderNode>();
	Root->Name = TEXT("Root");

	// Unassigned list — textures not yet placed in folders. Folder assignments are read from each
	// texture state's SubfolderPath (F13): keying off the array index broke when a non-last texture
	// was removed, because surviving textures shifted down and inherited the wrong folder. The
	// SubfolderPath travels with its state across removal/reindex, so the reconstruction stays correct.
	TSharedRef<TArray<TSharedPtr<FBulkFolderNode>>> Unassigned = MakeShared<TArray<TSharedPtr<FBulkFolderNode>>>();
	{
		// Helper: find or create intermediate folder nodes along a path.
		auto FindOrCreateFolder = [&Root](const FString& FolderPath) -> TSharedPtr<FBulkFolderNode>
		{
			TArray<FString> Parts;
			FolderPath.ParseIntoArray(Parts, TEXT("/"));
			TSharedPtr<FBulkFolderNode> Current = Root;
			for (const FString& Part : Parts)
			{
				TSharedPtr<FBulkFolderNode> Found;
				for (auto& C : Current->Children)
				{
					if (C->IsFolder() && C->Name == Part)
					{
						Found = C;
						break;
					}
				}
				if (!Found.IsValid())
				{
					Found = MakeShared<FBulkFolderNode>();
					Found->Name = Part;
					Found->Parent = Current;
					Current->Children.Add(Found);
				}
				Current = Found;
			}
			return Current;
		};

		for (int32 i = 0; i < TextureStates.Num(); i++)
		{
			TSharedPtr<FBulkFolderNode> Node = MakeShared<FBulkFolderNode>();
			Node->Name = TextureStates[i]->DisplayName;
			Node->TextureIndex = i;

			// SubfolderPath (when set) includes the texture name as the leaf — split it off to
			// recover the folder, e.g. "Attacks/Slash/TextureName" -> folder "Attacks/Slash".
			const FString& AssignedPath = TextureStates[i]->SubfolderPath;
			FString FolderPath, LeafName;
			if (!AssignedPath.IsEmpty()
				&& AssignedPath.Split(TEXT("/"), &FolderPath, &LeafName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
			{
				TSharedPtr<FBulkFolderNode> Folder = FindOrCreateFolder(FolderPath);
				Node->Parent = Folder;
				Folder->Children.Add(Node);
			}
			else
			{
				// No assignment, or a single-segment path (texture sat directly under root with no
				// real folder) — leave it Unassigned.
				Unassigned->Add(Node);
			}
		}
	}

	// Tree items for STreeView
	TSharedRef<TArray<TSharedPtr<FBulkFolderNode>>> TreeItems = MakeShared<TArray<TSharedPtr<FBulkFolderNode>>>();

	TSharedPtr<SListView<TSharedPtr<FBulkFolderNode>>> UnassignedList;
	TSharedPtr<STreeView<TSharedPtr<FBulkFolderNode>>> FolderTree;
	TSharedRef<TSharedPtr<FBulkFolderNode>> SelectedFolder = MakeShared<TSharedPtr<FBulkFolderNode>>();

	// Collapse state is keyed by FBulkFolderNode::NodeId, not by GetPath(). A path key goes stale on
	// every rename or move of the folder OR of any ancestor, and the restore pass then force-expanded
	// every folder whose key it could not find — which is exactly why folders unfolded after an item
	// was added or dropped. Node identities survive reparenting, so this set stays valid across every
	// tree mutation. PersistentCollapsedFolders (paths) remains the across-reopen format and is
	// rebuilt from the LIVE tree by SyncPersistentCollapsedFolders, so a stale key can never leak.
	TSharedRef<TSet<FGuid>> CollapsedFolderIds = MakeShared<TSet<FGuid>>();
	// Nodes whose stored collapse state has already been pushed into STreeView. After the first push,
	// STreeView's own pointer-keyed expansion state is the live authority — that is what makes a
	// collapsed folder stay collapsed through an add / drop / move / rename.
	TSharedRef<TSet<FGuid>> ExpansionApplied = MakeShared<TSet<FGuid>>();
	// Set while we drive SetItemExpansion ourselves; Private_SetItemExpansion fires OnExpansionChanged
	// even for programmatic calls, and that echo must not be recorded as a user gesture.
	TSharedRef<bool> bSuppressExpansionWrites = MakeShared<bool>(false);

	// Search/filter state. Empty = show everything. Non-empty keeps matching nodes plus their
	// ancestors, and force-expands them WITHOUT recording those expansions.
	TSharedRef<FString> TreeFilter = MakeShared<FString>();
	TSharedRef<TSet<FGuid>> VisibleNodeIds = MakeShared<TSet<FGuid>>();

	// Deferred inline-rename target for a freshly created folder, matched by node IDENTITY (several
	// folders can legitimately be named "NewFolder", so a name match picked the wrong row).
	TSharedRef<TSharedPtr<FBulkFolderNode>> PendingRenameNode = MakeShared<TSharedPtr<FBulkFolderNode>>();
	TSharedRef<TWeakPtr<SInlineEditableTextBlock>> PendingRenameWidget = MakeShared<TWeakPtr<SInlineEditableTextBlock>>();

	// Seed the id-keyed collapse set from the persisted path set. This is the only place paths are
	// read; from here on the session works in identities.
	{
		TFunction<void(const TSharedPtr<FBulkFolderNode>&)> SeedCollapsed =
			[this, &SeedCollapsed, CollapsedFolderIds](const TSharedPtr<FBulkFolderNode>& Node)
		{
			if (!Node.IsValid()) return;
			for (const TSharedPtr<FBulkFolderNode>& Child : Node->Children)
			{
				if (Child.IsValid() && Child->IsFolder())
				{
					if (PersistentCollapsedFolders.Contains(Child->GetPath()))
					{
						CollapsedFolderIds->Add(Child->NodeId);
					}
					SeedCollapsed(Child);
				}
			}
		};
		SeedCollapsed(Root);
	}

	// Rebuild the persisted (path-keyed) mirror from the LIVE tree. Called after every mutation that
	// can change a folder's path, so a rename or move can never leave a stale path behind to silently
	// collapse a future same-named folder. This also covers DESCENDANTS, which the old per-node key
	// remap in the rename handler did not.
	auto SyncPersistentCollapsedFolders = [this, Root, CollapsedFolderIds]()
	{
		TSet<FString> Paths;
		TFunction<void(const TSharedPtr<FBulkFolderNode>&)> Walk =
			[&Walk, &Paths, CollapsedFolderIds](const TSharedPtr<FBulkFolderNode>& Node)
		{
			if (!Node.IsValid()) return;
			for (const TSharedPtr<FBulkFolderNode>& Child : Node->Children)
			{
				if (Child.IsValid() && Child->IsFolder())
				{
					if (CollapsedFolderIds->Contains(Child->NodeId))
					{
						Paths.Add(Child->GetPath());
					}
					Walk(Child);
				}
			}
		};
		Walk(Root);
		PersistentCollapsedFolders = MoveTemp(Paths);
	};

	// Same resolution the OUTPUT PATH readout uses, so folder-row tooltips can preview exactly what
	// BuildOutputPath will produce at extraction time.
	auto ResolveBaseOutputPath = [this]() -> FString
	{
		FString Path = OutputPathOverride;
		if (Path.IsEmpty())
		{
			if (UPaper2DPlusCharacterProfileAsset* P = TargetProfile.LoadSynchronous())
			{
				Path = FPackageName::GetLongPackagePath(P->GetPackage()->GetName()) / TEXT("Sprites");
			}
			else if (TextureStates.Num() > 0 && TextureStates[0].IsValid())
			{
				if (UTexture2D* T = TextureStates[0]->Texture.LoadSynchronous())
				{
					Path = FPackageName::GetLongPackagePath(T->GetPackage()->GetName());
				}
			}
		}
		if (Path.IsEmpty()) Path = TEXT("/Game/Sprites");
		return Path;
	};

	auto RefreshTree = [TreeItems, Root, &FolderTree, CollapsedFolderIds, ExpansionApplied,
		bSuppressExpansionWrites, TreeFilter, VisibleNodeIds]()
	{
		const FString Filter = *TreeFilter;
		const bool bFiltering = !Filter.IsEmpty();

		// A node is visible when it matches, or any descendant matches (so the match is reachable).
		VisibleNodeIds->Empty();
		if (bFiltering)
		{
			TFunction<bool(const TSharedPtr<FBulkFolderNode>&)> MarkVisible =
				[&MarkVisible, &Filter, VisibleNodeIds](const TSharedPtr<FBulkFolderNode>& Node) -> bool
			{
				if (!Node.IsValid()) return false;
				bool bAnyDescendantMatches = false;
				for (const TSharedPtr<FBulkFolderNode>& Child : Node->Children)
				{
					bAnyDescendantMatches |= MarkVisible(Child);
				}
				if (Node->Name.Contains(Filter, ESearchCase::IgnoreCase) || bAnyDescendantMatches)
				{
					VisibleNodeIds->Add(Node->NodeId);
					return true;
				}
				return false;
			};
			for (const TSharedPtr<FBulkFolderNode>& Child : Root->Children)
			{
				MarkVisible(Child);
			}
		}

		TreeItems->Reset();
		for (const TSharedPtr<FBulkFolderNode>& Child : Root->Children)
		{
			if (Child.IsValid() && (!bFiltering || VisibleNodeIds->Contains(Child->NodeId)))
			{
				TreeItems->Add(Child);
			}
		}

		if (!FolderTree.IsValid()) return;
		FolderTree->RequestTreeRefresh();

		// While a filter is active every visible folder is force-expanded so matches are reachable,
		// and those expansions are deliberately NOT recorded. With no filter the stored state is
		// pushed ONCE per node; afterwards STreeView owns expansion, which is the fix for both
		// unfold mechanisms (stale path keys, and first-child force-expansion of an empty folder —
		// the child-count test now guards recursion only, so empty folders get a state too).
		*bSuppressExpansionWrites = true;
		TFunction<void(const TSharedPtr<FBulkFolderNode>&)> ApplyExpansion =
			[&ApplyExpansion, &FolderTree, bFiltering, CollapsedFolderIds, ExpansionApplied, VisibleNodeIds]
			(const TSharedPtr<FBulkFolderNode>& Node)
		{
			if (!Node.IsValid() || !Node->IsFolder()) return;
			if (bFiltering)
			{
				if (VisibleNodeIds->Contains(Node->NodeId))
				{
					FolderTree->SetItemExpansion(Node, true);
				}
			}
			else if (!ExpansionApplied->Contains(Node->NodeId))
			{
				ExpansionApplied->Add(Node->NodeId);
				FolderTree->SetItemExpansion(Node, !CollapsedFolderIds->Contains(Node->NodeId));
			}
			for (const TSharedPtr<FBulkFolderNode>& Child : Node->Children)
			{
				ApplyExpansion(Child);
			}
		};
		for (const TSharedPtr<FBulkFolderNode>& Child : Root->Children)
		{
			ApplyExpansion(Child);
		}
		*bSuppressExpansionWrites = false;
	};

	auto RefreshAll = [&UnassignedList, RefreshTree]()
	{
		RefreshTree();
		if (UnassignedList.IsValid()) UnassignedList->RequestListRefresh();
	};

	// One delete implementation shared by the "- Delete" button and the Delete/Backspace key, so the
	// two can never diverge. Multi-selection aware; returns true when something was actually removed.
	auto DeleteSelectedFolders = [SelectedFolder, Unassigned, RefreshAll, &FolderTree,
		CollapsedFolderIds, ExpansionApplied, SyncPersistentCollapsedFolders]() -> bool
	{
		TArray<TSharedPtr<FBulkFolderNode>> Targets;
		if (FolderTree.IsValid())
		{
			Targets = FolderTree->GetSelectedItems();
		}
		if (Targets.IsEmpty() && SelectedFolder->IsValid())
		{
			Targets.Add(*SelectedFolder);
		}
		Targets.RemoveAll([](const TSharedPtr<FBulkFolderNode>& Node)
		{
			return !Node.IsValid() || !Node->IsFolder();
		});
		BulkFolderOrganizer_Internal::PruneNestedNodes(Targets);
		if (Targets.IsEmpty()) return false;

		// Hoist every descendant texture back to Unassigned and drop the whole subtree's collapse
		// keys. Recursion is unconditional: the old `if (C->Children.Num() > 0)` guard meant an EMPTY
		// sub-folder kept its collapse key forever after its parent was deleted.
		TFunction<void(const TSharedPtr<FBulkFolderNode>&)> Flatten =
			[&Flatten, Unassigned, CollapsedFolderIds, ExpansionApplied](const TSharedPtr<FBulkFolderNode>& Node)
		{
			CollapsedFolderIds->Remove(Node->NodeId);
			ExpansionApplied->Remove(Node->NodeId);
			for (const TSharedPtr<FBulkFolderNode>& Child : Node->Children)
			{
				if (!Child.IsValid()) continue;
				if (Child->IsFolder())
				{
					Flatten(Child);
				}
				else
				{
					Child->Parent.Reset();
					Child->Children.Empty();
					Unassigned->Add(Child);
				}
			}
		};

		for (const TSharedPtr<FBulkFolderNode>& Target : Targets)
		{
			Flatten(Target);
			TSharedPtr<FBulkFolderNode> ParentNode = Target->Parent.Pin();
			if (ParentNode.IsValid()) ParentNode->Remove(Target);
		}

		SyncPersistentCollapsedFolders();
		*SelectedFolder = nullptr;
		if (FolderTree.IsValid()) FolderTree->ClearSelection();
		RefreshAll();
		return true;
	};

	// Shared drop-to-unassign body used by the UNASSIGNED pane border and every unassigned row.
	// Multi-selection aware: the whole dragged selection moves, not just the row under the cursor.
	auto UnassignDraggedNodes = [Unassigned, RefreshAll, CollapsedFolderIds, ExpansionApplied,
		SyncPersistentCollapsedFolders](const TSharedPtr<FFolderNodeDragDropOp>& Op) -> FReply
	{
		if (!Op.IsValid() || Op->bFromUnassigned) return FReply::Unhandled();

		TArray<TSharedPtr<FBulkFolderNode>> Nodes = Op->DraggedNodes;
		BulkFolderOrganizer_Internal::PruneNestedNodes(Nodes);
		if (Nodes.IsEmpty()) return FReply::Unhandled();

		for (const TSharedPtr<FBulkFolderNode>& Dragged : Nodes)
		{
			TSharedPtr<FBulkFolderNode> OldParent = Dragged->Parent.Pin();
			if (OldParent.IsValid()) OldParent->Remove(Dragged);

			if (Dragged->IsFolder())
			{
				// The folder itself is discarded — hoist its textures out and drop its collapse keys.
				TFunction<void(const TSharedPtr<FBulkFolderNode>&)> CollectTextures =
					[&CollectTextures, Unassigned, CollapsedFolderIds, ExpansionApplied](const TSharedPtr<FBulkFolderNode>& Node)
				{
					CollapsedFolderIds->Remove(Node->NodeId);
					ExpansionApplied->Remove(Node->NodeId);
					for (const TSharedPtr<FBulkFolderNode>& Child : Node->Children)
					{
						if (!Child.IsValid()) continue;
						if (Child->IsFolder())
						{
							CollectTextures(Child);
						}
						else
						{
							Child->Parent.Reset();
							Unassigned->Add(Child);
						}
					}
				};
				CollectTextures(Dragged);
			}
			else
			{
				Dragged->Parent.Reset();
				Unassigned->Add(Dragged);
			}
		}

		SyncPersistentCollapsedFolders();
		RefreshAll();
		return FReply::Handled();
	};

	OrgWindow->SetContent(
		SNew(SVerticalBox)

		// Main content
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SSplitter).Orientation(Orient_Horizontal)

			// Left: folder naming
			+ SSplitter::Slot().Value(0.25f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
				[
					SNew(STextBlock).Text(LOCTEXT("OrgFolderNaming", "FOLDER NAMING"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
				+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8, 0, 4, 0)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(SVerticalBox)
						// Output path
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
						[
							SNew(STextBlock).Text(LOCTEXT("OrgOutputLabel", "Output Path"))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text_Lambda([ResolveBaseOutputPath]() -> FText
								{
									return FText::FromString(ResolveBaseOutputPath() + TEXT("/"));
								})
								.AutoWrapText(true)
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0).VAlign(VAlign_Center)
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "SimpleButton")
								.ToolTipText(LOCTEXT("OrgBrowseOutput", "Browse for output folder"))
								.OnClicked_Lambda([this]() -> FReply
								{
									FString DefaultPath = OutputPathOverride.IsEmpty() ? TEXT("/Game/Sprites") : OutputPathOverride;
									TSharedRef<FString> SelectedPath = MakeShared<FString>(DefaultPath);
									FPathPickerConfig Config;
									Config.DefaultPath = *SelectedPath;
									Config.bAllowContextMenu = true;
									Config.bAddDefaultPath = true;
									Config.OnPathSelected = FOnPathSelected::CreateLambda([SelectedPath](const FString& Path) { *SelectedPath = Path; });
									FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
									TSharedRef<SWidget> PathPicker = CBModule.Get().CreatePathPicker(Config);
									TSharedRef<SWindow> PickerWindow = SNew(SWindow)
										.Title(LOCTEXT("OrgChooseFolder", "Choose Output Folder"))
										.ClientSize(FVector2D(400, 500))
										.SupportsMinimize(false).SupportsMaximize(false);
									PickerWindow->SetContent(
										SNew(SVerticalBox)
										+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4) [ PathPicker ]
										+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(4)
										[
											SNew(SButton)
											.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
											.Text(LOCTEXT("OrgSelectFolder", "Select"))
											.OnClicked_Lambda([this, PickerWindow, SelectedPath]() -> FReply
											{
												OutputPathOverride = *SelectedPath;
												PickerWindow->RequestDestroyWindow();
												return FReply::Handled();
											})
										]
									);
									FSlateApplication::Get().AddModalWindow(PickerWindow, AsShared());
									return FReply::Handled();
								})
								[
									SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.FolderOpen"))
								]
							]
						]
						// The "Create subfolders" checkbox that used to sit here is gone: Apply force-
						// cleared the flag it set, and an organized texture's folder is already
						// implicit in its SubfolderPath. Every texture now gets its own folder.
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 4)
						[
							SNew(SSeparator)
						]
						// Include prefix in folder
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[
							SNew(SCheckBox)
							.IsChecked_Lambda([bDlgFolderIncPrefix]() { return *bDlgFolderIncPrefix ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
							.OnCheckStateChanged_Lambda([bDlgFolderIncPrefix](ECheckBoxState S)
							{
								*bDlgFolderIncPrefix = (S == ECheckBoxState::Checked);
							})
							[
								SNew(STextBlock).Text(LOCTEXT("OrgIncPrefix", "Include prefix in folder"))
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[
							SNew(SCheckBox)
							.IsChecked_Lambda([bDlgFolderIncSuffix]() { return *bDlgFolderIncSuffix ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
							.OnCheckStateChanged_Lambda([bDlgFolderIncSuffix](ECheckBoxState S)
							{
								*bDlgFolderIncSuffix = (S == ECheckBoxState::Checked);
							})
							[
								SNew(STextBlock).Text(LOCTEXT("OrgIncSuffix", "Include suffix in folder"))
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[
							SNew(SCheckBox)
							.Visibility_Lambda([this]() { return LastBatchSuffix.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
							.IsChecked_Lambda([bDlgFolderIncBatchSuffix]() { return *bDlgFolderIncBatchSuffix ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
							.OnCheckStateChanged_Lambda([bDlgFolderIncBatchSuffix](ECheckBoxState S) { *bDlgFolderIncBatchSuffix = (S == ECheckBoxState::Checked); })
							[
								SNew(STextBlock)
								.Text_Lambda([this]() { return FText::Format(LOCTEXT("OrgIncBatchSfx", "Include batch suffix \"{0}\""), FText::FromString(LastBatchSuffix)); })
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 2)
						[ SNew(STextBlock).Text(LOCTEXT("OrgFolderPrefixLbl", "Folder Prefix")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[
							SNew(SEditableTextBox)
							.HintText(LOCTEXT("OrgFolderPrefixHint", "Prefix..."))
							.OnTextChanged_Lambda([DlgFolderPrefix](const FText& T) { *DlgFolderPrefix = T.ToString(); })
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 2)
						[ SNew(STextBlock).Text(LOCTEXT("OrgFolderSuffixLbl", "Folder Suffix")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[
							SNew(SEditableTextBox)
							.HintText(LOCTEXT("OrgFolderSuffixHint", "Suffix..."))
							.OnTextChanged_Lambda([DlgFolderSuffix](const FText& T) { *DlgFolderSuffix = T.ToString(); })
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						]
						// Folder find & replace
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 2)
						[
							SNew(SSeparator)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[ SNew(STextBlock).Text(LOCTEXT("OrgFolderFind", "Find & Replace")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
							[
								SNew(SEditableTextBox)
								.HintText(LOCTEXT("OrgFolderFindHint", "Find..."))
								.OnTextChanged_Lambda([DlgFolderFind](const FText& T) { *DlgFolderFind = T.ToString(); })
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0, 0, 0)
							[
								SNew(SEditableTextBox)
								.HintText(LOCTEXT("OrgFolderReplaceHint", "Replace..."))
								.OnTextChanged_Lambda([DlgFolderReplace](const FText& T) { *DlgFolderReplace = T.ToString(); })
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 2)
						[ SNew(STextBlock).Text(LOCTEXT("OrgFolderRemove", "Remove Text")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[
							SNew(SEditableTextBox)
							.HintText(LOCTEXT("OrgFolderRemoveHint", "Text to remove..."))
							.OnTextChanged_Lambda([DlgFolderRemove](const FText& T) { *DlgFolderRemove = T.ToString(); })
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						]

						// Folder preview
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
							.Padding(6)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(STextBlock)
									.Text(LOCTEXT("OrgFolderExample", "Example:"))
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
									.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
								[
									SNew(STextBlock)
									.Text_Lambda([this, bDlgFolderIncPrefix, bDlgFolderIncSuffix, DlgFolderPrefix, DlgFolderSuffix, DlgFolderFind, DlgFolderReplace, DlgFolderRemove, bDlgFolderIncBatchSuffix]() -> FText
									{
										FString ExName = TextureStates.Num() > 0 ? TextureStates[0]->DisplayName : TEXT("TextureName");
										if (!DlgFolderRemove->IsEmpty()) ExName = ExName.Replace(**DlgFolderRemove, TEXT(""));
										if (!DlgFolderFind->IsEmpty()) ExName = ExName.Replace(**DlgFolderFind, **DlgFolderReplace);
										FString Folder;
										if (!DlgFolderPrefix->IsEmpty()) Folder += *DlgFolderPrefix;
										if (*bDlgFolderIncPrefix && !NamePrefix.IsEmpty()) Folder += NamePrefix + TEXT("_");
										Folder += ExName;
										if (*bDlgFolderIncBatchSuffix && !LastBatchSuffix.IsEmpty()) Folder += LastBatchSuffix;
										if (!DlgFolderSuffix->IsEmpty()) Folder += *DlgFolderSuffix;
										FString BasePath = OutputPathOverride.IsEmpty() ? TEXT("/Game/Sprites") : OutputPathOverride;
										return FText::FromString(BasePath / Folder + TEXT("/"));
									})
									.AutoWrapText(true)
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
								]
							]
						]
					]
				]
			]

			// Center: unassigned textures (Unit 9: TASK-35 — drop target for moving items back)
			+ SSplitter::Slot().Value(0.3f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
				[
					SNew(STextBlock).Text(LOCTEXT("UnassignedLabel", "UNASSIGNED"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
				+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8, 0, 4, 0)
				[
					SNew(SDropTargetBorder)
					.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
					.Padding(2)
					.OnDropCallback(SDropTargetBorder::FOnDropCallback([UnassignDraggedNodes](const FGeometry&, const FDragDropEvent& Event) -> FReply
					{
						return UnassignDraggedNodes(Event.GetOperationAs<FFolderNodeDragDropOp>());
					}))
					[
						SAssignNew(UnassignedList, SListView<TSharedPtr<FBulkFolderNode>>)
						.ListItemsSource(Unassigned.operator->())
						.SelectionMode(ESelectionMode::Multi)
						.OnGenerateRow_Lambda([&UnassignedList, UnassignDraggedNodes](TSharedPtr<FBulkFolderNode> Item, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
						{
							return SNew(STableRow<TSharedPtr<FBulkFolderNode>>, Owner)
							.OnDragDetected_Lambda([Item, &UnassignedList](const FGeometry&, const FPointerEvent&) -> FReply
							{
								if (!Item.IsValid()) return FReply::Unhandled();

								// Carry the WHOLE selection when the grabbed row is part of it; a row
								// outside the selection carries only itself and takes the selection
								// with it (the Content Browser / Outliner convention).
								TArray<TSharedPtr<FBulkFolderNode>> Nodes;
								if (UnassignedList.IsValid())
								{
									Nodes = UnassignedList->GetSelectedItems();
									if (!Nodes.Contains(Item))
									{
										UnassignedList->SetSelection(Item, ESelectInfo::Direct);
										Nodes.Reset();
									}
								}
								if (Nodes.IsEmpty()) Nodes.Add(Item);
								return FReply::Handled().BeginDragDrop(FFolderNodeDragDropOp::New(Nodes, true));
							})
							.OnCanAcceptDrop_Lambda([](const FDragDropEvent& Event, EItemDropZone, TSharedPtr<FBulkFolderNode>) -> TOptional<EItemDropZone>
							{
								TSharedPtr<FFolderNodeDragDropOp> Op = Event.GetOperationAs<FFolderNodeDragDropOp>();
								if (Op.IsValid() && !Op->bFromUnassigned)
								{
									return EItemDropZone::OntoItem;
								}
								return TOptional<EItemDropZone>();
							})
							.OnAcceptDrop_Lambda([UnassignDraggedNodes](const FDragDropEvent& Event, EItemDropZone, TSharedPtr<FBulkFolderNode>) -> FReply
							{
								return UnassignDraggedNodes(Event.GetOperationAs<FFolderNodeDragDropOp>());
							})
							[
								SNew(SBorder)
								.BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
								.Padding(FMargin(4, 3))
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
									[
										SNew(SImage)
										.Image(FAppStyle::Get().GetBrush("Icons.FilledCircle"))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.6f, 0.9f)))
										.DesiredSizeOverride(FVector2D(8, 8))
									]
									+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
									[
										SNew(STextBlock).Text(FText::FromString(Item->Name))
											.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
									]
								]
							];
						})
					]
				]
			]

			// Right: folder tree
			+ SSplitter::Slot().Value(0.45f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(STextBlock).Text(LOCTEXT("FolderTreeLabel", "FOLDER STRUCTURE"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.Text(LOCTEXT("AddAnimationGroups", "+ Animation Groups"))
						.ToolTipText(LOCTEXT("AddAnimationGroupsTip", "Add one top-level folder for each direct child of Paper2DPlus.Animation, such as Locomotion and Combat. Sub-tags are ignored."))
						.OnClicked_Lambda([Root, RefreshAll]() -> FReply
						{
							using namespace Paper2DPlus::BulkFolderOrganization;

							const TArray<FString> GroupNames = GetLiveAnimationGroupFolderNames();
							if (GroupNames.IsEmpty())
							{
								FNotificationInfo Info(LOCTEXT("NoAnimationGroups", "No top-level Paper2DPlus.Animation groups are registered."));
								Info.ExpireDuration = 4.0f;
								FSlateNotificationManager::Get().AddNotification(Info);
								return FReply::Handled();
							}

							TArray<FString> ExistingRootFolders;
							for (const TSharedPtr<FBulkFolderNode>& Child : Root->Children)
							{
								if (Child.IsValid() && Child->IsFolder())
								{
									ExistingRootFolders.Add(Child->Name);
								}
							}

							const TArray<FString> MissingGroups = FindMissingRootFolderNames(GroupNames, ExistingRootFolders);
							for (const FString& GroupName : MissingGroups)
							{
								TSharedPtr<FBulkFolderNode> Folder = MakeShared<FBulkFolderNode>();
								// Injected names go through the same predicate the inline rename enforces,
								// so a tag name that is not a legal package-path segment can never reach
								// BuildOutputPath and fail only at Extract All.
								Folder->Name = BulkFolderOrganizer_Internal::MakeUniqueFolderName(Root, GroupName);
								Folder->Parent = Root;
								Root->Children.Add(Folder);
							}

							FNotificationInfo Info(MissingGroups.IsEmpty()
								? LOCTEXT("AnimationGroupsPresent", "Animation group folders are already present.")
								: FText::Format(LOCTEXT("AnimationGroupsAdded", "Added {0} animation group folders."), FText::AsNumber(MissingGroups.Num())));
							Info.ExpireDuration = 4.0f;
							FSlateNotificationManager::Get().AddNotification(Info);
							if (!MissingGroups.IsEmpty())
							{
								RefreshAll();
							}
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.Text(LOCTEXT("NewFolder", "+ Folder"))
						.OnClicked_Lambda([Root, SelectedFolder, RefreshAll, &FolderTree, CollapsedFolderIds,
							ExpansionApplied, SyncPersistentCollapsedFolders, PendingRenameNode, PendingRenameWidget]() -> FReply
						{
							TSharedPtr<FBulkFolderNode> Target = Root;
							if (SelectedFolder->IsValid() && (*SelectedFolder)->IsFolder())
							{
								Target = *SelectedFolder;
							}

							TSharedPtr<FBulkFolderNode> NewFolder = MakeShared<FBulkFolderNode>();
							// Uniquified at creation so the default name already passes the rename
							// validator and two siblings can never collapse onto one output directory.
							NewFolder->Name = BulkFolderOrganizer_Internal::MakeUniqueFolderName(Target, TEXT("NewFolder"));
							NewFolder->Parent = Target;
							Target->Children.Add(NewFolder);

							// Expand every ancestor so the new row is actually reachable (the old code
							// only un-collapsed the immediate parent, and only in the path set).
							if (FolderTree.IsValid())
							{
								for (TSharedPtr<FBulkFolderNode> Ancestor = Target;
									Ancestor.IsValid() && Ancestor->Parent.IsValid();
									Ancestor = Ancestor->Parent.Pin())
								{
									CollapsedFolderIds->Remove(Ancestor->NodeId);
									ExpansionApplied->Add(Ancestor->NodeId);
									FolderTree->SetItemExpansion(Ancestor, true);
								}
							}
							SyncPersistentCollapsedFolders();

							*PendingRenameNode = NewFolder;
							PendingRenameWidget->Reset();
							*SelectedFolder = NewFolder;
							RefreshAll();

							if (FolderTree.IsValid())
							{
								FolderTree->SetSelection(NewFolder, ESelectInfo::Direct);
								FolderTree->RequestScrollIntoView(NewFolder);

								// Defer inline rename so STreeView has generated the row first. The row
								// may not exist yet (virtualization + an async scroll-into-view), and
								// EnterEditingMode is a no-op while a mouse captor is live, so retry a
								// few frames instead of silently doing nothing. Registered on the tree —
								// a widget inside THIS modal — not on the extractor behind it.
								TSharedRef<int32> Attempts = MakeShared<int32>(0);
								FolderTree->RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
									[PendingRenameNode, PendingRenameWidget, Attempts](double, float) -> EActiveTimerReturnType
									{
										TSharedPtr<SInlineEditableTextBlock> NameWidget = PendingRenameWidget->Pin();
										if (NameWidget.IsValid() && !FSlateApplication::Get().HasAnyMouseCaptor())
										{
											NameWidget->EnterEditingMode();
											if (NameWidget->IsInEditMode())
											{
												PendingRenameNode->Reset();
												PendingRenameWidget->Reset();
												return EActiveTimerReturnType::Stop;
											}
										}
										if (++(*Attempts) < 8)
										{
											return EActiveTimerReturnType::Continue;
										}
										PendingRenameNode->Reset();
										PendingRenameWidget->Reset();
										return EActiveTimerReturnType::Stop;
									}));
							}
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.Text(LOCTEXT("DeleteFolder", "- Delete"))
						.ToolTipText(LOCTEXT("DeleteFolderTip", "Delete the selected folder(s). Their textures move back to UNASSIGNED. Delete or Backspace does the same from the tree."))
						.OnClicked_Lambda([DeleteSelectedFolders]() -> FReply
						{
							DeleteSelectedFolders();
							return FReply::Handled();
						})
					]
				]
				// Search / filter over the folder tree.
				+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 4)
				[
					SNew(SSearchBox)
					.HintText(LOCTEXT("OrgTreeSearchHint", "Search folders and textures..."))
					.ToolTipText(LOCTEXT("OrgTreeSearchTip", "Filter the folder tree. Matching folders and textures stay visible along with their parent folders, and matches are expanded automatically. Clearing the box restores your own collapsed folders."))
					.OnTextChanged_Lambda([TreeFilter, ExpansionApplied, RefreshAll](const FText& NewText)
					{
						const FString Next = NewText.ToString();
						if (Next.Equals(*TreeFilter, ESearchCase::CaseSensitive)) return;
						*TreeFilter = Next;
						// Leaving the filter re-applies the stored collapse state once; the filter's own
						// force-expansions were never recorded, so nothing was corrupted while it was on.
						if (TreeFilter->IsEmpty()) ExpansionApplied->Empty();
						RefreshAll();
					})
				]
				+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4, 0, 8, 0)
				[
					SNew(SFolderTreeHostBorder)
					.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
					.Padding(2)
					.OnDeleteRequested(SFolderTreeHostBorder::FOnDeleteRequested([DeleteSelectedFolders]() -> bool
					{
						return DeleteSelectedFolders();
					}))
					[
						SAssignNew(FolderTree, STreeView<TSharedPtr<FBulkFolderNode>>)
						.TreeItemsSource(TreeItems.operator->())
						.SelectionMode(ESelectionMode::Multi)
						.OnGetChildren_Lambda([TreeFilter, VisibleNodeIds](TSharedPtr<FBulkFolderNode> Item, TArray<TSharedPtr<FBulkFolderNode>>& OutChildren)
						{
							if (!Item.IsValid()) return;
							if (TreeFilter->IsEmpty())
							{
								OutChildren = Item->Children;
								return;
							}
							// Filtering hides rows without ever mutating the tree, so a drop or a
							// rename made while the search box is live still lands on the real node.
							for (const TSharedPtr<FBulkFolderNode>& Child : Item->Children)
							{
								if (Child.IsValid() && VisibleNodeIds->Contains(Child->NodeId))
								{
									OutChildren.Add(Child);
								}
							}
						})
						.OnSelectionChanged_Lambda([SelectedFolder, &FolderTree](TSharedPtr<FBulkFolderNode> Item, ESelectInfo::Type)
						{
							// The "focus" node drives "+ Folder" targeting; Delete reads the full selection.
							if (Item.IsValid())
							{
								*SelectedFolder = Item;
								return;
							}
							TArray<TSharedPtr<FBulkFolderNode>> Selection;
							if (FolderTree.IsValid()) Selection = FolderTree->GetSelectedItems();
							*SelectedFolder = Selection.Num() > 0 ? Selection.Last() : nullptr;
						})
						.OnExpansionChanged_Lambda([CollapsedFolderIds, bSuppressExpansionWrites, TreeFilter, SyncPersistentCollapsedFolders]
							(TSharedPtr<FBulkFolderNode> Item, bool bExpanded)
						{
							// Skip the echo from our own SetItemExpansion, and never record expansions a
							// live search filter forced open — either would corrupt the collapse set.
							if (*bSuppressExpansionWrites || !TreeFilter->IsEmpty()) return;
							if (!Item.IsValid() || !Item->IsFolder()) return;
							if (bExpanded) CollapsedFolderIds->Remove(Item->NodeId);
							else CollapsedFolderIds->Add(Item->NodeId);
							SyncPersistentCollapsedFolders();
						})
						.OnGenerateRow_Lambda([Unassigned, RefreshAll, &FolderTree, PendingRenameNode, PendingRenameWidget,
							SyncPersistentCollapsedFolders, ResolveBaseOutputPath](TSharedPtr<FBulkFolderNode> Item, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
						{
							const bool bFolder = Item->IsFolder();

							using FFolderRow = STableRow<TSharedPtr<FBulkFolderNode>>;
							TSharedRef<FFolderRow> Row = SNew(FFolderRow, Owner)
							.OnDragDetected_Lambda([Item, &FolderTree](const FGeometry&, const FPointerEvent&) -> FReply
							{
								if (!Item.IsValid()) return FReply::Unhandled();

								// Carry the WHOLE selection when the grabbed row is part of it; a row
								// outside the selection carries only itself and takes the selection with it.
								TArray<TSharedPtr<FBulkFolderNode>> Nodes;
								if (FolderTree.IsValid())
								{
									Nodes = FolderTree->GetSelectedItems();
									if (!Nodes.Contains(Item))
									{
										FolderTree->SetSelection(Item, ESelectInfo::Direct);
										Nodes.Reset();
									}
								}
								if (Nodes.IsEmpty()) Nodes.Add(Item);
								return FReply::Handled().BeginDragDrop(FFolderNodeDragDropOp::New(Nodes, false));
							})
							.OnCanAcceptDrop_Lambda([](const FDragDropEvent&, EItemDropZone, TSharedPtr<FBulkFolderNode>) -> TOptional<EItemDropZone>
							{
								return EItemDropZone::OntoItem;
							})
							.OnAcceptDrop_Lambda([Unassigned, RefreshAll, SyncPersistentCollapsedFolders]
								(const FDragDropEvent& Event, EItemDropZone, TSharedPtr<FBulkFolderNode> TargetItem) -> FReply
							{
								TSharedPtr<FFolderNodeDragDropOp> Op = Event.GetOperationAs<FFolderNodeDragDropOp>();
								if (!Op.IsValid() || !TargetItem.IsValid()) return FReply::Unhandled();

								TArray<TSharedPtr<FBulkFolderNode>> Nodes = Op->DraggedNodes;
								BulkFolderOrganizer_Internal::PruneNestedNodes(Nodes);

								int32 MovedCount = 0;
								for (const TSharedPtr<FBulkFolderNode>& Dragged : Nodes)
								{
									// Per-node guards: an offending node is SKIPPED, it does not abort the
									// whole drop, so one bad node in a multi-selection cannot cancel it.
									if (Dragged == TargetItem) continue;

									bool bWouldCycle = false;
									for (TSharedPtr<FBulkFolderNode> Check = TargetItem; Check.IsValid(); Check = Check->Parent.Pin())
									{
										if (Check == Dragged) { bWouldCycle = true; break; }
									}
									if (bWouldCycle) continue;

									if (Op->bFromUnassigned)
									{
										Unassigned->Remove(Dragged);
									}
									else
									{
										TSharedPtr<FBulkFolderNode> OldParent = Dragged->Parent.Pin();
										if (OldParent.IsValid()) OldParent->Remove(Dragged);
									}

									Dragged->Parent = TargetItem;
									TargetItem->Children.Add(Dragged);
									++MovedCount;
								}

								if (MovedCount == 0) return FReply::Unhandled();
								// Collapse state is node-identity keyed, so the moved subtree keeps its
								// expansion; only the persisted path mirror needs rebuilding.
								SyncPersistentCollapsedFolders();
								RefreshAll();
								return FReply::Handled();
							});

							TSharedRef<SWidget> NameSlot = SNullWidget::NullWidget;
							if (bFolder)
							{
								TSharedPtr<SInlineEditableTextBlock> NameWidget;
								SAssignNew(NameWidget, SInlineEditableTextBlock)
									.Text_Lambda([Item]() { return FText::FromString(Item->Name); })
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
									.IsSelected(FIsSelected::CreateSP(Row, &FFolderRow::IsSelectedExclusively))
									.OnVerifyTextChanged_Lambda([Item](const FText& NewText, FText& OutError) -> bool
									{
										// Shows the reason inline in red as the user types AND blocks the
										// commit (SInlineEditableTextBlock re-runs this on Enter and
										// reverts on focus loss).
										return BulkFolderOrganizer_Internal::IsFolderNameAcceptable(
											NewText.ToString(), Item, Item->Parent.Pin(), OutError);
									})
									.OnTextCommitted_Lambda([Item, SyncPersistentCollapsedFolders](const FText& NewText, ETextCommit::Type)
									{
										if (!Item.IsValid()) return;
										const FString Committed = NewText.ToString().TrimStartAndEnd();
										if (Committed.IsEmpty() || Committed.Equals(Item->Name, ESearchCase::CaseSensitive))
										{
											return; // includes the revert-on-rejected-focus-loss path
										}
										FText Unused;
										if (!BulkFolderOrganizer_Internal::IsFolderNameAcceptable(
											Committed, Item, Item->Parent.Pin(), Unused))
										{
											return;
										}
										Item->Name = Committed;
										// Collapse state is id-keyed, so a rename needs no key remap; only
										// the persisted path mirror is rebuilt — and it now covers
										// DESCENDANTS, which the old single-key remap silently missed.
										SyncPersistentCollapsedFolders();
									});

								if (PendingRenameNode->IsValid() && *PendingRenameNode == Item)
								{
									// Identity match, not a name match: several folders can be "NewFolder".
									*PendingRenameWidget = NameWidget;
								}
								NameSlot = NameWidget.ToSharedRef();
							}
							else
							{
								NameSlot = SNew(STextBlock)
									.Text_Lambda([Item]() { return FText::FromString(Item->Name); })
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9));
							}

							Row->SetContent(
								SNew(SBorder)
								.BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
								.Padding(FMargin(2, 2))
								// Tooltip on the ROW border, not the name widget, so it is reachable
								// across the whole row.
								.ToolTipText_Lambda([Item, bFolder, ResolveBaseOutputPath]() -> FText
								{
									if (!bFolder || !Item.IsValid()) return FText::GetEmpty();

									int32 SubfolderCount = 0;
									int32 TextureCount = 0;
									TFunction<void(const TSharedPtr<FBulkFolderNode>&)> Count =
										[&Count, &SubfolderCount, &TextureCount](const TSharedPtr<FBulkFolderNode>& Node)
									{
										for (const TSharedPtr<FBulkFolderNode>& Child : Node->Children)
										{
											if (!Child.IsValid()) continue;
											if (Child->IsFolder()) { ++SubfolderCount; Count(Child); }
											else { ++TextureCount; }
										}
									};
									Count(Item);

									return FText::Format(
										LOCTEXT("OrgFolderRowTip",
											"{0}\n\nHolds {1} sub-folder(s) and {2} texture(s), including nested ones.\nTextures filed here extract to:\n{3}/\n\nDrag textures onto this row to file them here; drag them onto UNASSIGNED to take them back out. Click the name of a selected folder to rename it."),
										FText::FromString(Item->GetPath()),
										FText::AsNumber(SubfolderCount),
										FText::AsNumber(TextureCount),
										FText::FromString(ResolveBaseOutputPath() / Item->GetPath()));
								})
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
									[
										SNew(SImage)
										.Image(FAppStyle::Get().GetBrush(bFolder ? "Icons.FolderClosed" : "Icons.FilledCircle"))
										.ColorAndOpacity(bFolder
											? FSlateColor(FLinearColor(0.9f, 0.7f, 0.2f))
											: FSlateColor(FLinearColor(0.4f, 0.6f, 0.9f)))
										.DesiredSizeOverride(bFolder ? FVector2D(14, 14) : FVector2D(8, 8))
									]
									+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
									[
										NameSlot
									]
								]);

							return Row;
						})
					]
				]
			]
		]

		// Bottom: Apply / Cancel
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("ApplyOrg", "Apply"))
				.OnClicked_Lambda([this, Root, OrgWindow, bDlgFolderIncPrefix, bDlgFolderIncSuffix, DlgFolderPrefix, DlgFolderSuffix, DlgFolderFind, DlgFolderReplace, DlgFolderRemove, bDlgFolderIncBatchSuffix, SyncPersistentCollapsedFolders]() -> FReply
				{
					// Belt-and-braces backstop. With inline validation in place this should be a no-op:
					// no illegal character can reach a committed folder name. It must apply the SAME
					// rule the inline rename does (AssetViewUtils::IsValidFolderName via
					// IsFolderNameAcceptable) — SanitizeAssetName replaces spaces and nothing else, so
					// on its own it let every other illegal package-path character through.
					TFunction<void(const TSharedPtr<FBulkFolderNode>&, const TSharedPtr<FBulkFolderNode>&)> SanitizeFolders =
						[&SanitizeFolders](const TSharedPtr<FBulkFolderNode>& Node, const TSharedPtr<FBulkFolderNode>& Parent)
					{
						if (!Node.IsValid()) return;
						FText Reason;
						if (Node->IsFolder()
							&& !BulkFolderOrganizer_Internal::IsFolderNameAcceptable(Node->Name, Node, Parent, Reason))
						{
							Node->Name = BulkFolderOrganizer_Internal::MakeUniqueFolderName(Parent, Node->Name);
						}
						for (const TSharedPtr<FBulkFolderNode>& C : Node->Children) SanitizeFolders(C, Node);
					};
					// The synthetic Root is excluded from every GetPath(), so start at its children.
					for (const TSharedPtr<FBulkFolderNode>& Child : Root->Children) SanitizeFolders(Child, Root);

					TFunction<void(TSharedPtr<FBulkFolderNode>)> Assign = [this, &Assign](TSharedPtr<FBulkFolderNode> Node)
					{
						for (auto& C : Node->Children)
						{
							if (!C->IsFolder() && TextureStates.IsValidIndex(C->TextureIndex))
							{
								// F13: store the assignment on the texture state only — it travels with the
								// state across removal/reindex (no parallel index map left to desync).
								TextureStates[C->TextureIndex]->SubfolderPath = C->GetPath();
							}
							if (C->Children.Num() > 0)
							{
								Assign(C);
							}
						}
					};
					for (auto& S : TextureStates) S->SubfolderPath.Empty();
					Assign(Root);
					this->bFolderIncludePrefix = *bDlgFolderIncPrefix;
					this->bFolderIncludeSuffix = *bDlgFolderIncSuffix;
					this->FolderCustomPrefix = *DlgFolderPrefix;
					this->FolderCustomSuffix = *DlgFolderSuffix;
					this->FolderFindStr = *DlgFolderFind;
					this->FolderReplaceStr = *DlgFolderReplace;
					this->FolderRemoveStr = *DlgFolderRemove;
					this->bFolderIncludeBatchSuffix = *bDlgFolderIncBatchSuffix;
					// Rebuild the persisted collapse paths LAST — SanitizeFolders above may have
					// rewritten names, which would otherwise leave every stored path stale.
					SyncPersistentCollapsedFolders();
					OrgWindow->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("CancelOrg", "Cancel"))
				.OnClicked_Lambda([OrgWindow, SyncPersistentCollapsedFolders]() -> FReply
				{
					// Folder edits are discarded, but the collapse state of surviving folders is a view
					// preference and is kept for the next open.
					SyncPersistentCollapsedFolders();
					OrgWindow->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
		]
	);

	RefreshTree();
	FSlateApplication::Get().AddModalWindow(OrgWindow, AsShared());
	return FReply::Handled();
}

// ============================================================================
// Export / Import of the folder organization
//
// The JSON shape and every parsing rule live in the Slate-free
// Paper2DPlus::BulkFolderOrganization helpers, so the round-trip is testable without a window.
// ============================================================================

FReply SBulkSpriteExtractorWindow::OnExportOrganizationClicked()
{
	using namespace Paper2DPlus::BulkFolderOrganization;

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	FBulkFolderOrganizationSnapshot Snapshot;
	Snapshot.Naming.OutputPath = OutputPathOverride;
	Snapshot.Naming.bIncludePrefixInFolder = bFolderIncludePrefix;
	Snapshot.Naming.bIncludeSuffixInFolder = bFolderIncludeSuffix;
	Snapshot.Naming.bIncludeBatchSuffix = bFolderIncludeBatchSuffix;
	Snapshot.Naming.FolderPrefix = FolderCustomPrefix;
	Snapshot.Naming.FolderSuffix = FolderCustomSuffix;
	Snapshot.Naming.Find = FolderFindStr;
	Snapshot.Naming.Replace = FolderReplaceStr;
	Snapshot.Naming.RemoveText = FolderRemoveStr;

	// Rebuild the nested tree from each state's SubfolderPath, exactly as the organizer modal does:
	// the stored path INCLUDES the texture's own leaf, so the folder is everything before the last
	// separator and a single-segment path means "no real folder" (Unassigned).
	TMap<FString, TSharedPtr<FBulkFolderRecord>> FoldersByPath;
	auto FindOrAddFolder = [&Snapshot, &FoldersByPath](const FString& FolderPath) -> TSharedPtr<FBulkFolderRecord>
	{
		TArray<FString> Segments;
		FolderPath.ParseIntoArray(Segments, TEXT("/"));
		TSharedPtr<FBulkFolderRecord> Current;
		FString Accumulated;
		for (const FString& Segment : Segments)
		{
			Accumulated = Accumulated.IsEmpty() ? Segment : (Accumulated / Segment);
			if (TSharedPtr<FBulkFolderRecord>* Existing = FoldersByPath.Find(Accumulated))
			{
				Current = *Existing;
				continue;
			}
			TSharedRef<FBulkFolderRecord> Created = MakeShared<FBulkFolderRecord>();
			Created->Name = Segment;
			if (Current.IsValid()) Current->Children.Add(Created);
			else Snapshot.Folders.Add(Created);
			FoldersByPath.Add(Accumulated, Created);
			Current = Created;
		}
		return Current;
	};

	for (const TSharedPtr<FBulkExtractorTextureState>& State : TextureStates)
	{
		if (!State.IsValid())
		{
			continue;
		}
		const FString Label = BulkSpriteExtractor_Internal::StateLabel(State);

		FBulkTextureRecord Record;
		Record.DisplayName = Label;
		Record.SubfolderPath = State->SubfolderPath;
		Record.AssetPath = State->Texture.ToSoftObjectPath().ToString();
		Snapshot.Textures.Add(MoveTemp(Record));

		FString FolderPath;
		FString LeafName;
		if (!State->SubfolderPath.IsEmpty()
			&& State->SubfolderPath.Split(TEXT("/"), &FolderPath, &LeafName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			if (const TSharedPtr<FBulkFolderRecord> Folder = FindOrAddFolder(FolderPath))
			{
				Folder->Items.Add(Label);
				continue;
			}
		}
		Snapshot.Unassigned.Add(Label);
	}

	TArray<FString> OutFilenames;
	const bool bPicked = DesktopPlatform->SaveFileDialog(
		nullptr,
		LOCTEXT("ExportOrgDialogTitle", "Export Folder Organization").ToString(),
		FPaths::ProjectDir(),
		TEXT("BulkFolderOrganization.json"),
		TEXT("JSON files (*.json)|*.json"),
		EFileDialogFlags::None,
		OutFilenames);
	if (!bPicked || OutFilenames.Num() == 0)
	{
		return FReply::Handled();
	}

	const FString Json = SerializeOrganizationSnapshot(Snapshot);
	if (!FFileHelper::SaveStringToFile(Json, *OutFilenames[0]))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("ExportOrgWriteFailed", "Could not write {0}."),
			FText::FromString(OutFilenames[0])));
		return FReply::Handled();
	}

	FNotificationInfo Info(FText::Format(
		LOCTEXT("ExportOrgDone", "Exported {0} texture(s) and {1} root folder(s) to {2}."),
		FText::AsNumber(Snapshot.Textures.Num()),
		FText::AsNumber(Snapshot.Folders.Num()),
		FText::FromString(FPaths::GetCleanFilename(OutFilenames[0]))));
	Info.ExpireDuration = 6.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
	return FReply::Handled();
}

FReply SBulkSpriteExtractorWindow::OnImportOrganizationClicked()
{
	using namespace Paper2DPlus::BulkFolderOrganization;

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	TArray<FString> OutFilenames;
	const bool bPicked = DesktopPlatform->OpenFileDialog(
		nullptr,
		LOCTEXT("ImportOrgDialogTitle", "Import Folder Organization").ToString(),
		FPaths::ProjectDir(),
		TEXT(""),
		TEXT("JSON files (*.json)|*.json"),
		EFileDialogFlags::None,
		OutFilenames);
	if (!bPicked || OutFilenames.Num() == 0)
	{
		return FReply::Handled();
	}

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *OutFilenames[0]))
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ImportOrgReadFailed", "The selected file could not be read."));
		return FReply::Handled();
	}

	FBulkFolderOrganizationSnapshot Snapshot;
	FString ParseError;
	if (!DeserializeOrganizationSnapshot(JsonText, Snapshot, ParseError))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("ImportOrgParseFailed", "Import failed: {0}"), FText::FromString(ParseError)));
		return FReply::Handled();
	}

	// Name -> folder path (no per-texture leaf). Duplicates are reported, never silently resolved.
	TMap<FString, FString> NameToFolder;
	TArray<FString> DuplicateNames;
	FlattenSnapshotAssignments(Snapshot, NameToFolder, DuplicateNames);

	// The per-texture rows carry the same assignment as the tree (and are the ONLY source when a
	// payload has rows but no tree), plus an asset path that makes matching survive a rename.
	TMap<FString, FString> PayloadAssetPathsByName;
	for (const FBulkTextureRecord& Record : Snapshot.Textures)
	{
		if (Record.DisplayName.IsEmpty())
		{
			continue;
		}
		if (!Record.AssetPath.IsEmpty())
		{
			PayloadAssetPathsByName.FindOrAdd(Record.DisplayName.ToLower()) = Record.AssetPath;
		}
		if (NameToFolder.Contains(Record.DisplayName))
		{
			continue;
		}
		FString FolderPath;
		FString LeafName;
		if (!Record.SubfolderPath.IsEmpty()
			&& Record.SubfolderPath.Split(TEXT("/"), &FolderPath, &LeafName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			NameToFolder.Add(Record.DisplayName, FolderPath);
		}
	}

	// The payload's rename map is oldName -> nameUsedInTheTree, so an import needs the INVERSE to go
	// from a tree entry back to a row that still carries the older name.
	TMap<FString, FString> TreeNameToOldName;
	for (const TPair<FString, FString>& Pair : Snapshot.Renames)
	{
		TreeNameToOldName.FindOrAdd(Pair.Value) = Pair.Key;
	}

	// Row lookup: by display label, then by source asset name, then by asset path.
	TMap<FString, TSharedPtr<FBulkExtractorTextureState>> RowsByLabel;
	TMap<FString, TSharedPtr<FBulkExtractorTextureState>> RowsByAssetPath;
	for (const TSharedPtr<FBulkExtractorTextureState>& State : TextureStates)
	{
		if (!State.IsValid()) continue;
		RowsByLabel.FindOrAdd(BulkSpriteExtractor_Internal::StateLabel(State).ToLower()) = State;
		if (!State->Texture.IsNull())
		{
			RowsByAssetPath.FindOrAdd(State->Texture.ToSoftObjectPath().ToString()) = State;
			// The source asset name is a legitimate fallback for a row already renamed in session,
			// but it must never shadow a real display-name match.
			const FString AssetNameKey = State->Texture.GetAssetName().ToLower();
			if (!RowsByLabel.Contains(AssetNameKey))
			{
				RowsByLabel.Add(AssetNameKey, State);
			}
		}
	}

	auto ResolveRow = [&RowsByLabel, &RowsByAssetPath, &PayloadAssetPathsByName, &TreeNameToOldName](
		const FString& PayloadName) -> TSharedPtr<FBulkExtractorTextureState>
	{
		if (const TSharedPtr<FBulkExtractorTextureState>* Direct = RowsByLabel.Find(PayloadName.ToLower()))
		{
			return *Direct;
		}
		if (const FString* AssetPath = PayloadAssetPathsByName.Find(PayloadName.ToLower()))
		{
			if (const TSharedPtr<FBulkExtractorTextureState>* ByPath = RowsByAssetPath.Find(*AssetPath))
			{
				return *ByPath;
			}
		}
		if (const FString* OldName = TreeNameToOldName.Find(PayloadName))
		{
			if (const TSharedPtr<FBulkExtractorTextureState>* Renamed = RowsByLabel.Find(OldName->ToLower()))
			{
				return *Renamed;
			}
		}
		return nullptr;
	};

	// ------------------------------------------------------------------
	// MATCH FIRST, COMMIT LAST. Import used to clear every row's SubfolderPath before it had matched
	// a single name, so picking the wrong file (a sibling batch's export parses perfectly well)
	// destroyed an existing organization: there is no undo for it, and the tree exists only in
	// memory. Nothing below touches the batch until the user has seen exactly what will happen.
	// ------------------------------------------------------------------
	TArray<TPair<TSharedPtr<FBulkExtractorTextureState>, FString>> StagedAssignments;
	TSet<const FBulkExtractorTextureState*> Placed;
	TArray<FString> UnmatchedPayloadNames;
	TArray<FString> AdjustedFolderNames;
	TArray<FString> RejectedFolderNames;
	TArray<FString> RejectedRows;
	int32 AssignedCount = 0;

	for (const TPair<FString, FString>& Entry : NameToFolder)
	{
		const TSharedPtr<FBulkExtractorTextureState> Row = ResolveRow(Entry.Key);
		if (!Row.IsValid())
		{
			UnmatchedPayloadNames.Add(Entry.Key);
			continue;
		}

		// Validate every imported segment against the SAME rule the inline rename and
		// MakeUniqueFolderName use. SanitizeAssetName replaces spaces and nothing else, so on its own
		// it let ':' '.' '[' ']' '&' '!' '~' '@' '#' and over-length names travel straight into
		// SubfolderPath — where they became an invalid long package name at CreatePackage time, i.e.
		// at the very end of a long batch, after every other row had already been written.
		TArray<FString> Segments;
		Entry.Value.ParseIntoArray(Segments, TEXT("/"));
		FString LegalFolder;
		bool bAllSegmentsLegal = true;
		for (const FString& Segment : Segments)
		{
			FString LegalSegment;
			if (!BulkFolderOrganizer_Internal::DeriveLegalFolderSegment(Segment, LegalSegment))
			{
				RejectedFolderNames.AddUnique(Segment);
				bAllSegmentsLegal = false;
				break;
			}
			if (!LegalSegment.Equals(Segment, ESearchCase::CaseSensitive))
			{
				AdjustedFolderNames.AddUnique(Segment);
			}
			LegalFolder = LegalFolder.IsEmpty() ? LegalSegment : (LegalFolder / LegalSegment);
		}

		// Fail closed on a name no repair could make legal: leave the row unassigned and name it,
		// rather than filing it under a path that cannot be created.
		if (!bAllSegmentsLegal)
		{
			RejectedRows.Add(BulkSpriteExtractor_Internal::StateLabel(Row));
			Placed.Add(Row.Get());
			continue;
		}

		// SubfolderPath carries the texture's own leaf — that is the contract every downstream
		// consumer (BuildOutputPath, the organizer reconstruction, group auto-create) assumes.
		const FString Leaf = BulkSpriteExtractor_Internal::StateLabel(Row);
		const FString StagedPath = LegalFolder.IsEmpty() ? FString() : (LegalFolder / Leaf);
		StagedAssignments.Emplace(Row, StagedPath);
		if (!StagedPath.IsEmpty())
		{
			++AssignedCount;
		}
		Placed.Add(Row.Get());
	}

	// A payload can name a row as EXPLICITLY unassigned. The clear below already satisfies it, but it
	// counts as "the file covered this row" so it must not be reported as missing from the file.
	for (const FString& Name : Snapshot.Unassigned)
	{
		if (const TSharedPtr<FBulkExtractorTextureState> Row = ResolveRow(Name))
		{
			Placed.Add(Row.Get());
		}
		else
		{
			UnmatchedPayloadNames.AddUnique(Name);
		}
	}

	TArray<FString> UnplacedRows;
	for (const TSharedPtr<FBulkExtractorTextureState>& State : TextureStates)
	{
		if (State.IsValid() && !Placed.Contains(State.Get()))
		{
			UnplacedRows.Add(BulkSpriteExtractor_Internal::StateLabel(State));
		}
	}

	// Nothing in the file names anything in this batch — the wrong-file signature. Decline instead of
	// wiping the current organization for a payload that cannot replace it.
	if (Placed.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("ImportOrgNoMatches",
				"Nothing was changed: none of the {0} name(s) in this file match a texture in this batch."),
			FText::AsNumber(UnmatchedPayloadNames.Num())));
		return FReply::Handled();
	}

	// ONE summary covering every outcome, shown BEFORE anything is applied. The payload format
	// identifies textures by name only, so a lossy match is expected and must be visible.
	FString Summary = FString::Printf(
		TEXT("Apply this folder organization?\n\n%d texture(s) will be filed."), AssignedCount);
	if (UnplacedRows.Num() > 0)
	{
		Summary += FString::Printf(TEXT("\n\n%d row(s) are not in the file and will be left unassigned: %s."),
			UnplacedRows.Num(), *BulkSpriteExtractor_Internal::JoinLabels(UnplacedRows, 12));
	}
	if (RejectedRows.Num() > 0)
	{
		Summary += FString::Printf(
			TEXT("\n\n%d row(s) will be left unassigned because their folder name in the file is not a legal package path (%s): %s."),
			RejectedRows.Num(),
			*BulkSpriteExtractor_Internal::JoinLabels(RejectedFolderNames, 12),
			*BulkSpriteExtractor_Internal::JoinLabels(RejectedRows, 12));
	}
	if (UnmatchedPayloadNames.Num() > 0)
	{
		Summary += FString::Printf(TEXT("\n\n%d name(s) in the file match no texture in this batch: %s."),
			UnmatchedPayloadNames.Num(), *BulkSpriteExtractor_Internal::JoinLabels(UnmatchedPayloadNames, 12));
	}
	if (DuplicateNames.Num() > 0)
	{
		Summary += FString::Printf(TEXT("\n\n%d name(s) appear in more than one folder; the first occurrence won: %s."),
			DuplicateNames.Num(), *BulkSpriteExtractor_Internal::JoinLabels(DuplicateNames, 12));
	}
	if (AdjustedFolderNames.Num() > 0)
	{
		Summary += FString::Printf(TEXT("\n\n%d folder name(s) will be adjusted to be valid package paths: %s."),
			AdjustedFolderNames.Num(), *BulkSpriteExtractor_Internal::JoinLabels(AdjustedFolderNames, 12));
	}
	if (Snapshot.Naming.bPresentInPayload)
	{
		Summary += TEXT("\n\nThe file's folder-naming settings will replace the current ones.");
	}
	Summary += TEXT("\n\nThis replaces the current organization for every row in the batch and cannot be undone.");

	if (FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(Summary)) != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	// ---- Commit. Clear every assignment first, exactly as the organizer's Apply does, so a partial
	// import cannot leave half the batch on a previous organization. ----
	for (const TSharedPtr<FBulkExtractorTextureState>& State : TextureStates)
	{
		if (State.IsValid()) State->SubfolderPath.Reset();
	}
	for (const TPair<TSharedPtr<FBulkExtractorTextureState>, FString>& Staged : StagedAssignments)
	{
		if (Staged.Key.IsValid()) Staged.Key->SubfolderPath = Staged.Value;
	}

	// Naming settings apply ONLY when the payload actually carried a settings block. The supported
	// hand-captured `{"tree": {...}}` shape has none, and applying struct defaults for it silently
	// reset the user's prefix/suffix/find/replace configuration. createSubfolders is deliberately
	// absent from this window now, so a payload carrying it is simply ignored by the reader.
	if (Snapshot.Naming.bPresentInPayload)
	{
		if (!Snapshot.Naming.OutputPath.IsEmpty())
		{
			OutputPathOverride = Snapshot.Naming.OutputPath;
		}
		bFolderIncludePrefix = Snapshot.Naming.bIncludePrefixInFolder;
		bFolderIncludeSuffix = Snapshot.Naming.bIncludeSuffixInFolder;
		bFolderIncludeBatchSuffix = Snapshot.Naming.bIncludeBatchSuffix;
		FolderCustomPrefix = Snapshot.Naming.FolderPrefix;
		FolderCustomSuffix = Snapshot.Naming.FolderSuffix;
		FolderFindStr = Snapshot.Naming.Find;
		FolderReplaceStr = Snapshot.Naming.Replace;
		FolderRemoveStr = Snapshot.Naming.RemoveText;
	}

	RefreshFilteredTextureList();

	FNotificationInfo Info(FText::Format(
		LOCTEXT("ImportOrgDone", "Imported folder organization: {0} texture(s) filed."),
		FText::AsNumber(AssignedCount)));
	Info.ExpireDuration = 6.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
	return FReply::Handled();
}

// ============================================================================
// De-bake groups (variant sheets → recovered base + overlays, FVariantDebake)
// ============================================================================

namespace BulkDebake_Internal
{
	/** Toast helper (headline error/info reporting; details go to the Output Log). */
	void Toast(const FText& Message, float ExpireSeconds = 6.0f)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = ExpireSeconds;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	void ToastError(const FText& Message, float ExpireSeconds = 6.0f)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = ExpireSeconds;
		Info.bUseSuccessFailIcons = true;
		if (const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Notification->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}

	/** Deterministic per-group chip color hashed from the name (saturated, readable on dark UI). */
	FLinearColor GroupColorFromName(const FString& Name)
	{
		const uint32 Hash = GetTypeHash(Name);
		return FLinearColor::MakeFromHSV8((uint8)(Hash % 256), 170, 230);
	}

	/** The member main-names, in member order (dead members yield their last-known display name). */
	TArray<FString> MemberNames(const FDebakeGroupState& Group)
	{
		TArray<FString> Names;
		Names.Reserve(Group.Members.Num());
		for (const FDebakeGroupMember& Member : Group.Members)
		{
			const TSharedPtr<FBulkExtractorTextureState> State = Member.State.Pin();
			Names.Add(State.IsValid() ? State->Texture.GetAssetName() : FString());
		}
		return Names;
	}
}

FReply SBulkSpriteExtractorWindow::OnCreateDebakeGroupClicked()
{
	ON_SCOPE_EXIT { OpenDebakeWindow(); };
	if (!TextureListView.IsValid())
	{
		return FReply::Handled();
	}

	// Preserve the list order (selection order is click order).
	TArray<TSharedPtr<FBulkExtractorTextureState>> Selected = TextureListView->GetSelectedItems();
	Selected.Sort([this](const TSharedPtr<FBulkExtractorTextureState>& A, const TSharedPtr<FBulkExtractorTextureState>& B)
	{
		return TextureStates.IndexOfByKey(A) < TextureStates.IndexOfByKey(B);
	});

	CreateDebakeGroupFromStates(Selected);
	return FReply::Handled();
}

TSharedPtr<FDebakeGroupState> SBulkSpriteExtractorWindow::CreateDebakeGroupFromStates(
	const TArray<TSharedPtr<FBulkExtractorTextureState>>& States)
{
	TArray<TSharedPtr<FBulkExtractorTextureState>> Valid;
	int32 AlreadyGrouped = 0;
	for (const TSharedPtr<FBulkExtractorTextureState>& State : States)
	{
		if (!State.IsValid() || State->Texture.IsNull())
		{
			continue;
		}
		if (FindGroupContaining(State).IsValid())
		{
			AlreadyGrouped++;
			continue;
		}
		Valid.Add(State);
	}
	if (AlreadyGrouped > 0)
	{
		BulkDebake_Internal::Toast(FText::Format(
			LOCTEXT("DebakeAlreadyGrouped", "{0} texture(s) skipped \x2014 already in a de-bake group."),
			FText::AsNumber(AlreadyGrouped)));
	}
	if (Valid.Num() < 2)
	{
		BulkDebake_Internal::ToastError(LOCTEXT("DebakeNeedTwo", "Select at least 2 ungrouped variant sheets to create a de-bake group."));
		return nullptr;
	}

	TSharedPtr<FDebakeGroupState> Group = MakeShared<FDebakeGroupState>();

	// DELIBERATE: the group name is derived from the SOURCE ASSET names, not from the editable
	// display names. AcceptDebakeGroup writes real packages from this name, so it must not depend on
	// session-local renames.
	TArray<FString> Names;
	for (const TSharedPtr<FBulkExtractorTextureState>& State : Valid)
	{
		Names.Add(State->Texture.GetAssetName());
		FDebakeGroupMember Member;
		Member.State = State;
		Group->Members.Add(Member);
	}

	// Name: common prefix of the mains, uniquified against existing groups.
	FString BaseName = FBulkDebakeUtils::CommonPrefix(Names);
	FString GroupName = BaseName;
	int32 Suffix = 2;
	while (DebakeGroups.ContainsByPredicate([&GroupName](const TSharedPtr<FDebakeGroupState>& G)
		{ return G.IsValid() && G->GroupName == GroupName; }))
	{
		GroupName = FString::Printf(TEXT("%s_%d"), *BaseName, Suffix++);
	}
	Group->GroupName = GroupName;
	Group->GroupColor = BulkDebake_Internal::GroupColorFromName(GroupName);

	// Grid prefill: the first member's confirmed/inferred grid when it has one, else the
	// square-cells heuristic (columns = W/H when integral, 1 row).
	const TSharedPtr<FBulkExtractorTextureState>& First = Valid[0];
	const FIntPoint EffectiveGrid = First->GetEffectiveGrid();
	if (First->bDetectionRun && EffectiveGrid.X > 0 && EffectiveGrid.Y > 0)
	{
		Group->GridColumns = EffectiveGrid.X;
		Group->GridRows = EffectiveGrid.Y;
	}
	else if (UTexture2D* FirstTex = First->Texture.LoadSynchronous())
	{
		const FIntPoint Dims = FSpriteExtractionUtils::GetDetectionDimensions(FirstTex);
		if (Dims.X > 0 && Dims.Y > 0 && Dims.X % Dims.Y == 0)
		{
			Group->GridColumns = Dims.X / Dims.Y;
			Group->GridRows = 1;
		}
	}

	PrefillGroupVfx(Group);

	DebakeGroups.Add(Group);
	SelectDebakeGroup(Group);
	if (TextureListView.IsValid())
	{
		TextureListView->RequestListRefresh();
	}
	return Group;
}

void SBulkSpriteExtractorWindow::AddDebakeGroupFromTextures(const TArray<TSoftObjectPtr<UTexture2D>>& VariantSheets)
{
	TArray<TSharedPtr<FBulkExtractorTextureState>> GroupStates;
	bool bAddedAny = false;
	for (const TSoftObjectPtr<UTexture2D>& SoftTex : VariantSheets)
	{
		if (SoftTex.IsNull())
		{
			continue;
		}
		TSharedPtr<FBulkExtractorTextureState> Existing;
		for (const TSharedPtr<FBulkExtractorTextureState>& State : TextureStates)
		{
			if (State.IsValid() && State->Texture.ToSoftObjectPath() == SoftTex.ToSoftObjectPath())
			{
				Existing = State;
				break;
			}
		}
		if (!Existing.IsValid())
		{
			Existing = MakeShared<FBulkExtractorTextureState>();
			Existing->Texture = SoftTex;
			Existing->Status = EBulkExtractorTextureStatus::Pending;
			if (UTexture2D* Tex = SoftTex.LoadSynchronous())
			{
				Existing->DisplayName = Tex->GetName();
			}
			TextureStates.Add(Existing);
			bAddedAny = true;
		}
		GroupStates.Add(Existing);
	}
	if (bAddedAny)
	{
		// New rows change the extraction gate as well as the pad count.
		InvalidateDerivedCounts();
		RefreshFilteredTextureList();
	}
	CreateDebakeGroupFromStates(GroupStates);
}

void SBulkSpriteExtractorWindow::SelectDebakeGroup(TSharedPtr<FDebakeGroupState> Group)
{
	SelectedDebakeGroup = Group;
	if (Group.IsValid())
	{
		DebakePreviewVariantIndex = FMath::Clamp(DebakePreviewVariantIndex, 0,
			FMath::Max(0, Group->CachedResult.Overlays.Num() - 1));
		bShowDebakePreview = (Group->Status == EDebakeGroupStatus::Previewed || Group->Status == EDebakeGroupStatus::Accepted)
			&& Group->BasePreviewTex.IsValid();
	}
	else
	{
		bShowDebakePreview = false;
	}
	RefreshDebakeSection();
	UpdateCanvasForDebakePreview();
}

void SBulkSpriteExtractorWindow::RemoveDebakeGroup(TSharedPtr<FDebakeGroupState> Group)
{
	if (!Group.IsValid())
	{
		return;
	}
	if (Group->Status == EDebakeGroupStatus::Accepted)
	{
		// Removing an accepted group first restores its members (assets stay on disk).
		RevertDebakeGroup(Group);
	}
	DebakeGroups.Remove(Group);
	if (SelectedDebakeGroup == Group)
	{
		SelectDebakeGroup(DebakeGroups.Num() > 0 ? DebakeGroups.Last() : nullptr);
	}
	else
	{
		RefreshDebakeSection();
	}
	if (TextureListView.IsValid())
	{
		TextureListView->RequestListRefresh();
	}
}

TSharedPtr<FDebakeGroupState> SBulkSpriteExtractorWindow::FindGroupContaining(
	const TSharedPtr<FBulkExtractorTextureState>& State) const
{
	if (!State.IsValid())
	{
		return nullptr;
	}
	for (const TSharedPtr<FDebakeGroupState>& Group : DebakeGroups)
	{
		if (!Group.IsValid())
		{
			continue;
		}
		for (const FDebakeGroupMember& Member : Group->Members)
		{
			if (Member.State.Pin() == State)
			{
				return Group;
			}
		}
	}
	return nullptr;
}

void SBulkSpriteExtractorWindow::PrefillGroupVfx(TSharedPtr<FDebakeGroupState> Group)
{
	if (!Group.IsValid() || Group->Members.Num() == 0)
	{
		return;
	}

	const FString ScanPath = ResolveGroupOutputPath(Group);
	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> FolderAssets;
	AssetRegistry.Get().GetAssetsByPath(FName(*ScanPath), FolderAssets, /*bRecursive=*/true);

	TSet<FSoftObjectPath> MainPaths;
	for (const FDebakeGroupMember& Member : Group->Members)
	{
		if (const TSharedPtr<FBulkExtractorTextureState> State = Member.State.Pin())
		{
			MainPaths.Add(State->Texture.ToSoftObjectPath());
		}
	}

	TArray<FBulkDebakeUtils::FVfxPrefillCandidate> Candidates;
	for (const FAssetData& Asset : FolderAssets)
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
		if (Asset.AssetClassPath != UTexture2D::StaticClass()->GetClassPathName())
#else
		// FAssetData::AssetClassPath + UClass::GetClassPathName are 5.1+ — 5.0 compares the FName form.
		if (Asset.AssetClass != UTexture2D::StaticClass()->GetFName())
#endif
		{
			continue;
		}
		if (MainPaths.Contains(Asset.ToSoftObjectPath()))
		{
			continue;
		}
		const FString Name = Asset.AssetName.ToString();
		if (!Name.Contains(TEXT("vfx"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		FBulkDebakeUtils::FVfxPrefillCandidate Candidate;
		Candidate.Path = Asset.ToSoftObjectPath();
		Candidate.NameCore = FBulkDebakeUtils::StripSandwichTokens(FBulkDebakeUtils::NormalizeName(Name));
		Candidate.bBack = Name.Contains(TEXT("back"), ESearchCase::IgnoreCase);
		if (!Candidate.NameCore.IsEmpty())
		{
			Candidates.Add(Candidate);
		}
	}

	TArray<FSoftObjectPath> Front, Back;
	FBulkDebakeUtils::PrefillVfxAssignments(BulkDebake_Internal::MemberNames(*Group), Candidates, Front, Back);
	for (int32 i = 0; i < Group->Members.Num(); ++i)
	{
		if (!Front[i].IsNull())
		{
			Group->Members[i].VfxFront = TSoftObjectPtr<UTexture2D>(Front[i]);
		}
		if (!Back[i].IsNull())
		{
			Group->Members[i].VfxBack = TSoftObjectPtr<UTexture2D>(Back[i]);
		}
	}
}

void SBulkSpriteExtractorWindow::MarkGroupStale(TSharedPtr<FDebakeGroupState> Group)
{
	if (!Group.IsValid()
		|| (Group->Status != EDebakeGroupStatus::Previewed && Group->Status != EDebakeGroupStatus::Accepted))
	{
		return;
	}
	// The stale preview keeps displaying (amber note); Preview/Accept recompute.
	Group->bPreviewStale = true;
	if (Group->Status == EDebakeGroupStatus::Accepted)
	{
		// The accepted outputs no longer match the edited settings — pull them back out of the
		// pipeline, or Extract All stays enabled and commits the STALE textures/grid. Deferred:
		// the callers are value/commit lambdas hosted inside the group editor the revert rebuilds.
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda(
			[this, WeakGroup = TWeakPtr<FDebakeGroupState>(Group)](double, float)
			{
				const TSharedPtr<FDebakeGroupState> Pinned = WeakGroup.Pin();
				if (Pinned.IsValid() && Pinned->Status == EDebakeGroupStatus::Accepted)
				{
					RevertDebakeGroup(Pinned);
					Pinned->bPreviewStale = true;
				}
				return EActiveTimerReturnType::Stop;
			}));
	}
}

int32 SBulkSpriteExtractorWindow::CompactGroupMembers(TSharedPtr<FDebakeGroupState> Group)
{
	if (!Group.IsValid())
	{
		return 0;
	}
	Group->Members.RemoveAll([](const FDebakeGroupMember& Member)
	{
		return !Member.State.IsValid();
	});
	Group->ReferenceIndex = FMath::Clamp(Group->ReferenceIndex, 0, FMath::Max(0, Group->Members.Num() - 1));
	DebakePreviewVariantIndex = FMath::Clamp(DebakePreviewVariantIndex, 0, FMath::Max(0, Group->Members.Num() - 1));
	return Group->Members.Num();
}

FString SBulkSpriteExtractorWindow::ResolveGroupOutputPath(const TSharedPtr<FDebakeGroupState>& Group) const
{
	if (!Group.IsValid())
	{
		return TEXT("/Game");
	}
	if (!Group->OutputPathOverride.IsEmpty())
	{
		return Group->OutputPathOverride;
	}
	for (const FDebakeGroupMember& Member : Group->Members)
	{
		if (const TSharedPtr<FBulkExtractorTextureState> State = Member.State.Pin())
		{
			if (!State->Texture.IsNull())
			{
				return FPackageName::GetLongPackagePath(State->Texture.ToSoftObjectPath().GetLongPackageName());
			}
		}
	}
	return TEXT("/Game");
}

bool SBulkSpriteExtractorWindow::PreviewDebakeGroup(TSharedPtr<FDebakeGroupState> Group)
{
	if (!Group.IsValid())
	{
		return false;
	}

	// Re-validate membership at run time — rows may have been removed since the group was built.
	const int32 N = CompactGroupMembers(Group);
	if (N < 2)
	{
		Group->Status = EDebakeGroupStatus::Error;
		Group->LastError = LOCTEXT("DebakeTooFewMembers", "De-bake needs at least 2 variant sheets (members were removed).");
		BulkDebake_Internal::ToastError(Group->LastError);
		RefreshDebakeSection();
		return false;
	}

	FScopedSlowTask SlowTask((float)(N + 1), LOCTEXT("DebakePreviewProgress", "Computing de-bake preview..."));
	SlowTask.MakeDialog(/*bShowCancelButton=*/true);

	TArray<FDebakeSheetInput> Sheets;
	Sheets.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		if (SlowTask.ShouldCancel())
		{
			BulkDebake_Internal::Toast(LOCTEXT("DebakePreviewCancelled", "De-bake preview cancelled. The group was not changed."));
			return false;
		}
		SlowTask.EnterProgressFrame(1.0f);
		const TSharedPtr<FBulkExtractorTextureState> State = Group->Members[i].State.Pin();
		UTexture2D* Main = State.IsValid() ? State->Texture.LoadSynchronous() : nullptr;
		int32 DiscardH = 0;
		if (!FBulkDebakeUtils::LoadPixels(Main, Sheets[i].Pixels, Sheets[i].Width, Sheets[i].Height))
		{
			Group->Status = EDebakeGroupStatus::Error;
			Group->LastError = FText::Format(LOCTEXT("DebakeLoadMainFailed", "Failed to read pixel data from '{0}'."),
				FText::FromString(State.IsValid() ? BulkSpriteExtractor_Internal::StateLabel(State) : TEXT("?")));
			BulkDebake_Internal::ToastError(Group->LastError);
			RefreshDebakeSection();
			return false;
		}
		// A configured-but-unloadable VFX mask ABORTS, never silently degrades to a maskless vote —
		// that is exactly the mis-cut the masks exist to prevent (stale soft-ref after rename/delete).
		if (!Group->Members[i].VfxFront.IsNull())
		{
			UTexture2D* FrontTex = Group->Members[i].VfxFront.LoadSynchronous();
			if (!FrontTex || !FBulkDebakeUtils::LoadPixels(FrontTex, Sheets[i].VfxFront, Sheets[i].VfxFrontWidth, DiscardH))
			{
				Group->Status = EDebakeGroupStatus::Error;
				Group->LastError = FText::Format(LOCTEXT("DebakeLoadVfxFailed", "Failed to load VFX sheet '{0}' \x2014 fix or clear the picker and re-preview."),
					FText::FromString(Group->Members[i].VfxFront.ToSoftObjectPath().ToString()));
				BulkDebake_Internal::ToastError(Group->LastError);
				RefreshDebakeSection();
				return false;
			}
		}
		if (!Group->Members[i].VfxBack.IsNull())
		{
			UTexture2D* BackTex = Group->Members[i].VfxBack.LoadSynchronous();
			if (!BackTex || !FBulkDebakeUtils::LoadPixels(BackTex, Sheets[i].VfxBack, Sheets[i].VfxBackWidth, DiscardH))
			{
				Group->Status = EDebakeGroupStatus::Error;
				Group->LastError = FText::Format(LOCTEXT("DebakeLoadVfxBackFailed", "Failed to load VFX sheet '{0}' \x2014 fix or clear the picker and re-preview."),
					FText::FromString(Group->Members[i].VfxBack.ToSoftObjectPath().ToString()));
				BulkDebake_Internal::ToastError(Group->LastError);
				RefreshDebakeSection();
				return false;
			}
		}
	}

	FDebakeSettings Settings;
	Settings.Columns = Group->GridColumns;
	Settings.Rows = Group->GridRows;
	Settings.ReferenceSheetIndex = Group->ReferenceIndex;
	Settings.VfxOffsetOverride = Group->bOverrideVfxOffset ? Group->VfxOffsetOverrideValue : INDEX_NONE;

	if (SlowTask.ShouldCancel())
	{
		BulkDebake_Internal::Toast(LOCTEXT("DebakePreviewCancelledBeforeRun", "De-bake preview cancelled. The group was not changed."));
		return false;
	}
	SlowTask.EnterProgressFrame(1.0f, LOCTEXT("DebakePreviewRunning", "Recovering shared base..."));

	FDebakeResult Result;
	FText Error;
	if (!FVariantDebake::Run(Sheets, Settings, Result, Error))
	{
		Group->Status = EDebakeGroupStatus::Error;
		Group->LastError = Error;
		BulkDebake_Internal::ToastError(Error, 8.0f);
		RefreshDebakeSection();
		return false;
	}

	Group->CachedResult = MoveTemp(Result);
	Group->PreviewW = Sheets[0].Width;
	Group->PreviewH = Sheets[0].Height;
	Group->PreviewMemberNames = BulkDebake_Internal::MemberNames(*Group);
	Group->bPreviewStale = false;
	Group->LastError = FText::GetEmpty();
	Group->Status = (Group->Status == EDebakeGroupStatus::Accepted) ? EDebakeGroupStatus::Accepted : EDebakeGroupStatus::Previewed;

	// Base texture builds eagerly (the default view); overlay/composite build on first view.
	Group->InvalidatePreviewTextures();
	Group->BasePreviewTex = TStrongObjectPtr<UTexture2D>(
		FBulkDebakeUtils::CreateTransientPreviewTexture(Group->PreviewW, Group->PreviewH, Group->CachedResult.BasePixels));
	Group->OverlayPreviewTex.SetNum(N);
	Group->CompositePreviewTex.SetNum(N);

	DebakePreviewVariantIndex = FMath::Clamp(DebakePreviewVariantIndex, 0, N - 1);
	bShowDebakePreview = true;
	RefreshDebakeSection();
	UpdateCanvasForDebakePreview();
	return true;
}

bool SBulkSpriteExtractorWindow::IsDebakePreviewActive() const
{
	return bShowDebakePreview
		&& SelectedDebakeGroup.IsValid()
		&& (SelectedDebakeGroup->Status == EDebakeGroupStatus::Previewed
			|| SelectedDebakeGroup->Status == EDebakeGroupStatus::Accepted)
		&& SelectedDebakeGroup->BasePreviewTex.IsValid();
}

UTexture2D* SBulkSpriteExtractorWindow::GetOrBuildDebakePreviewTexture()
{
	if (!IsDebakePreviewActive())
	{
		return nullptr;
	}
	TSharedPtr<FDebakeGroupState> Group = SelectedDebakeGroup;
	const int32 N = Group->CachedResult.Overlays.Num();
	if (DebakePreviewView == EDebakePreviewView::Base || N == 0)
	{
		return Group->BasePreviewTex.Get();
	}

	const int32 V = FMath::Clamp(DebakePreviewVariantIndex, 0, N - 1);
	if (Group->OverlayPreviewTex.Num() != N)
	{
		Group->OverlayPreviewTex.SetNum(N);
		Group->CompositePreviewTex.SetNum(N);
	}

	if (DebakePreviewView == EDebakePreviewView::Overlay)
	{
		if (!Group->OverlayPreviewTex[V].IsValid())
		{
			Group->OverlayPreviewTex[V] = TStrongObjectPtr<UTexture2D>(FBulkDebakeUtils::CreateTransientPreviewTexture(
				Group->PreviewW, Group->PreviewH, Group->CachedResult.Overlays[V]));
		}
		UTexture2D* Overlay = Group->OverlayPreviewTex[V].Get();
		return Overlay ? Overlay : Group->BasePreviewTex.Get();
	}

	// Composite = overlay OVER base.
	if (!Group->CompositePreviewTex[V].IsValid())
	{
		TArray<FColor> Composite;
		if (FBulkDebakeUtils::BuildCompositeBuffer(Group->CachedResult.BasePixels, Group->CachedResult.Overlays[V], Composite))
		{
			Group->CompositePreviewTex[V] = TStrongObjectPtr<UTexture2D>(FBulkDebakeUtils::CreateTransientPreviewTexture(
				Group->PreviewW, Group->PreviewH, Composite));
		}
	}
	UTexture2D* CompositeTex = Group->CompositePreviewTex[V].Get();
	return CompositeTex ? CompositeTex : Group->BasePreviewTex.Get();
}

void SBulkSpriteExtractorWindow::UpdateCanvasForDebakePreview()
{
	// The de-bake preview belongs to the DE-BAKE window's own canvas. It used to be pushed into the
	// extractor's center canvas, so selecting a group silently replaced the texture the rest of the
	// window was reasoning about (grid overlay, cell size, detection readout) with a preview image.
	if (DebakePreviewCanvas.IsValid())
	{
		if (IsDebakePreviewActive())
		{
			if (UTexture2D* Preview = GetOrBuildDebakePreviewTexture())
			{
				DebakePreviewCanvas->SetTexture(Preview);
				DebakePreviewCanvas->SetDetectedSprites(TArray<FDetectedSprite>());
			}
		}
		else
		{
			DebakePreviewCanvas->SetTexture(nullptr);
			DebakePreviewCanvas->SetDetectedSprites(TArray<FDetectedSprite>());
		}
		DebakePreviewCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	}

	// The extractor's canvas always shows the selected TEXTURE now — nothing else may repurpose it.
	if (CenterCanvas.IsValid() && SelectedTexture.IsValid())
	{
		CenterCanvas->SetTexture(SelectedTexture->Texture.LoadSynchronous());
		CenterCanvas->SetDetectedSprites(SelectedTexture->DetectedSprites);
		CenterCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

int32 SBulkSpriteExtractorWindow::CountUngroupedSelectedTextures() const
{
	if (!TextureListView.IsValid())
	{
		return 0;
	}
	int32 Ungrouped = 0;
	for (const TSharedPtr<FBulkExtractorTextureState>& Item : TextureListView->GetSelectedItems())
	{
		if (Item.IsValid() && !FindGroupContaining(Item).IsValid())
		{
			++Ungrouped;
		}
	}
	return Ungrouped;
}

UPaper2DPlusCharacterCatalogAsset* SBulkSpriteExtractorWindow::ResolveTargetCatalog() const
{
	const UPaper2DPlusSettings* Settings = GetDefault<UPaper2DPlusSettings>();
	if (!Settings || Settings->DefaultCharacterCatalog.IsNull())
	{
		return nullptr;
	}
	// Deliberate synchronous load: this only ever runs from an explicit click, never from paint.
	return Settings->DefaultCharacterCatalog.LoadSynchronous();
}

bool SBulkSpriteExtractorWindow::CanAddToCatalog() const
{
	if (!bLinkToProfile || TargetProfile.IsNull())
	{
		return false;
	}
	const UPaper2DPlusCharacterCatalogAsset* Catalog = ResolveTargetCatalog();
	if (!Catalog)
	{
		return false;
	}
	// Already present is not an error, but it is not an action either.
	const FSoftObjectPath ProfilePath = TargetProfile.ToSoftObjectPath();
	for (const FPaper2DPlusCharacterCatalogEntry& Entry : Catalog->Entries)
	{
		if (Entry.CharacterProfile.ToSoftObjectPath() == ProfilePath)
		{
			return false;
		}
	}
	return true;
}

FReply SBulkSpriteExtractorWindow::OnAddToCatalogClicked()
{
	UPaper2DPlusCharacterCatalogAsset* Catalog = ResolveTargetCatalog();
	if (!Catalog)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("AddToCatalogNoDefault",
			"No default Character Catalog is configured.\n\nSet one in Project Settings > Plugins > Paper2DPlus > Character Catalog, then try again."));
		return FReply::Handled();
	}
	if (!bLinkToProfile || TargetProfile.IsNull())
	{
		return FReply::Handled();
	}

	const FSoftObjectPath ProfilePath = TargetProfile.ToSoftObjectPath();
	for (const FPaper2DPlusCharacterCatalogEntry& Entry : Catalog->Entries)
	{
		if (Entry.CharacterProfile.ToSoftObjectPath() == ProfilePath)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
				LOCTEXT("AddToCatalogAlreadyPresent", "{0} is already in {1}."),
				FText::FromString(TargetProfile.GetAssetName()),
				FText::FromString(Catalog->GetName())));
			return FReply::Handled();
		}
	}

	// Mirrors FCharacterCatalogEditorModel::AddCharacters: one transaction, append the entry, dirty
	// the package. Saving stays the user's call, as everywhere else in this window.
	{
		FScopedTransaction Transaction(LOCTEXT("AddCatalogCharacters", "Add Characters to Catalog"));
		Catalog->Modify();
		FPaper2DPlusCharacterCatalogEntry& Entry = Catalog->Entries.AddDefaulted_GetRef();
		Entry.CharacterProfile = TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>(ProfilePath);
		Catalog->MarkPackageDirty();
	}

	FNotificationInfo Info(FText::Format(
		LOCTEXT("AddToCatalogDone", "Added {0} to {1}."),
		FText::FromString(TargetProfile.GetAssetName()),
		FText::FromString(Catalog->GetName())));
	Info.ExpireDuration = 4.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
	return FReply::Handled();
}

void SBulkSpriteExtractorWindow::OpenDebakeWindow()
{
	// Reuse the live window — a de-bake session carries preview state worth keeping.
	if (const TSharedPtr<SWindow> Existing = DebakeWindowPtr.Pin())
	{
		Existing->BringToFront();
		RefreshDebakeSection();
		UpdateCanvasForDebakePreview();
		return;
	}

	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("DebakeWindowTitle", "De-bake Shared Base"))
		.ClientSize(FVector2D(1180.0f, 760.0f))
		.SupportsMaximize(true)
		.SupportsMinimize(true);

	Window->SetContent(
		SNew(SHorizontalBox)
		// Groups + editor
		+ SHorizontalBox::Slot().FillWidth(0.42f).Padding(6)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				BuildDebakeGroupsSection()
			]
		]
		// This window's OWN preview canvas — clipped, per the mandatory canvas rule.
		+ SHorizontalBox::Slot().FillWidth(0.58f).Padding(6)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() -> FText
				{
					if (!IsDebakePreviewActive())
					{
						return LOCTEXT("DebakeNoPreview", "Select a group and press Preview.");
					}
					return FText::Format(
						LOCTEXT("DebakeWindowPreviewTitle", "{0}{1}"),
						FText::FromString(SelectedDebakeGroup->GroupName),
						SelectedDebakeGroup->bPreviewStale
							? LOCTEXT("DebakePreviewStaleSuffix", "  (settings changed - re-preview)")
							: FText::GetEmpty());
				})
				.ColorAndOpacity_Lambda([this]()
				{
					return (IsDebakePreviewActive() && SelectedDebakeGroup->bPreviewStale)
						? FSlateColor(FLinearColor(1.0f, 0.75f, 0.2f))
						: FSlateColor::UseForeground();
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.Clipping(EWidgetClipping::ClipToBounds)   // canvases pan/zoom; see the canvas rule
				[
					SAssignNew(DebakePreviewCanvas, SSpriteExtractorCanvas)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
			[
				BuildDebakeCanvasStrip()
			]
		]);

	Window->GetOnWindowClosedEvent().AddLambda([this](const TSharedRef<SWindow>&)
	{
		// Leaving the window must not leave the extractor believing a preview is on screen.
		bShowDebakePreview = false;
		DebakePreviewCanvas.Reset();
		DebakeWindowPtr.Reset();
		UpdateCanvasForDebakePreview();
	});

	DebakeWindowPtr = Window;
	FSlateApplication::Get().AddWindow(Window);
	RefreshDebakeSection();
	UpdateCanvasForDebakePreview();
}

bool SBulkSpriteExtractorWindow::AcceptDebakeGroup(TSharedPtr<FDebakeGroupState> Group)
{
	if (!Group.IsValid())
	{
		return false;
	}

	// Re-accept keeps the currently accepted batch intact until every replacement texture has
	// materialized. Settings changes schedule an immediate revert, so reject the tiny stale window.
	const bool bReplacingAccepted = Group->Status == EDebakeGroupStatus::Accepted;
	if (bReplacingAccepted && Group->bPreviewStale)
	{
		BulkDebake_Internal::ToastError(LOCTEXT(
			"DebakeReacceptStale", "The de-bake settings changed. Wait for the group to revert, then preview and accept again."));
		return false;
	}

	// Never accept a stale/missing result.
	if (!bReplacingAccepted && (Group->Status != EDebakeGroupStatus::Previewed || Group->bPreviewStale))
	{
		if (!PreviewDebakeGroup(Group))
		{
			return false;
		}
	}

	const int32 N = Group->Members.Num();
	const FString OutputPath = ResolveGroupOutputPath(Group);
	if ((!OutputPath.Equals(TEXT("/Game")) && !OutputPath.StartsWith(TEXT("/Game/")))
		|| !FPackageName::IsValidLongPackageName(OutputPath))
	{
		BulkDebake_Internal::ToastError(FText::Format(
			LOCTEXT("DebakeInvalidOutputPath", "'{0}' is not a valid /Game content path. Choose an output folder and try again."),
			FText::FromString(OutputPath)));
		return false;
	}
	const FString AssetBase = Group->GroupName;
	const TArray<FString> Names = BulkDebake_Internal::MemberNames(*Group);
	const FString RawPrefix = FBulkDebakeUtils::RawCommonPrefix(Names);
	const FDebakeResult& Result = Group->CachedResult;
	const int32 W = Group->PreviewW;
	const int32 SheetH = Group->PreviewH;

	FScopedSlowTask SlowTask((float)(N + 1), LOCTEXT("DebakeAcceptProgress", "Writing de-baked textures..."));
	SlowTask.MakeDialog(/*bShowCancelButton=*/true);

	// Appends one confirmed, grid-stamped pipeline entry for a de-baked output texture.
	auto AppendOutputEntry = [this, Group](UTexture2D* Tex, const FString& Display)
	{
		TSharedPtr<FBulkExtractorTextureState> State = MakeShared<FBulkExtractorTextureState>();
		State->Texture = Tex;
		State->DisplayName = Display;
		State->Status = EBulkExtractorTextureStatus::Confirmed;
		State->bDetectionRun = true;
		State->InferredGrid = FIntPoint(Group->GridColumns, Group->GridRows);
		State->OverrideGrid = State->InferredGrid;
		State->ConfirmedDetectionParams = DetectionParams;

		// Full-cell detected sprites so the canvas + commit paths see the same grid the group used.
		const FIntPoint Dims = FSpriteExtractionUtils::GetDetectionDimensions(Tex);
		const int32 CellW = Dims.X / FMath::Max(1, Group->GridColumns);
		const int32 CellH = Dims.Y / FMath::Max(1, Group->GridRows);
		int32 Idx = 0;
		for (int32 Row = 0; Row < Group->GridRows; ++Row)
		{
			for (int32 Col = 0; Col < Group->GridColumns; ++Col)
			{
				FDetectedSprite DS;
				DS.Bounds = FIntRect(Col * CellW, Row * CellH, (Col + 1) * CellW, (Row + 1) * CellH);
				DS.OriginalBounds = DS.Bounds;
				DS.bSelected = true;
				DS.Index = Idx++;
				State->DetectedSprites.Add(DS);
			}
		}

		TextureStates.Add(State);
		Group->AcceptedEntries.Add(State);
	};

	if (SlowTask.ShouldCancel())
	{
		BulkDebake_Internal::Toast(LOCTEXT("DebakeAcceptCancelled", "De-bake accept cancelled. The batch was not changed."));
		return false;
	}
	SlowTask.EnterProgressFrame(1.0f);
	UTexture2D* BaseTexture = FBulkDebakeUtils::WriteSheetTexture(
		OutputPath, FString::Printf(TEXT("T_%s_Base"), *AssetBase), W, SheetH, Result.BasePixels);
	if (!BaseTexture)
	{
		BulkDebake_Internal::ToastError(LOCTEXT("DebakeWriteBaseFailed", "Failed to create the de-baked base texture."));
		return false;
	}
	TArray<TPair<UTexture2D*, FString>> MaterializedOutputs;
	MaterializedOutputs.Emplace(BaseTexture, FString::Printf(TEXT("%s_Base"), *AssetBase));

	int32 OverlaysWritten = 0;
	for (int32 i = 0; i < N; ++i)
	{
		if (SlowTask.ShouldCancel())
		{
			BulkDebake_Internal::Toast(LOCTEXT("DebakeAcceptCancelledDuringWrite", "De-bake accept cancelled. The batch was not changed; any written assets may be reused on the next accept."));
			return false;
		}
		SlowTask.EnterProgressFrame(1.0f);
		const FString Label = FBulkDebakeUtils::VariantLabel(Names[i], RawPrefix);
		FString OverlayName = FString::Printf(TEXT("T_%s_Overlay_%s"), *AssetBase, *Label);
		FSpriteExtractionUtils::SanitizeAssetName(OverlayName);
		if (Result.OverlayPixelCounts[i] <= 0)
		{
			// Nothing to carry — skip creating an empty texture, but OVERWRITE a leftover from a
			// previous run with the same name so a corrected re-run can't leave stale pixels behind.
			const FString OverlayPackageName = OutputPath / OverlayName;
			if (FindPackage(nullptr, *OverlayPackageName) || FPackageName::DoesPackageExist(OverlayPackageName))
			{
				if (!FBulkDebakeUtils::WriteSheetTexture(OutputPath, OverlayName, W, SheetH, Result.Overlays[i]))
				{
					BulkDebake_Internal::ToastError(FText::Format(
						LOCTEXT("DebakeClearOverlayFailed", "Failed to clear stale de-baked overlay '{0}'. The batch was not changed."),
						FText::FromString(Label)));
					return false;
				}
			}
			continue;
		}
		UTexture2D* OverlayTexture = FBulkDebakeUtils::WriteSheetTexture(OutputPath, OverlayName, W, SheetH, Result.Overlays[i]);
		if (!OverlayTexture)
		{
			BulkDebake_Internal::ToastError(FText::Format(
				LOCTEXT("DebakeWriteOverlayFailed", "Failed to create de-baked overlay '{0}'. The batch was not changed."),
				FText::FromString(Label)));
			return false;
		}
		OverlaysWritten++;
		MaterializedOutputs.Emplace(OverlayTexture, FString::Printf(TEXT("%s_%s"), *AssetBase, *Label));
	}

	// Only mutate the batch after every required output has materialized. A failed overlay write may
	// leave a package on disk, but it cannot leave a half-accepted extraction pipeline in memory.
	if (bReplacingAccepted)
	{
		RevertDebakeGroup(Group);
	}
	Group->AcceptedEntries.Empty();
	Group->PreAcceptStatuses.Empty();
	for (const TPair<UTexture2D*, FString>& Output : MaterializedOutputs)
	{
		AppendOutputEntry(Output.Key, Output.Value);
	}

	// Flip the consumed inputs (members + any VFX masks that sit in the list) to DebakeSource.
	TSet<FSoftObjectPath> ConsumedVfxPaths;
	for (const FDebakeGroupMember& Member : Group->Members)
	{
		if (!Member.VfxFront.IsNull())
		{
			ConsumedVfxPaths.Add(Member.VfxFront.ToSoftObjectPath());
		}
		if (!Member.VfxBack.IsNull())
		{
			ConsumedVfxPaths.Add(Member.VfxBack.ToSoftObjectPath());
		}
	}
	for (const TSharedPtr<FBulkExtractorTextureState>& State : TextureStates)
	{
		if (!State.IsValid())
		{
			continue;
		}
		const bool bIsMember = Group->Members.ContainsByPredicate([&State](const FDebakeGroupMember& M)
		{
			return M.State.Pin() == State;
		});
		const bool bIsConsumedVfx = ConsumedVfxPaths.Contains(State->Texture.ToSoftObjectPath());
		if (bIsMember || bIsConsumedVfx)
		{
			EBulkExtractorTextureStatus OriginalStatus = State->Status;
			if (OriginalStatus == EBulkExtractorTextureStatus::DebakeSource)
			{
				// A VFX texture may be shared by several accepted groups. Carry forward the original
				// non-consumed status so whichever group reverts last can restore it correctly.
				for (const TSharedPtr<FDebakeGroupState>& OtherGroup : DebakeGroups)
				{
					if (!OtherGroup.IsValid() || OtherGroup == Group || OtherGroup->Status != EDebakeGroupStatus::Accepted)
					{
						continue;
					}
					for (const TPair<TWeakPtr<FBulkExtractorTextureState>, EBulkExtractorTextureStatus>& Pair : OtherGroup->PreAcceptStatuses)
					{
						if (Pair.Key.Pin() == State)
						{
							OriginalStatus = Pair.Value;
							break;
						}
					}
					if (OriginalStatus != EBulkExtractorTextureStatus::DebakeSource)
					{
						break;
					}
				}
			}
			Group->PreAcceptStatuses.Emplace(State, OriginalStatus);
			State->Status = EBulkExtractorTextureStatus::DebakeSource;
		}
	}

	Group->Status = EDebakeGroupStatus::Accepted;
	InvalidateDerivedCounts();
	RefreshFilteredTextureList();
	RefreshDebakeSection();

	// Land on the recovered base entry (real texture on canvas, ready for rename/folders/extract).
	if (Group->AcceptedEntries.Num() > 0)
	{
		if (const TSharedPtr<FBulkExtractorTextureState> BaseEntry = Group->AcceptedEntries[0].Pin())
		{
			const int32 BaseIndex = TextureStates.IndexOfByKey(BaseEntry);
			if (BaseIndex != INDEX_NONE)
			{
				SelectTextureByIndex(BaseIndex);
			}
		}
	}

	TSet<int32> UnrecoverableFrames;
	if (Group->GridColumns > 0 && Group->GridRows > 0 && W > 0)
	{
		const int32 CellW = W / Group->GridColumns;
		const int32 CellH = SheetH / Group->GridRows;
		for (const FIntPoint& Pixel : Result.UnrecoverablePixels)
		{
			UnrecoverableFrames.Add((Pixel.Y / CellH) * Group->GridColumns + (Pixel.X / CellW));
		}
	}
	BulkDebake_Internal::Toast(FText::Format(
		LOCTEXT("DebakeAcceptDone", "De-bake accepted: base + {0} overlay texture(s) added to the batch. {1} unrecoverable px across {2} frame(s)."),
		FText::AsNumber(OverlaysWritten),
		FText::AsNumber(Result.UnrecoverablePixels.Num()),
		FText::AsNumber(UnrecoverableFrames.Num())), 8.0f);

	return true;
}

void SBulkSpriteExtractorWindow::RevertDebakeGroup(TSharedPtr<FDebakeGroupState> Group)
{
	if (!Group.IsValid() || Group->Status != EDebakeGroupStatus::Accepted)
	{
		return;
	}

	for (const TWeakPtr<FBulkExtractorTextureState>& Entry : Group->AcceptedEntries)
	{
		if (const TSharedPtr<FBulkExtractorTextureState> Pinned = Entry.Pin())
		{
			if (SelectedTexture == Pinned)
			{
				SelectedTexture = nullptr;
			}
			TextureStates.Remove(Pinned);
		}
	}
	Group->AcceptedEntries.Empty();

	for (const TPair<TWeakPtr<FBulkExtractorTextureState>, EBulkExtractorTextureStatus>& Pair : Group->PreAcceptStatuses)
	{
		if (const TSharedPtr<FBulkExtractorTextureState> Pinned = Pair.Key.Pin())
		{
			const FSoftObjectPath TexturePath = Pinned->Texture.ToSoftObjectPath();
			const bool bStillConsumed = DebakeGroups.ContainsByPredicate([&](const TSharedPtr<FDebakeGroupState>& OtherGroup)
			{
				return OtherGroup.IsValid()
					&& OtherGroup != Group
					&& OtherGroup->Status == EDebakeGroupStatus::Accepted
					&& OtherGroup->ConsumesTexturePath(TexturePath);
			});
			Pinned->Status = bStillConsumed ? EBulkExtractorTextureStatus::DebakeSource : Pair.Value;
		}
	}
	Group->PreAcceptStatuses.Empty();

	Group->Status = Group->CachedResult.BasePixels.Num() > 0 ? EDebakeGroupStatus::Previewed : EDebakeGroupStatus::Pending;
	InvalidateDerivedCounts();
	// Refresh FIRST, then route the replacement selection through the one entry point: assigning
	// SelectedTexture directly left the list highlight and the canvas on the removed entry, and
	// TextureStates[0] is not necessarily a visible row while a search filter is active.
	RefreshFilteredTextureList();
	if (!SelectedTexture.IsValid())
	{
		if (FilteredTextureStates.Num() > 0)
		{
			SelectTextureState(FilteredTextureStates[0]);
		}
		else
		{
			if (TextureListView.IsValid())
			{
				TextureListView->ClearSelection();
			}
			if (CenterCanvas.IsValid() && !IsDebakePreviewActive())
			{
				CenterCanvas->SetTexture(nullptr);
				CenterCanvas->SetDetectedSprites(TArray<FDetectedSprite>());
			}
		}
	}
	RefreshDebakeSection();
	UpdateCanvasForDebakePreview();
}

// ---------------------------------------------------------------------------
// De-bake UI builders
// ---------------------------------------------------------------------------

void SBulkSpriteExtractorWindow::RefreshDebakeSection()
{
	if (DebakeGroupListBox.IsValid())
	{
		DebakeGroupListBox->ClearChildren();
		for (const TSharedPtr<FDebakeGroupState>& Group : DebakeGroups)
		{
			if (!Group.IsValid())
			{
				continue;
			}
			DebakeGroupListBox->AddSlot()
			.AutoHeight()
			.Padding(0, 1)
			[
				BuildDebakeGroupRow(Group)
			];
		}
	}
	if (DebakeGroupEditorBox.IsValid())
	{
		DebakeGroupEditorBox->ClearChildren();
		if (SelectedDebakeGroup.IsValid())
		{
			DebakeGroupEditorBox->AddSlot()
			.AutoHeight()
			.Padding(0, 4, 0, 0)
			[
				BuildDebakeGroupEditor(SelectedDebakeGroup)
			];
		}
	}
}

TSharedRef<SWidget> SBulkSpriteExtractorWindow::BuildDebakeGroupsSection()
{
	TSharedRef<SWidget> Section = SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(6)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DebakeEmptyHint", "Ctrl-select 2+ variant sheets, then use De-bake in the bottom toolbar."))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
				.AutoWrapText(true)
				.Visibility_Lambda([this]() { return DebakeGroups.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				SAssignNew(DebakeGroupListBox, SVerticalBox)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(DebakeGroupEditorBox, SVerticalBox)
			]
		];
	RefreshDebakeSection();
	return Section;
}

TSharedRef<SWidget> SBulkSpriteExtractorWindow::BuildDebakeGroupRow(TSharedPtr<FDebakeGroupState> Group)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
		.BorderBackgroundColor_Lambda([this, Group]()
		{
			return FSlateColor(SelectedDebakeGroup == Group
				? FLinearColor(0.25f, 0.4f, 0.6f)
				: FLinearColor(0.08f, 0.08f, 0.08f));
		})
		.Padding(FMargin(4, 2))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(FSlateColor(Group->GroupColor))
				.Padding(FMargin(5, 5))
				[
					SNew(SBox).WidthOverride(0.0f).HeightOverride(0.0f)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.OnClicked_Lambda([this, Group]() -> FReply
				{
					SelectDebakeGroup(Group);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(Group->GroupName))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.Text_Lambda([Group]()
				{
					switch (Group->Status)
					{
					case EDebakeGroupStatus::Previewed:
						return Group->bPreviewStale ? LOCTEXT("GroupStale", "Stale") : LOCTEXT("GroupPreviewed", "Previewed");
					case EDebakeGroupStatus::Accepted:
						return Group->bPreviewStale ? LOCTEXT("GroupStale2", "Stale") : LOCTEXT("GroupAccepted", "Accepted");
					case EDebakeGroupStatus::Error: return LOCTEXT("GroupError", "Error");
					default: return LOCTEXT("GroupPending", "Pending");
					}
				})
				.ColorAndOpacity_Lambda([Group]()
				{
					if (Group->bPreviewStale) return FSlateColor(FLinearColor(1.0f, 0.75f, 0.2f));
					switch (Group->Status)
					{
					case EDebakeGroupStatus::Previewed: return FSlateColor(FLinearColor(0.45f, 0.82f, 1.0f));
					case EDebakeGroupStatus::Accepted:  return FSlateColor(FLinearColor(0.55f, 1.0f, 0.55f));
					case EDebakeGroupStatus::Error:     return FSlateColor(FLinearColor(1.0f, 0.5f, 0.5f));
					default:                            return FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f));
					}
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("RemoveDebakeGroupTip", "Remove this de-bake group (an accepted group is reverted first; written assets stay on disk)"))
				.OnClicked_Lambda([this, Group]() -> FReply
				{
					RemoveDebakeGroup(Group);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("\u00D7")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.3f, 0.3f)))
				]
			]
		];
}

TSharedRef<SWidget> SBulkSpriteExtractorWindow::BuildDebakeGroupEditor(TSharedPtr<FDebakeGroupState> Group)
{
	TSharedRef<SVerticalBox> Editor = SNew(SVerticalBox);

	// Group name (drives the T_<Name>_Base / T_<Name>_Overlay_<Variant> output names).
	Editor->AddSlot().AutoHeight().Padding(0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			SNew(STextBlock).Text(LOCTEXT("DebakeGroupNameLabel", "Name")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SNew(SEditableTextBox)
			.Text_Lambda([Group]() { return FText::FromString(Group->GroupName); })
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.OnTextCommitted_Lambda([this, Group](const FText& Text, ETextCommit::Type)
			{
				// Sanitize like the batch rename path — the group name feeds asset/package names
				// (T_<Group>_Base + entry DisplayNames), where a space fails only at Extract All.
				FString NewName = Text.ToString().TrimStartAndEnd();
				FSpriteExtractionUtils::SanitizeAssetName(NewName);
				if (!NewName.IsEmpty() && NewName != Group->GroupName)
				{
					Group->GroupName = NewName;
					Group->GroupColor = BulkDebake_Internal::GroupColorFromName(NewName);
					MarkGroupStale(Group);
					RefreshDebakeSection();
					if (TextureListView.IsValid()) TextureListView->RequestListRefresh();
				}
			})
		]
	];

	// Members + per-member reference radio and VFX pickers.
	Editor->AddSlot().AutoHeight().Padding(0, 4, 0, 2)
	[
		SNew(STextBlock)
		.Text(FText::Format(LOCTEXT("DebakeMembersHeader", "Members ({0}) \x2014 the reference sheet owns tie-breaks:"),
			FText::AsNumber(Group->Members.Num())))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
	];
	if (Group->Members.Num() < 3)
	{
		Editor->AddSlot().AutoHeight().Padding(0, 0, 0, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DebakeFewSheetsWarning", "Fewer than 3 variants: the consensus vote is weak and the result may keep per-variant art."))
			.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.65f, 0.1f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.AutoWrapText(true)
		];
	}
	for (int32 i = 0; i < Group->Members.Num(); ++i)
	{
		const TSharedPtr<FBulkExtractorTextureState> MemberState = Group->Members[i].State.Pin();
		// Label, not raw asset name: this row must agree with the list once a texture is renamed.
		// (The GROUP's own name is still derived from the source asset names — see
		// CreateDebakeGroupFromStates — so accepted output packages never depend on session renames.)
		const FString MemberName = MemberState.IsValid()
			? BulkSpriteExtractor_Internal::StateLabel(MemberState)
			: TEXT("(removed)");

		Editor->AddSlot().AutoHeight().Padding(0, 2, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(SCheckBox)
				.Style(&FAppStyle::Get(), "RadioButton")
				.ToolTipText(LOCTEXT("DebakeReferenceTip", "Reference sheet: ambiguous (all-unique) pixels resolve from this sheet so the base stays consistent across frames."))
				.IsChecked_Lambda([Group, i]() { return Group->ReferenceIndex == i ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, Group, i](ECheckBoxState State)
				{
					if (State == ECheckBoxState::Checked && Group->ReferenceIndex != i)
					{
						Group->ReferenceIndex = i;
						MarkGroupStale(Group);
					}
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%d: %s"), i, *MemberName)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ToolTipText(FText::FromString(MemberName))
			]
		];
		Editor->AddSlot().AutoHeight().Padding(14, 1, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 2, 0)
			[
				SNew(SBox).WidthOverride(34.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("DebakeVfxFrontLabel", "Front")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UTexture2D::StaticClass())
				.DisplayThumbnail(false)
				.AllowClear(true)
				.ObjectPath_Lambda([Group, i]() -> FString
				{
					return Group->Members.IsValidIndex(i) && !Group->Members[i].VfxFront.IsNull()
						? Group->Members[i].VfxFront.ToSoftObjectPath().ToString() : FString();
				})
				.OnObjectChanged_Lambda([this, Group, i](const FAssetData& Asset)
				{
					if (Group->Members.IsValidIndex(i))
					{
						Group->Members[i].VfxFront = TSoftObjectPtr<UTexture2D>(Asset.ToSoftObjectPath());
						MarkGroupStale(Group);
					}
				})
			]
		];
		Editor->AddSlot().AutoHeight().Padding(14, 1, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 2, 0)
			[
				SNew(SBox).WidthOverride(34.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("DebakeVfxBackLabel", "Back")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UTexture2D::StaticClass())
				.DisplayThumbnail(false)
				.AllowClear(true)
				.ObjectPath_Lambda([Group, i]() -> FString
				{
					return Group->Members.IsValidIndex(i) && !Group->Members[i].VfxBack.IsNull()
						? Group->Members[i].VfxBack.ToSoftObjectPath().ToString() : FString();
				})
				.OnObjectChanged_Lambda([this, Group, i](const FAssetData& Asset)
				{
					if (Group->Members.IsValidIndex(i))
					{
						Group->Members[i].VfxBack = TSoftObjectPtr<UTexture2D>(Asset.ToSoftObjectPath());
						MarkGroupStale(Group);
					}
				})
			]
		];
	}

	// Grid (the sheets' frame grid — shared by every member).
	Editor->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			SNew(STextBlock).Text(LOCTEXT("DebakeGridLabel", "Grid")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		]
		+ SHorizontalBox::Slot().FillWidth(0.5f)
		[
			SNew(SSpinBox<int32>)
			.MinValue(1).MaxValue(4096)
			.ToolTipText(LOCTEXT("DebakeColsTip", "Columns"))
			.Value_Lambda([Group]() { return Group->GridColumns; })
			.OnValueChanged_Lambda([this, Group](int32 V)
			{
				if (Group->GridColumns != V) { Group->GridColumns = V; MarkGroupStale(Group); }
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3, 0)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("\u00D7"))).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		]
		+ SHorizontalBox::Slot().FillWidth(0.5f)
		[
			SNew(SSpinBox<int32>)
			.MinValue(1).MaxValue(4096)
			.ToolTipText(LOCTEXT("DebakeRowsTip", "Rows"))
			.Value_Lambda([Group]() { return Group->GridRows; })
			.OnValueChanged_Lambda([this, Group](int32 V)
			{
				if (Group->GridRows != V) { Group->GridRows = V; MarkGroupStale(Group); }
			})
		]
	];

	// VFX frame-offset override (unchecked = auto-search per variant).
	Editor->AddSlot().AutoHeight().Padding(0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([Group]() { return Group->bOverrideVfxOffset ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Group](ECheckBoxState State)
			{
				Group->bOverrideVfxOffset = (State == ECheckBoxState::Checked);
				MarkGroupStale(Group);
			})
			.ToolTipText(LOCTEXT("DebakeOffsetTip", "Frame (cell) offset of the VFX sheets within the mains. Unchecked = auto-search per variant; resolved offsets show in the stats."))
			[
				SNew(STextBlock).Text(LOCTEXT("DebakeOffsetOverride", "VFX offset")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(0.5f)
		[
			SNew(SSpinBox<int32>)
			.MinValue(0).MaxValue(4096)
			.IsEnabled_Lambda([Group]() { return Group->bOverrideVfxOffset; })
			.Value_Lambda([Group]() { return Group->VfxOffsetOverrideValue; })
			.OnValueChanged_Lambda([this, Group](int32 V)
			{
				if (Group->VfxOffsetOverrideValue != V) { Group->VfxOffsetOverrideValue = V; MarkGroupStale(Group); }
			})
		]
	];

	// Output path (empty override = beside the first member).
	Editor->AddSlot().AutoHeight().Padding(0, 2)
	[
		FSpriteExtractionUtils::MakeContentPathPicker(
			TAttribute<FString>::CreateLambda([this, Group]() { return ResolveGroupOutputPath(Group); }),
			[this, Group](const FString& Path)
			{
				Group->OutputPathOverride = Path;
				MarkGroupStale(Group);
			})
	];

	// Stats for the previewed result (current variant).
	Editor->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
	[
		SNew(STextBlock)
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
		.AutoWrapText(true)
		.Visibility_Lambda([Group]()
		{
			return Group->CachedResult.BasePixels.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
		})
		.Text_Lambda([this, Group]() -> FText
		{
			const FDebakeResult& R = Group->CachedResult;
			if (R.Overlays.Num() == 0)
			{
				return FText::GetEmpty();
			}
			const int32 V = FMath::Clamp(DebakePreviewVariantIndex, 0, R.Overlays.Num() - 1);
			const FString OffsetText = R.VfxOffsets.IsValidIndex(V) && R.VfxOffsets[V] != INDEX_NONE
				? FString::FromInt(R.VfxOffsets[V]) : TEXT("-");
			return FText::Format(
				LOCTEXT("DebakeStats", "Variant {0}: overlay {1} px, residual {2} px, erase-unfixable {3} px, VFX offset {4}. Unrecoverable: {5} px total."),
				FText::AsNumber(V),
				FText::AsNumber(R.OverlayPixelCounts.IsValidIndex(V) ? R.OverlayPixelCounts[V] : 0),
				FText::AsNumber(R.ResidualCounts.IsValidIndex(V) ? R.ResidualCounts[V] : 0),
				FText::AsNumber(R.EraseUnfixableCounts.IsValidIndex(V) ? R.EraseUnfixableCounts[V] : 0),
				FText::FromString(OffsetText),
				FText::AsNumber(R.UnrecoverablePixels.Num()));
		})
	];

	// Stale / error notes.
	Editor->AddSlot().AutoHeight().Padding(0, 2)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("DebakeStaleNote", "Settings changed \x2014 the preview is stale. Re-preview (Accept recomputes automatically)."))
		.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.75f, 0.2f)))
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		.AutoWrapText(true)
		.Visibility_Lambda([Group]() { return Group->bPreviewStale ? EVisibility::Visible : EVisibility::Collapsed; })
	];
	Editor->AddSlot().AutoHeight().Padding(0, 2)
	[
		SNew(STextBlock)
		.Text_Lambda([Group]() { return Group->LastError; })
		.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.4f, 0.4f)))
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		.AutoWrapText(true)
		.Visibility_Lambda([Group]()
		{
			return (Group->Status == EDebakeGroupStatus::Error && !Group->LastError.IsEmpty())
				? EVisibility::Visible : EVisibility::Collapsed;
		})
	];

	// Actions.
	Editor->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text(LOCTEXT("DebakePreviewButton", "Preview"))
			.ToolTipText(LOCTEXT("DebakePreviewButtonTip", "Compute the de-bake and show the recovered base / overlays on the canvas. Creates NO assets."))
			.OnClicked_Lambda([this, Group]() -> FReply
			{
				PreviewDebakeGroup(Group);
				return FReply::Handled();
			})
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text_Lambda([Group]()
			{
				return Group->Status == EDebakeGroupStatus::Accepted
					? LOCTEXT("DebakeReacceptButton", "Re-accept")
					: LOCTEXT("DebakeAcceptButton", "Accept");
			})
			.ToolTipText(LOCTEXT("DebakeAcceptButtonTip", "Write the base + overlay textures and add them to the batch as confirmed entries (rename / folders / extract apply to them). The original baked sheets drop out of the extraction."))
			.OnClicked_Lambda([this, Group]() -> FReply
			{
				AcceptDebakeGroup(Group);
				return FReply::Handled();
			})
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text(LOCTEXT("DebakeRevertButton", "Revert"))
			.ToolTipText(LOCTEXT("DebakeRevertButtonTip", "Undo the accept: remove the output entries from the batch and restore the members. Written assets stay on disk (a re-accept reuses them)."))
			.Visibility_Lambda([Group]()
			{
				return Group->Status == EDebakeGroupStatus::Accepted ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.OnClicked_Lambda([this, Group]() -> FReply
			{
				RevertDebakeGroup(Group);
				return FReply::Handled();
			})
		]
	];

	return Editor;
}

TSharedRef<SWidget> SBulkSpriteExtractorWindow::BuildDebakeCanvasStrip()
{
	const auto MakeViewButton = [this](EDebakePreviewView View, const FText& Label, const FText& Tip) -> TSharedRef<SWidget>
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.Text(Label)
			.ToolTipText(Tip)
			.ButtonColorAndOpacity_Lambda([this, View]()
			{
				return DebakePreviewView == View ? FLinearColor(0.2f, 0.4f, 0.7f) : FLinearColor(0.15f, 0.15f, 0.15f);
			})
			.OnClicked_Lambda([this, View]() -> FReply
			{
				DebakePreviewView = View;
				UpdateCanvasForDebakePreview();
				return FReply::Handled();
			});
	};

	const auto CycleVariant = [this](int32 Delta)
	{
		if (!SelectedDebakeGroup.IsValid())
		{
			return;
		}
		const int32 N = SelectedDebakeGroup->CachedResult.Overlays.Num();
		if (N <= 0)
		{
			return;
		}
		DebakePreviewVariantIndex = (DebakePreviewVariantIndex + Delta + N) % N;
		UpdateCanvasForDebakePreview();
	};

	return SNew(SHorizontalBox)
		.Visibility_Lambda([this]() { return IsDebakePreviewActive() ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
		[
			MakeViewButton(EDebakePreviewView::Base,
				LOCTEXT("DebakeViewBase", "Base"),
				LOCTEXT("DebakeViewBaseTip", "The recovered shared base sheet"))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
		[
			MakeViewButton(EDebakePreviewView::Overlay,
				LOCTEXT("DebakeViewOverlay", "Overlay"),
				LOCTEXT("DebakeViewOverlayTip", "The selected variant's residual overlay layer"))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeViewButton(EDebakePreviewView::Composite,
				LOCTEXT("DebakeViewComposite", "Composite"),
				LOCTEXT("DebakeViewCompositeTip", "Overlay OVER base \x2014 how the variant reconstructs"))
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.Text(FText::FromString(TEXT("\u25C0")))
			.IsEnabled_Lambda([this]() { return DebakePreviewView != EDebakePreviewView::Base; })
			.OnClicked_Lambda([this, CycleVariant]() -> FReply { CycleVariant(-1); return FReply::Handled(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.Text_Lambda([this]() -> FText
			{
				if (!SelectedDebakeGroup.IsValid() || SelectedDebakeGroup->PreviewMemberNames.Num() == 0)
				{
					return FText::GetEmpty();
				}
				const int32 N = SelectedDebakeGroup->PreviewMemberNames.Num();
				const int32 V = FMath::Clamp(DebakePreviewVariantIndex, 0, N - 1);
				return FText::Format(LOCTEXT("DebakeVariantLabel", "{0}/{1}: {2}"),
					FText::AsNumber(V + 1), FText::AsNumber(N),
					FText::FromString(SelectedDebakeGroup->PreviewMemberNames[V]));
			})
			.ColorAndOpacity_Lambda([this]()
			{
				return DebakePreviewView == EDebakePreviewView::Base
					? FSlateColor(FLinearColor(0.45f, 0.45f, 0.45f))
					: FSlateColor::UseForeground();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.Text(FText::FromString(TEXT("\u25B6")))
			.IsEnabled_Lambda([this]() { return DebakePreviewView != EDebakePreviewView::Base; })
			.OnClicked_Lambda([this, CycleVariant]() -> FReply { CycleVariant(1); return FReply::Handled(); })
		];
}

#undef LOCTEXT_NAMESPACE
