// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FlipbookBrowserPanel.h"
#include "AnimationMapCore.h"
#include "AnimationTagChipUtils.h" // TASK-108 U6: shared provenance-styled tag chips (R6)
#include "AssetRegistry/AssetData.h"
#include "CharacterCoverage/SExpectedTagDragDropWidgets.h"
#include "CharacterProfileEditorModel.h"
#include "ContentBrowserModule.h"
#include "Paper2DPlusSettings.h" // Tag Colors registry — inline card chip tint
#include "ProfileCard.h"
#include "DestructiveActionUtils.h"
#include "EditorCanvasUtils.h"
#include "SDragClickWrapper.h"
#include "SFlipbookGroupWidgets.h"
#include "SlateShortcutUtils.h"
#include "IContentBrowserSingleton.h"
#include "Misc/EngineVersionComparison.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/SlateTypes.h" // ETextOverflowPolicy — the browse bar's narrow-width label elision (5.0+)
// SGameplayTagCombo was added in UE 5.3 (the inline grid-card Animation/Phase tag chips, P4).
// SGameplayTagPicker backs the custom compact leaf-name chip (so long tag paths fit the narrow card).
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
#include "SGameplayTagCombo.h"
#include "SGameplayTagPicker.h"
#endif
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Misc/ConfigCacheIni.h" // TASK-108 U7: per-asset grouping-lens persistence (GEditorPerProjectIni)
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SWindow.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "FlipbookBrowserPanel"

namespace DestructiveActions = Paper2DPlusEditor::DestructiveActionUtils;

namespace
{
static TMap<FString, int32> BuildFlipbookNameUsageCounts(const UPaper2DPlusCharacterProfileAsset* InAsset)
{
	TMap<FString, int32> UsageCounts;
	if (!InAsset)
	{
		return UsageCounts;
	}

	for (const FFlipbookProfileEntry& Entry : InAsset->Flipbooks)
	{
		const FString Key = Entry.Identity.FlipbookName.TrimStartAndEnd();
		if (!Key.IsEmpty())
		{
			UsageCounts.FindOrAdd(Key)++;
		}
	}
	return UsageCounts;
}

static int32 GetFlipbookValidationIssueCount(const FFlipbookProfileEntry& Data, const TMap<FString, int32>& NameUsageCounts, FString* OutTooltip = nullptr)
{
	TArray<FString> Issues;

	const UPaperFlipbook* LoadedFlipbook = Data.Identity.Flipbook.IsNull() ? nullptr : Data.Identity.Flipbook.LoadSynchronous();

	if (!LoadedFlipbook)
	{
		Issues.Add(TEXT("Missing flipbook asset"));
	}

	int32 FrameCount = Data.CombatData.Frames.Num();
	if (FrameCount <= 0)
	{
		if (LoadedFlipbook)
		{
			FrameCount = LoadedFlipbook->GetNumKeyFrames();
		}
		if (FrameCount <= 0)
		{
			Issues.Add(TEXT("Zero frames"));
		}
	}

	const FString NameKey = Data.Identity.FlipbookName.TrimStartAndEnd();
	const int32* NameCount = NameUsageCounts.Find(NameKey);
	if (!NameKey.IsEmpty() && NameCount && *NameCount > 1)
	{
		Issues.Add(TEXT("Duplicate flipbook name"));
	}

	if (OutTooltip)
	{
		*OutTooltip = FString::Join(Issues, TEXT("\n"));
	}

	return Issues.Num();
}

// TASK-108 U7 (R10): per-asset "By Tag Group" lens persistence — the Animations-tab view-mode recipe
// (GEditorPerProjectIni section + sanitized-asset-path key; no explicit Flush).
static const TCHAR* FlipbookBrowser_ConfigSection = TEXT("Paper2DPlus.FlipbookBrowser");

static FString FlipbookBrowser_LensKey(const UObject* InAsset)
{
	FString AssetKey = InAsset ? InAsset->GetPathName() : TEXT("NoAsset");
	AssetKey.ReplaceInline(TEXT("/"), TEXT("_"));
	AssetKey.ReplaceInline(TEXT("."), TEXT("_"));
	AssetKey.ReplaceInline(TEXT(":"), TEXT("_"));
	AssetKey.ReplaceInline(TEXT(" "), TEXT("_"));
	return FString::Printf(TEXT("GroupingLens_%s"), *AssetKey);
}

}

// ==========================================
// CONSTRUCT / DESTRUCT
// ==========================================

void SFlipbookBrowserPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	if (Model.IsValid())
	{
		Asset = Model->GetAsset();
	}

	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}

	if (Model.IsValid())
	{
		ModelFlipbookSelectionHandle = Model->OnFlipbookSelectionChanged.AddSP(this, &SFlipbookBrowserPanel::ScrollToFlipbookWidget);
		ModelAssetDataChangedHandle = Model->OnAssetDataChanged.AddLambda([this]()
		{
			// Guard symmetry with OnAssetExternallyModified below (crash-fix, legacy-cleanup 2026-07):
			// a broadcast landing while THIS panel is mid-transaction must not ClearChildren() the very
			// widgets (open tag-picker combos) driving the edit. Commit sites broadcast AFTER
			// EndTransaction, so the deliberate synchronous self-rebuild path is unaffected.
			if (ActiveTransaction.IsValid()) return;
			RefreshFlipbookGroupsPanel();
		});
		ModelAssetExternallyModifiedHandle = Model->OnAssetExternallyModified.AddLambda([this]()
		{
			if (ActiveTransaction.IsValid()) return;
			RefreshFlipbookGroupsPanel();
		});
		ModelCompletionFilterHandle = Model->OnCompletionFilterChanged.AddLambda([this](int32)
		{
			RefreshFlipbookGroupsPanel();
		});
		// Grid(cards)/List(rows) is chosen by the Animations tab's top-level segments now; refresh on flip.
		ModelGroupViewModeHandle = Model->OnFlipbookGroupViewModeChanged.AddSP(this, &SFlipbookBrowserPanel::RefreshFlipbookGroupsPanel);
	}

	// Tag Colors registry edits repaint the card chips + list-row tints live (the Animation Map board
	// already subscribes — without this the browser shows stale colors until the next incidental rebuild).
	// AddSP self-unbinds when the widget dies; the chips snapshot colors at build, so refresh = rebuild.
	TagColorsChangedHandle = UPaper2DPlusSettings::OnTagColorsChanged().AddSP(this, &SFlipbookBrowserPanel::RefreshFlipbookGroupsPanel);

	// TASK-108 U7: restore the per-asset grouping lens BEFORE the first refresh paints.
	RestoreLensFromConfig();

	ChildSlot
	[
		BuildFlipbookGroupsPanel()
	];

	RefreshFlipbookGroupsPanel();
}

SFlipbookBrowserPanel::~SFlipbookBrowserPanel()
{
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}

	if (Model.IsValid())
	{
		Model->OnFlipbookSelectionChanged.Remove(ModelFlipbookSelectionHandle);
		Model->OnAssetDataChanged.Remove(ModelAssetDataChangedHandle);
		Model->OnAssetExternallyModified.Remove(ModelAssetExternallyModifiedHandle);
		Model->OnCompletionFilterChanged.Remove(ModelCompletionFilterHandle);
		Model->OnFlipbookGroupViewModeChanged.Remove(ModelGroupViewModeHandle);
	}

	if (TagColorsChangedHandle.IsValid())
	{
		UPaper2DPlusSettings::OnTagColorsChanged().Remove(TagColorsChangedHandle);
	}
}

FReply SFlipbookBrowserPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return FReply::Unhandled();
	}

	if (InKeyEvent.GetKey() == EKeys::F2 && PendingRenameFlipbookIndex == INDEX_NONE)
	{
		if (Model.IsValid())
		{
			int32 SelectedIdx = Model->GetSelectedFlipbookIndex();
			if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(SelectedIdx))
			{
				PendingRenameFlipbookIndex = SelectedIdx;
				RefreshFlipbookGroupsPanel();
				return FReply::Handled();
			}
		}
	}
	if (InKeyEvent.GetKey() == EKeys::Delete && CanMutateFlipbookCollection())
	{
		const int32 SelectedIdx = Model.IsValid() ? Model->GetSelectedFlipbookIndex() : INDEX_NONE;
		if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(SelectedIdx))
		{
			DeleteSelectedFlipbook(true);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

// ==========================================
// TRANSACTION SUPPORT
// ==========================================

void SFlipbookBrowserPanel::BeginTransaction(const FText& Description)
{
	if (!ActiveTransaction.IsValid() && Asset.IsValid())
	{
		ActiveTransaction = MakeUnique<FScopedTransaction>(Description);
		Asset->Modify();
	}
}

void SFlipbookBrowserPanel::EndTransaction()
{
	ActiveTransaction.Reset();
	if (Asset.IsValid())
	{
		Asset->MarkPackageDirty();
	}
}

// ==========================================
// PROFILE ANIMATION COLLECTION
// ==========================================

bool SFlipbookBrowserPanel::CanMutateFlipbookCollection() const
{
	if (!Asset.IsValid())
	{
		return false;
	}
#if WITH_EDITORONLY_DATA
	// Fixed / Baked Layer publishing owns this array while attached. Direct edits would immediately
	// diverge from the captured Character Baseline and the bake registration.
	if (Asset->LayerBakeOwnerToken.IsValid())
	{
		return false;
	}
#endif
	return true;
}

FText SFlipbookBrowserPanel::GetCollectionMutationTooltip(bool bDelete) const
{
#if WITH_EDITORONLY_DATA
	if (Asset.IsValid() && Asset->LayerBakeOwnerToken.IsValid())
	{
		return LOCTEXT(
			"LayerBakeOwnsFlipbooksTooltip",
			"This Character Profile's animations are managed by an attached Fixed / Baked Layer Asset. "
			"Change its animation registration or detach that bake set before editing the profile collection.");
	}
#endif
	return bDelete
		? LOCTEXT(
			"DeleteFlipbookTooltip",
			"Delete the selected animation from this Character Profile. The Paper Flipbook asset is not deleted.")
		: LOCTEXT(
			"AddFlipbooksTooltip",
			"Choose one or more existing Paper Flipbooks. Assets already in this profile are skipped.");
}

FReply SFlipbookBrowserPanel::HandleAddFlipbooksClicked()
{
	OpenFlipbookPicker();
	return FReply::Handled();
}

FReply SFlipbookBrowserPanel::HandleDeleteSelectedClicked()
{
	// Button-driven delete returns focus to a stable browse control once it succeeds (TASK-151 U2).
	// The Delete-key path in OnKeyDown deliberately does not: the panel already owns focus there.
	DeleteSelectedFromBrowseBar(true);
	return FReply::Handled();
}

void SFlipbookBrowserPanel::OpenFlipbookPicker()
{
	if (!CanMutateFlipbookCollection())
	{
		return;
	}

	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	FAssetPickerConfig PickerConfig;
	PickerConfig.SelectionMode = ESelectionMode::Multi;
	PickerConfig.Filter.bRecursiveClasses = true;
	PickerConfig.InitialAssetViewType = EAssetViewType::Tile;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
	PickerConfig.Filter.ClassPaths.Add(UPaperFlipbook::StaticClass()->GetClassPathName());
#else
	PickerConfig.Filter.ClassNames.Add(UPaperFlipbook::StaticClass()->GetFName());
#endif

	const TSharedRef<FGetCurrentSelectionDelegate> GetSelection =
		MakeShared<FGetCurrentSelectionDelegate>();
	PickerConfig.GetCurrentSelectionDelegates.Add(&GetSelection.Get());
	const TSharedRef<SWidget> Picker = ContentBrowserModule.Get().CreateAssetPicker(PickerConfig);
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("AddFlipbooksPickerTitle", "Add Character Profile Flipbooks"))
		.ClientSize(FVector2D(760.0f, 560.0f))
		.SupportsMaximize(true)
		.SupportsMinimize(false);
	const TWeakPtr<SWindow> WeakWindow = Window;
	const TWeakPtr<SFlipbookBrowserPanel> WeakPanel = SharedThis(this);

	Window->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(5.0f)
		[
			Picker
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(5.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 5.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("AddSelectedFlipbooks", "Add Selected"))
				.AccessibleText(LOCTEXT(
					"AddSelectedFlipbooksAccessible",
					"Add the selected Paper Flipbooks to this Character Profile"))
				.OnClicked_Lambda([WeakPanel, WeakWindow, GetSelection]()
				{
					const TArray<FAssetData> SelectedAssets = GetSelection->IsBound()
						? GetSelection->Execute()
						: TArray<FAssetData>();
					int32 AddedCount = 0;
					if (TSharedPtr<SFlipbookBrowserPanel> Self = WeakPanel.Pin())
					{
						TArray<UPaperFlipbook*> SelectedFlipbooks;
						SelectedFlipbooks.Reserve(SelectedAssets.Num());
						for (const FAssetData& AssetData : SelectedAssets)
						{
							if (UPaperFlipbook* Flipbook = Cast<UPaperFlipbook>(AssetData.GetAsset()))
							{
								SelectedFlipbooks.Add(Flipbook);
							}
						}
						AddedCount = Self->AddFlipbooks(SelectedFlipbooks);

						const int32 SkippedCount = SelectedAssets.Num() - AddedCount;
						const FText Summary = SelectedAssets.IsEmpty()
							? LOCTEXT("NoFlipbooksSelected", "No Paper Flipbooks were selected.")
							: FText::Format(
								LOCTEXT("AddFlipbooksSummary", "Added {0} flipbook(s); skipped {1}."),
								FText::AsNumber(AddedCount),
								FText::AsNumber(SkippedCount));
						FNotificationInfo Info(Summary);
						Info.ExpireDuration = SkippedCount > 0 ? 5.0f : 3.0f;
						if (TSharedPtr<SNotificationItem> Notification =
							FSlateNotificationManager::Get().AddNotification(Info))
						{
							Notification->SetCompletionState(
								AddedCount > 0 ? SNotificationItem::CS_Success : SNotificationItem::CS_Pending);
						}
					}
					if (TSharedPtr<SWindow> PinnedWindow = WeakWindow.Pin())
					{
						PinnedWindow->RequestDestroyWindow();
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
					.Text(LOCTEXT("CancelAddFlipbooks", "Cancel"))
					.AccessibleText(LOCTEXT("CancelAddFlipbooksAccessible", "Cancel adding flipbooks"))
					.OnClicked_Lambda([WeakWindow]()
					{
						if (TSharedPtr<SWindow> PinnedWindow = WeakWindow.Pin())
						{
							PinnedWindow->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
			]
		]);

	FSlateApplication::Get().AddModalWindow(
		Window,
		FSlateApplication::Get().FindWidgetWindow(AsShared()));
}

int32 SFlipbookBrowserPanel::AddFlipbooks(const TArray<UPaperFlipbook*>& InFlipbooks)
{
	if (!CanMutateFlipbookCollection() || InFlipbooks.IsEmpty())
	{
		return 0;
	}

	TSet<FSoftObjectPath> SeenPaths;
	for (const FFlipbookProfileEntry& Entry : Asset->Flipbooks)
	{
		const FSoftObjectPath ExistingPath = Entry.Identity.Flipbook.ToSoftObjectPath();
		if (!ExistingPath.IsNull())
		{
			SeenPaths.Add(ExistingPath);
		}
	}

	TArray<UPaperFlipbook*> AcceptedFlipbooks;
	AcceptedFlipbooks.Reserve(InFlipbooks.Num());
	for (UPaperFlipbook* Flipbook : InFlipbooks)
	{
		if (!Flipbook)
		{
			continue;
		}
		const FSoftObjectPath FlipbookPath(Flipbook);
		if (FlipbookPath.IsNull() || SeenPaths.Contains(FlipbookPath))
		{
			continue;
		}
		SeenPaths.Add(FlipbookPath);
		AcceptedFlipbooks.Add(Flipbook);
	}

	if (AcceptedFlipbooks.IsEmpty())
	{
		return 0;
	}

	TSet<FString> UsedNames;
	for (const FFlipbookProfileEntry& Entry : Asset->Flipbooks)
	{
		const FString ExistingName = Entry.Identity.FlipbookName.TrimStartAndEnd();
		if (!ExistingName.IsEmpty())
		{
			UsedNames.Add(ExistingName.ToLower());
		}
	}

	FScopedEditorModelMutation MutationScope(Model);
	BeginTransaction(LOCTEXT("AddFlipbooksTransaction", "Add Character Profile Flipbooks"));
	int32 LastAddedIndex = INDEX_NONE;
	for (UPaperFlipbook* Flipbook : AcceptedFlipbooks)
	{
		const FString BaseName = Flipbook->GetName();
		FString UniqueName = BaseName;
		for (int32 Suffix = 2; UsedNames.Contains(UniqueName.ToLower()); ++Suffix)
		{
			UniqueName = FString::Printf(TEXT("%s (%d)"), *BaseName, Suffix);
		}
		UsedNames.Add(UniqueName.ToLower());

		FFlipbookProfileEntry NewEntry;
		NewEntry.Identity.FlipbookName = UniqueName;
		NewEntry.Identity.Flipbook = TSoftObjectPtr<UPaperFlipbook>(Flipbook);
		LastAddedIndex = Asset->Flipbooks.Add(MoveTemp(NewEntry));
		Asset->SyncFramesToFlipbook(LastAddedIndex);
	}
	Asset->InvalidateFlipbookLookupCache();
	EndTransaction();

	if (Model.IsValid())
	{
		Model->SetSelectedFlipbook(LastAddedIndex);
		Model->NotifyAssetDataChanged();
	}
	return AcceptedFlipbooks.Num();
}

bool SFlipbookBrowserPanel::DeleteSelectedFlipbook(bool bRequireConfirmation)
{
	if (!CanMutateFlipbookCollection() || !Model.IsValid())
	{
		return false;
	}

	const int32 RemovedIndex = Model->GetSelectedFlipbookIndex();
	if (!Asset->Flipbooks.IsValidIndex(RemovedIndex))
	{
		return false;
	}
	const FString RemovedName = Asset->Flipbooks[RemovedIndex].Identity.FlipbookName;
	const Paper2DPlusAnimationMap::FMoveRowCounts RowCounts =
		Paper2DPlusAnimationMap::CountRowsForMove(Asset.Get(), RemovedName);
	int32 TagMappingCount = 0;
	for (const TPair<FGameplayTag, FFlipbookTagMapping>& Pair : Asset->TagMappings)
	{
		for (const FFlipbookTagMappingEntry& Entry : Pair.Value.Entries)
		{
			if (Entry.FlipbookName.Equals(RemovedName, ESearchCase::IgnoreCase))
			{
				++TagMappingCount;
			}
		}
	}
	const bool bThumbnailReference =
		Asset->ThumbnailFlipbookName.Equals(RemovedName, ESearchCase::IgnoreCase);

	if (bRequireConfirmation)
	{
		DestructiveActions::FDestructiveActionPrompt Prompt;
		Prompt.Title = LOCTEXT("DeleteProfileAnimationTitle", "Delete Profile Animation");
		Prompt.Body = FText::Format(
			LOCTEXT(
				"DeleteProfileAnimationBody",
				"Delete \"{0}\" and its authored Character Profile data?"),
			FText::FromString(RemovedName));
		Prompt.Consequence = LOCTEXT(
			"DeleteProfileAnimationConsequence",
			"This removes the profile entry plus its hitboxes, cues, curves, root motion, tag memberships, "
			"transition references, thumbnail override, and Animation Map placement. "
			"The Paper Flipbook asset itself will not be deleted.");
		Prompt.AffectedItems.Add(FString::Printf(TEXT("Profile animation: %s"), *RemovedName));
		if (TagMappingCount > 0)
		{
			Prompt.AffectedItems.Add(FString::Printf(
				TEXT("%d animation tag membership(s)"), TagMappingCount));
		}
		if (RowCounts.TotalRows() > 0)
		{
			Prompt.AffectedItems.Add(FString::Printf(
				TEXT("%d transition row(s)"), RowCounts.TotalRows()));
		}
		if (bThumbnailReference)
		{
			Prompt.AffectedItems.Add(TEXT("Character thumbnail override"));
		}
		if (RowCounts.bPlacementExists)
		{
			Prompt.AffectedItems.Add(TEXT("Animation Map placement"));
		}
		if (!DestructiveActions::Confirm(Prompt))
		{
			return false;
		}
	}

	FScopedEditorModelMutation MutationScope(Model);
	BeginTransaction(LOCTEXT("DeleteProfileAnimationTransaction", "Delete Character Profile Animation"));
	Asset->RemoveFlipbookFromTagMappings(RemovedName);
	Paper2DPlusAnimationMap::RemoveAllRowsForMove(Asset.Get(), RemovedName);
	if (bThumbnailReference)
	{
		Asset->ThumbnailFlipbookName.Reset();
	}
	Asset->Flipbooks.RemoveAt(RemovedIndex);
	Asset->InvalidateFlipbookLookupCache();
	EndTransaction();

	const int32 PreferredSelectionIndex = Asset->Flipbooks.IsEmpty()
		? INDEX_NONE
		: FMath::Min(RemovedIndex, Asset->Flipbooks.Num() - 1);
	Model->HandleFlipbookRemoved(RemovedIndex, PreferredSelectionIndex);
	Model->NotifyAssetDataChanged();
	return true;
}

// ==========================================
// "BY TAG GROUP" LENS STATE (TASK-108 U7)
// ==========================================

void SFlipbookBrowserPanel::SetByTagLens(bool bInLens)
{
	if (bByTagLens == bInLens)
	{
		return;
	}
	bByTagLens = bInLens;
	SaveLensToConfig();
	RefreshFlipbookGroupsPanel();
}

void SFlipbookBrowserPanel::RestoreLensFromConfig()
{
	if (!GConfig || !Model.IsValid())
	{
		return;
	}
	FString Saved;
	if (GConfig->GetString(FlipbookBrowser_ConfigSection, *FlipbookBrowser_LensKey(Model->GetAsset()), Saved, GEditorPerProjectIni))
	{
		bByTagLens = (Saved == TEXT("ByTagGroup")); // "MyGroups" + anything else = the default
	}
}

void SFlipbookBrowserPanel::SaveLensToConfig() const
{
	if (!GConfig || !Model.IsValid())
	{
		return;
	}
	GConfig->SetString(FlipbookBrowser_ConfigSection, *FlipbookBrowser_LensKey(Model->GetAsset()),
		bByTagLens ? TEXT("ByTagGroup") : TEXT("MyGroups"), GEditorPerProjectIni);
	// No explicit Flush (legacy-cleanup 2026-07): GConfig holds the value; the engine flushes
	// GEditorPerProjectIni itself. A manual Flush rewrites the whole multi-hundred-KB ini via
	// tmp+MoveFile and stalled ~8s per call under file-lock contention - the slow-close bug.
}

void SFlipbookBrowserPanel::CommitFlipbookRename(int32 FlipbookIndex, const FString& NewName)
{
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Model.IsValid() ? Model->GetAsset() : nullptr;
	if (!AssetPtr || !AssetPtr->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return;
	}

	// Cheap no-op guard so committing an unchanged name (the common case — entering then leaving
	// edit mode) does not open an empty transaction or dirty the package.
	const FString Trimmed = NewName.TrimStartAndEnd();
	if (Trimmed.IsEmpty() ||
		Trimmed.Equals(AssetPtr->Flipbooks[FlipbookIndex].Identity.FlipbookName, ESearchCase::CaseSensitive))
	{
		return;
	}

	BeginTransaction(LOCTEXT("RenameFlipbookTransaction", "Rename Flipbook"));
	const bool bChanged = AssetPtr->RenameFlipbookAndPropagate(FlipbookIndex, Trimmed);
	EndTransaction();

	if (bChanged && Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
}

FGameplayTag SFlipbookBrowserPanel::GetHomeAnimationTag(const FString& FlipbookName) const
{
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Model.IsValid() ? Model->GetAsset() : nullptr;
	if (!AssetPtr || FlipbookName.IsEmpty())
	{
		return FGameplayTag();
	}
	// First-tag-wins single-home (delegates to the shared sorted-key walk so it can't drift from
	// ProjectAnimationMap's dedup order).
	return Paper2DPlusAnimationMap::FindHomeAnimationGroupTag(AssetPtr, FlipbookName);
}

void SFlipbookBrowserPanel::CommitCardAnimationTag(int32 FlipbookIndex, FGameplayTag CurrentTag, FGameplayTag NewTag)
{
	if (NewTag == CurrentTag)
	{
		return; // no-op never transacts
	}
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Model.IsValid() ? Model->GetAsset() : nullptr;
	if (!AssetPtr || !AssetPtr->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return;
	}
	const FString FlipbookName = AssetPtr->Flipbooks[FlipbookIndex].Identity.FlipbookName;
	UObject* Sequence = AssetPtr->Flipbooks[FlipbookIndex].Identity.PaperZDSequence.Get();

	BeginTransaction(LOCTEXT("CardSetAnimationTag", "Set Animation Tag"));
	if (NewTag.IsValid())
	{
		// Single-home: strips the move from every other tag + keeps the hidden FlipbookGroup<->tag
		// coupling in sync. NEVER a raw TagMappings write.
		AssetPtr->AssignFlipbookToTagMapping(NewTag, FlipbookName, Sequence);
	}
	else
	{
		AssetPtr->RemoveFlipbookFromTagMappings(FlipbookName);
	}
	EndTransaction();

	if (Model.IsValid())
	{
		// Synchronous RefreshFlipbookGroupsPanel rebuilds the firing combo from its own OnTagChanged —
		// proven safe (CommitFlipbookRename does the same from an SInlineEditableTextBlock).
		Model->NotifyAssetDataChanged();
	}
}

void SFlipbookBrowserPanel::CommitCardAnimationTags(int32 FlipbookIndex, const FGameplayTagContainer& NewTags)
{
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Model.IsValid() ? Model->GetAsset() : nullptr;
	if (!AssetPtr || !AssetPtr->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return;
	}
	if (AssetPtr->Flipbooks[FlipbookIndex].EditorMeta.AnimationTags == NewTags)
	{
		return; // no-op never transacts
	}
	BeginTransaction(LOCTEXT("CardSetAnimationTags", "Set Animation Tags"));
	AssetPtr->Flipbooks[FlipbookIndex].EditorMeta.AnimationTags = NewTags;
	EndTransaction();

	if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
}

void SFlipbookBrowserPanel::CommitCardPhaseTag(int32 FlipbookIndex, FGameplayTag NewTag)
{
	UPaper2DPlusCharacterProfileAsset* AssetPtr = Model.IsValid() ? Model->GetAsset() : nullptr;
	if (!AssetPtr || !AssetPtr->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		return;
	}
	if (AssetPtr->Flipbooks[FlipbookIndex].EditorMeta.PhaseTag == NewTag)
	{
		return;
	}
	BeginTransaction(LOCTEXT("CardSetPhaseTag", "Set Phase Tag"));
	AssetPtr->Flipbooks[FlipbookIndex].EditorMeta.PhaseTag = NewTag;
	EndTransaction();

	if (Model.IsValid())
	{
		Model->NotifyAssetDataChanged();
	}
}

// ==========================================
// UNDO
// ==========================================

void SFlipbookBrowserPanel::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		RefreshFlipbookGroupsPanel();
	}
}

void SFlipbookBrowserPanel::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		RefreshFlipbookGroupsPanel();
	}
}

// ==========================================
// HELPERS
// ==========================================

TArray<int32> SFlipbookBrowserPanel::GetSortedFlipbookIndices() const
{
	if (Model.IsValid())
	{
		return Model->GetSortedFlipbookIndices();
	}
	return TArray<int32>();
}

void SFlipbookBrowserPanel::TriggerPendingRenameIfNeeded(TMap<int32, TSharedPtr<SInlineEditableTextBlock>>& NameTexts)
{
	if (PendingRenameFlipbookIndex == INDEX_NONE) return;
	int32 PendingIdx = PendingRenameFlipbookIndex;
	PendingRenameFlipbookIndex = INDEX_NONE;

	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
		[this, &NameTexts, PendingIdx](double, float) -> EActiveTimerReturnType
		{
			if (TSharedPtr<SInlineEditableTextBlock>* FoundText = NameTexts.Find(PendingIdx))
			{
				if (FoundText->IsValid())
				{
					(*FoundText)->EnterEditingMode();
				}
			}
			return EActiveTimerReturnType::Stop;
		}));
}

void SFlipbookBrowserPanel::ScrollToFlipbookWidget(int32 FlipbookIndex)
{
	if (!FlipbookGroupsScrollBox.IsValid()) return;

	TWeakPtr<SWidget>* FoundWidget = FlipbookGroupWidgetMap.Find(FlipbookIndex);
	if (FoundWidget)
	{
		TSharedPtr<SWidget> Widget = FoundWidget->Pin();
		if (Widget.IsValid())
		{
			FlipbookGroupsScrollBox->ScrollDescendantIntoView(Widget.ToSharedRef(), true);
		}
	}
}

// ==========================================
// BUILD
// ==========================================

// TASK-151 U2: ONE compact browse bar replaces the previous two chrome rows. Order is the approved
// narrow-width priority (KTD11): Add | flexible search | completion filter (+ conditional direct
// clear) | contextual Delete | Organize | Browse overflow.
//
// Narrow-width contract (KTD11/R17) — the bar has to survive the narrow Character capture, where the
// full-length labels alone exceed the panel width:
//   1. Search is floored at SearchBoxMinWidth on an AUTOWIDTH slot, and an empty FillWidth spacer
//      immediately after it absorbs all remaining slack. This ordering is load-bearing, not
//      cosmetic: SHorizontalBox allots AutoWidth slots their desired width and only then divides
//      what is left among fill slots, ignoring the fill child's desired size entirely. Search used
//      to BE the fill slot, so its MinDesiredWidth was never consulted and the box could be allotted
//      zero — untypable at exactly the widths the floor was written to protect. Putting the floor on
//      an AutoWidth slot makes it real; putting the spacer after it keeps every trailing control
//      pinned right, unchanged. The spacer is the only slot allowed to collapse to nothing.
//   2. Every long label sits in a MaxDesiredWidth SBox and elides (ETextOverflowPolicy::Ellipsis), so
//      no single label can push a trailing control out of the row.
//   3. Below CompactBrowseBarWidth the labels drop to their compact forms, which keeps Add, the active
//      filter state, Organize and the Browse overflow all reachable. Tooltips and accessible names keep
//      the full text, so nothing becomes unlabelled (R19).
//   4. The whole row is wrapped in a ClipToBounds box: even in the pathological case nothing renders
//      outside the panel's allotted geometry, and the bar still never wraps to a second line.
TSharedRef<SWidget> SFlipbookBrowserPanel::BuildFlipbookGroupsPanel()
{
	CollectionMutationControls.Reset();

	// Built once here and handed to BOTH the widget and the collection-mutation registry, so the
	// persistent-count seam measures exactly the visibility the bar renders.
	const TAttribute<EVisibility> AddVisibility = EVisibility::Visible;
	const TAttribute<EVisibility> DeleteVisibility = TAttribute<EVisibility>::Create(
		TAttribute<EVisibility>::FGetter::CreateSP(this, &SFlipbookBrowserPanel::GetDeleteActionVisibility));

	TSharedRef<SWidget> BrowseBar =
		SNew(SHorizontalBox)

		// R4: the SOLE persistent collection mutation.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 4, 0)
		[
			SAssignNew(AddFlipbooksButton, SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ToolTipText_Lambda([this]() { return GetCollectionMutationTooltip(false); })
			.AccessibleText(LOCTEXT(
				"AddFlipbooksAccessible",
				"Add one or more existing Paper Flipbooks to this Character Profile"))
			.Visibility(AddVisibility)
			.IsEnabled_Lambda([this]() { return CanMutateFlipbookCollection(); })
			.OnClicked(this, &SFlipbookBrowserPanel::HandleAddFlipbooksClicked)
			[
				SNew(SBox)
				.MaxDesiredWidth(120.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return GetAddActionButtonLabel(); })
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
			]
		]

		// R5: search stays direct and is the only flexible slot, so it is the first thing to give up
		// width. The SBox floor keeps it typable instead of letting the fill slot squeeze to zero.
		// AutoWidth, NOT FillWidth: this is what makes the 80px floor real. SHorizontalBox allots
		// AutoWidth slots their desired width and only then divides the remainder among fill slots,
		// so a MinDesiredWidth on a FILL slot's child is never consulted — the search box was being
		// allotted whatever was left, down to and including zero, and going untypable exactly when
		// the floor was supposed to save it. On an AutoWidth slot the same 80px is honoured.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 4, 0)
		[
			SNew(SBox)
			.MinDesiredWidth(SearchBoxMinWidth)
			[
				SNew(SEditableTextBox)
				.MinDesiredWidth(SearchBoxMinWidth)
				.HintText(LOCTEXT("GroupSearchHint", "Search flipbooks..."))
				.Text_Lambda([this]() { return Model.IsValid() ? FText::FromString(Model->GetFlipbookGroupSearchText()) : FText::GetEmpty(); })
				.OnTextChanged_Lambda([this](const FText& NewText)
				{
					if (!Model.IsValid()) return;
					const FString NewSearch = NewText.ToString();
					Model->SetFlipbookGroupSearchText(NewSearch);

					if (auto PinnedTimer = FlipbookGroupSearchDebounceTimer.Pin())
					{
						UnRegisterActiveTimer(PinnedTimer.ToSharedRef());
					}
					FlipbookGroupSearchDebounceTimer = RegisterActiveTimer(0.15f,
						FWidgetActiveTimerDelegate::CreateLambda([this](double, float) -> EActiveTimerReturnType
						{
							RefreshFlipbookGroupsPanel();
							return EActiveTimerReturnType::Stop;
						}));
				})
			]
		]

		// The slack search used to absorb now lands here. This keeps the trailing controls pinned to
		// the right edge exactly as before — the only visible change from flooring the search box is
		// that search stops growing with the panel instead of stretching across it. The spacer is the
		// one slot that may collapse to nothing, which is precisely what a spacer is for.
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNullWidget::NullWidget
		]

		// R5/R16: the compact completion filter keeps its visible active state (tinted icon + count).
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			BuildCompletionFilterButton([this]() { RefreshFlipbookGroupsPanel(); })
		]

		// R16: an ACTIVE completion filter gets a one-click clear right beside its state readout;
		// it collapses again the moment the filter is inactive (the Map filter-chip idiom).
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2, 0, 4, 0)
		[
			SAssignNew(ClearCompletionFilterButton, SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(2, 0))
			.ToolTipText(LOCTEXT("ClearCompletionFilterTip", "Clear the active completion filter"))
			.AccessibleText(LOCTEXT(
				"ClearCompletionFilterAccessible",
				"Clear the active completion filter"))
			.Visibility_Lambda([this]()
			{
				return IsCompletionFilterActive() ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.OnClicked(this, &SFlipbookBrowserPanel::HandleClearCompletionFilterClicked)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ClearCompletionFilterGlyph", "✕"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.7f, 1.0f)))
			]
		]

		// R6: Delete is CONTEXTUAL on the SELECTION — it consumes bar width only while a deletable
		// entry is selected. Fixed / Baked Layer ownership is a blocked cause rather than a missing
		// target, so it disables the action (with Add's explanation) instead of hiding it.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 4, 0)
		[
			SAssignNew(DeleteFlipbookButton, SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.ToolTipText_Lambda([this]() { return GetCollectionMutationTooltip(true); })
			.AccessibleText(LOCTEXT(
				"DeleteSelectedFlipbookAccessible",
				"Delete the selected animation from this Character Profile"))
			.Visibility(DeleteVisibility)
			.IsEnabled_Lambda([this]() { return IsDeleteActionEnabled(); })
			.OnClicked(this, &SFlipbookBrowserPanel::HandleDeleteSelectedClicked)
			[
				SNew(SBox)
				.MaxDesiredWidth(120.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return GetDeleteActionButtonLabel(); })
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
			]
		]

		// R8: one labeled Organize menu replaces the "My Groups | By Tag Group" segmented toggle.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 4, 0)
		[
			SAssignNew(OrganizeComboButton, SComboButton)
			.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
			.ContentPadding(FMargin(6, 2))
			.ToolTipText_Lambda([this]() { return GetOrganizeTooltip(); })
			.AccessibleText_Lambda([this]() { return GetOrganizeLabel(); })
			.OnGetMenuContent(this, &SFlipbookBrowserPanel::BuildOrganizeMenu)
			.ButtonContent()
			[
				SNew(SBox)
				.MaxDesiredWidth_Lambda([this]()
				{
					return FOptionalSize(IsBrowseBarCompact() ? 90.0f : 160.0f);
				})
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return GetOrganizeButtonLabel(); })
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
			]
		]

		// R7: group maintenance lives in a labeled overflow. Both entries stay listed in every
		// organization mode; By Tag Group only disables them with an explanatory tooltip.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SAssignNew(BrowseComboButton, SComboButton)
			.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
			.ContentPadding(FMargin(6, 2))
			.ToolTipText(LOCTEXT(
				"BrowseOverflowTip",
				"Group maintenance for this animation collection: New Group and Auto-group."))
			.AccessibleText(LOCTEXT(
				"BrowseOverflowAccessible",
				"Browse menu: group maintenance for this animation collection"))
			.OnGetMenuContent(this, &SFlipbookBrowserPanel::BuildBrowseMenu)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BrowseOverflowLabel", "Browse"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]
		];

	RegisterCollectionMutationControl(TEXT("AddFlipbooks"), AddFlipbooksButton, AddVisibility);
	RegisterCollectionMutationControl(TEXT("DeleteSelected"), DeleteFlipbookButton, DeleteVisibility);

	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			// Clipped host: an over-full bar degrades to elided/compact labels, never to controls
			// painted outside the panel's allotted geometry (KTD11).
			SNew(SBox)
			.Clipping(EWidgetClipping::ClipToBounds)
			[
				BrowseBar
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(FlipbookGroupsScrollBox, SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(FlipbookGroupsListBox, SVerticalBox)
			]
		];
}

// ==========================================
// COMPACT BROWSE BAR STATE (TASK-151 U2)
// ==========================================

void SFlipbookBrowserPanel::RegisterCollectionMutationControl(
	FName Id,
	TSharedPtr<SWidget> Control,
	// Not "Visibility": SWidget already declares a member of that name, and the shadow is a
	// C4458 that UE 5.1-5.5 promote to an error under -WarningsAsErrors.
	const TAttribute<EVisibility>& InVisibility)
{
	FCollectionMutationControl& Entry = CollectionMutationControls.AddDefaulted_GetRef();
	Entry.Id = Id;
	Entry.Control = MoveTemp(Control);
	Entry.Visibility = InVisibility;
}

TArray<FName> SFlipbookBrowserPanel::GetPersistentCollectionMutationControls() const
{
	TArray<FName> Persistent;
	for (const FCollectionMutationControl& Entry : CollectionMutationControls)
	{
		if (!Entry.Control.IsValid())
		{
			// Never constructed, so it is not on the bar at all.
			continue;
		}
		if (Entry.Visibility.IsBound())
		{
			// Bound to live state = contextual by construction; it can collapse, so it is not
			// persistent. A contextual control that LOSES its binding lands in this list instead.
			continue;
		}
		if (!Entry.Visibility.Get(EVisibility::Visible).IsVisible())
		{
			continue;
		}
		Persistent.Add(Entry.Id);
	}
	return Persistent;
}

TArray<FName> SFlipbookBrowserPanel::GetVisibleCollectionMutationControls() const
{
	TArray<FName> Visible;
	for (const FCollectionMutationControl& Entry : CollectionMutationControls)
	{
		if (Entry.Control.IsValid() && Entry.Visibility.Get(EVisibility::Visible).IsVisible())
		{
			Visible.Add(Entry.Id);
		}
	}
	return Visible;
}

bool SFlipbookBrowserPanel::ShouldShowDeleteAction() const
{
	// VISIBILITY answers "is there something to delete?" only. Blocked-but-targeted cases (Fixed /
	// Baked ownership) stay visible and disabled so the designer gets the same explanation Add gives,
	// instead of the action silently disappearing for a cause it cannot infer.
	if (!Model.IsValid() || !Asset.IsValid())
	{
		return false;
	}
	return Asset->Flipbooks.IsValidIndex(Model->GetSelectedFlipbookIndex());
}

bool SFlipbookBrowserPanel::IsDeleteActionEnabled() const
{
	return ShouldShowDeleteAction() && CanMutateFlipbookCollection();
}

EVisibility SFlipbookBrowserPanel::GetDeleteActionVisibility() const
{
	return ShouldShowDeleteAction() ? EVisibility::Visible : EVisibility::Collapsed;
}

bool SFlipbookBrowserPanel::IsBrowseBarCompact() const
{
	// Unmeasured geometry (headless construction, or the very first paint) is deliberately NOT compact:
	// the seams, tooltips and accessible names then report the full labels in automation.
	const float AllottedWidth = GetTickSpaceGeometry().GetLocalSize().X;
	return AllottedWidth > 0.0f && AllottedWidth < CompactBrowseBarWidth;
}

FText SFlipbookBrowserPanel::GetAddActionButtonLabel() const
{
	// The compact form keeps its trailing ellipsis: the affordance still opens a picker dialog.
	return IsBrowseBarCompact()
		? LOCTEXT("AddFlipbooksCompact", "+ Add…")
		: LOCTEXT("AddFlipbooks", "+ Add Flipbooks…");
}

FText SFlipbookBrowserPanel::GetDeleteActionButtonLabel() const
{
	// Keeps the confirmation ellipsis in both forms (the delete still confirms before it mutates).
	return IsBrowseBarCompact()
		? LOCTEXT("DeleteSelectedFlipbookCompact", "Delete…")
		: LOCTEXT("DeleteSelectedFlipbook", "Delete Selected…");
}

FText SFlipbookBrowserPanel::GetOrganizeButtonLabel() const
{
	if (!IsBrowseBarCompact())
	{
		return GetOrganizeLabel();
	}
	// The active organization mode must stay readable without opening the menu (R16), so the compact
	// form drops the "Organize:" prefix rather than the value.
	return bByTagLens
		? LOCTEXT("OrganizeByTagGroupCompact", "By Tag Group")
		: LOCTEXT("OrganizeMyGroupsCompact", "My Groups");
}

FText SFlipbookBrowserPanel::GetOrganizeLabel() const
{
	return bByTagLens
		? LOCTEXT("OrganizeByTagGroup", "Organize: By Tag Group")
		: LOCTEXT("OrganizeMyGroups", "Organize: My Groups");
}

FText SFlipbookBrowserPanel::GetOrganizeTooltip() const
{
	return bByTagLens
		? LOCTEXT(
			"OrganizeByTagGroupTip",
			"Organizing by tag group: one derived section per animation tag group (plus Unmapped), "
			"chains in root-to-finisher order. Ordering is read-only — drag-drop and group management "
			"are disabled. Choose My Groups to organize by your own flipbook groups.")
		: LOCTEXT(
			"OrganizeMyGroupsTip",
			"Organizing by your own flipbook groups: drag cards between groups and manage groups from "
			"the Browse menu. Choose By Tag Group for the derived read-only tag view.");
}

TSharedRef<SWidget> SFlipbookBrowserPanel::BuildOrganizeMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.BeginSection(NAME_None, LOCTEXT("OrganizeSection", "Organize"));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("MyGroupsLens", "My Groups"),
		LOCTEXT("MyGroupsLensTip", "Organize by your own flipbook groups (drag-drop enabled)."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SFlipbookBrowserPanel::SetByTagLens, false),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &SFlipbookBrowserPanel::IsByTagLensActive, false)),
		NAME_None,
		EUserInterfaceActionType::RadioButton);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ByTagLens", "By Tag Group"),
		LOCTEXT(
			"ByTagLensTip",
			"Derived view: one section per animation tag group (plus Unmapped), chains in "
			"root-to-finisher order. Read-only ordering — drag-drop is disabled."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SFlipbookBrowserPanel::SetByTagLens, true),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &SFlipbookBrowserPanel::IsByTagLensActive, true)),
		NAME_None,
		EUserInterfaceActionType::RadioButton);

	MenuBuilder.EndSection();
	return MenuBuilder.MakeWidget();
}

bool SFlipbookBrowserPanel::CanExecuteBrowseOverflowAction(EFlipbookBrowseOverflowAction Action) const
{
	if (!Asset.IsValid())
	{
		return false;
	}
	// U7 rule preserved: group management targets the user-authored groups, so it applies only in
	// My Groups. In the derived lens the result would be invisible until the designer flips back.
	switch (Action)
	{
	case EFlipbookBrowseOverflowAction::NewGroup:
	case EFlipbookBrowseOverflowAction::AutoGroup:
		return !bByTagLens;
	default:
		return false;
	}
}

// Every disable reason gets its OWN explanation. CanExecuteBrowseOverflowAction refuses both a
// missing Character Profile and the derived lens; telling a designer with no loaded profile to switch
// Organize back to My Groups would send them somewhere that cannot help (review finding B).
// The Action stays in the signature so a future per-action reason has a place to go without touching
// call sites; today both entries share the same two refusal causes.
FText SFlipbookBrowserPanel::GetBrowseOverflowDisabledTooltip(EFlipbookBrowseOverflowAction /*Action*/) const
{
	if (!Asset.IsValid())
	{
		return LOCTEXT(
			"BrowseOverflowNoAssetTip",
			"Unavailable: this browser has no Character Profile loaded, so there is no group list to "
			"manage. Reopen the Character Profile editor for the asset you want to organize.");
	}
	if (bByTagLens)
	{
		return LOCTEXT(
			"BrowseOverflowLensDisabledTip",
			"Unavailable while Organize is set to By Tag Group: that view is derived from animation tag "
			"mappings and its ordering is read-only. Switch Organize to My Groups to manage groups.");
	}
	// Defensive: a future refusal reason must add its own branch here rather than inherit someone
	// else's explanation.
	return LOCTEXT(
		"BrowseOverflowUnavailableTip",
		"Unavailable for this animation collection right now.");
}

TArray<FFlipbookBrowseOverflowEntry> SFlipbookBrowserPanel::GetBrowseOverflowActions() const
{
	TArray<FFlipbookBrowseOverflowEntry> Entries;

	{
		FFlipbookBrowseOverflowEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Action = EFlipbookBrowseOverflowAction::NewGroup;
		Entry.Label = LOCTEXT("NewGroup", "New Group");
		Entry.bEnabled = CanExecuteBrowseOverflowAction(Entry.Action);
		Entry.ToolTip = Entry.bEnabled
			? LOCTEXT("NewGroupTooltip", "Create a new flipbook group at the root level")
			: GetBrowseOverflowDisabledTooltip(Entry.Action);
	}

	{
		FFlipbookBrowseOverflowEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Action = EFlipbookBrowseOverflowAction::AutoGroup;
		Entry.Label = LOCTEXT("AutoGroup", "Auto-group");
		Entry.bEnabled = CanExecuteBrowseOverflowAction(Entry.Action);
		Entry.ToolTip = Entry.bEnabled
			? LOCTEXT("AutoGroupTooltip", "Automatically group ungrouped flipbooks by name prefix")
			: GetBrowseOverflowDisabledTooltip(Entry.Action);
	}

	return Entries;
}

void SFlipbookBrowserPanel::ExecuteBrowseOverflowAction(EFlipbookBrowseOverflowAction Action)
{
	if (!CanExecuteBrowseOverflowAction(Action))
	{
		return;
	}
	switch (Action)
	{
	case EFlipbookBrowseOverflowAction::NewGroup:
		CreateFlipbookGroup();
		break;
	case EFlipbookBrowseOverflowAction::AutoGroup:
		AutoGroupByPrefix();
		break;
	default:
		break;
	}
}

TSharedRef<SWidget> SFlipbookBrowserPanel::BuildBrowseMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.BeginSection(NAME_None, LOCTEXT("BrowseGroupsSection", "Groups"));

	for (const FFlipbookBrowseOverflowEntry& Entry : GetBrowseOverflowActions())
	{
		const EFlipbookBrowseOverflowAction Action = Entry.Action;
		MenuBuilder.AddMenuEntry(
			Entry.Label,
			Entry.ToolTip,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SFlipbookBrowserPanel::ExecuteBrowseOverflowAction, Action),
				FCanExecuteAction::CreateSP(this, &SFlipbookBrowserPanel::CanExecuteBrowseOverflowAction, Action)));
	}

	MenuBuilder.EndSection();
	return MenuBuilder.MakeWidget();
}

bool SFlipbookBrowserPanel::IsCompletionFilterActive() const
{
	return Model.IsValid() && Model->GetCompletionFilterMask() != 0;
}

void SFlipbookBrowserPanel::ClearCompletionFilter()
{
	if (!IsCompletionFilterActive())
	{
		return;
	}
	// The model broadcasts OnCompletionFilterChanged, which this panel already subscribes to, so the
	// clear restores the compact inactive state without a second explicit refresh.
	Model->SetCompletionFilterMask(0);
}

FReply SFlipbookBrowserPanel::HandleClearCompletionFilterClicked()
{
	ClearCompletionFilter();
	return FReply::Handled();
}

bool SFlipbookBrowserPanel::DeleteSelectedFromBrowseBar(bool bRequireConfirmation)
{
	const bool bDeleted = DeleteSelectedFlipbook(bRequireConfirmation);
	if (bDeleted)
	{
		// The contextual action just removed its own target (and collapses itself), so hand focus to
		// a stable browse control rather than letting it fall to the destroyed card.
		FocusStableBrowseControl();
	}
	return bDeleted;
}

void SFlipbookBrowserPanel::FocusStableBrowseControl()
{
	LastBrowseFocusTarget = NAME_None;

	TSharedPtr<SWidget> Target;
	if (AddFlipbooksButton.IsValid() && AddFlipbooksButton->IsEnabled())
	{
		Target = AddFlipbooksButton;
		LastBrowseFocusTarget = TEXT("AddFlipbooks");
	}
	else if (OrganizeComboButton.IsValid())
	{
		// Fixed / Baked ownership disables Add; Organize is the next stable, always-present control.
		Target = OrganizeComboButton;
		LastBrowseFocusTarget = TEXT("Organize");
	}

	// Only move real focus when the control is actually hosted in a live window; headless automation
	// constructs the panel outside any window and must not push focus into the running editor.
	if (Target.IsValid()
		&& FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().FindWidgetWindow(Target.ToSharedRef()).IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(Target, EFocusCause::SetDirectly);
	}
}

// ==========================================
// REFRESH
// ==========================================

void SFlipbookBrowserPanel::RefreshFlipbookGroupsPanel()
{
	if (!FlipbookGroupsListBox.IsValid() || !Asset.IsValid() || !Model.IsValid()) return;

	float SavedScrollOffset = 0.0f;
	if (FlipbookGroupsScrollBox.IsValid())
	{
		SavedScrollOffset = FlipbookGroupsScrollBox->GetScrollOffset();
	}

	TSet<int32>& SelectedFlipbookCards = Model->GetSelectedFlipbookCardsMutable();
	if (Asset.IsValid())
	{
		TSet<int32> ValidSelection;
		for (int32 Idx : SelectedFlipbookCards)
		{
			if (Asset->Flipbooks.IsValidIndex(Idx))
			{
				ValidSelection.Add(Idx);
			}
		}
		SelectedFlipbookCards = MoveTemp(ValidSelection);
		int32 Anchor = Model->GetSelectionAnchorIndex();
		if (Anchor != INDEX_NONE && !Asset->Flipbooks.IsValidIndex(Anchor))
		{
			Model->SetSelectionAnchorIndex(INDEX_NONE);
		}
	}
	else
	{
		SelectedFlipbookCards.Empty();
		Model->SetSelectionAnchorIndex(INDEX_NONE);
	}
	FlipbookGroupNameTexts.Empty();
	FlipbookGroupFlipbookNameTexts.Empty();
	FlipbookGroupWidgetMap.Empty();

	// TASK-108 U6: ONE effective-tag batch per refresh — the card chips (R6) and the search
	// predicate's tag dimension (R7) both read this cache; never re-batch per card.
	CachedAnimationTagMap = Paper2DPlusAnimationTagQuery::BuildAnimationTagMap(Asset.Get());

	FlipbookGroupsListBox->ClearChildren();

	// TASK-108 U7 (R10): the derived "By Tag Group" lens replaces the user-group tree wholesale —
	// one collapsible section per TagMappings key + the "Unmapped" fallback, members chain-ordered
	// by the PURE BuildTagLensSections helper. My Groups below stays byte-identical when off.
	if (bByTagLens)
	{
		const TMap<FString, int32> LensUsageCounts = BuildFlipbookNameUsageCounts(Asset.Get());
		const TArray<Paper2DPlusAnimationMap::FTagLensSection> LensSections =
			Paper2DPlusAnimationMap::BuildTagLensSections(Asset.Get());
		for (const Paper2DPlusAnimationMap::FTagLensSection& LensSection : LensSections)
		{
			TSharedRef<SWidget> SectionWidget = BuildTagLensSectionWidget(LensSection, LensUsageCounts);
			if (SectionWidget != SNullWidget::NullWidget)
			{
				FlipbookGroupsListBox->AddSlot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SectionWidget
				];
			}
		}
		if (LensSections.Num() == 0)
		{
			FlipbookGroupsListBox->AddSlot()
			.AutoHeight()
			.Padding(8, 12)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("TagLensEmpty", "No animations yet — add flipbooks to see tag-group sections."))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			];
		}

		TriggerPendingRenameIfNeeded(FlipbookGroupFlipbookNameTexts);

		if (FlipbookGroupsScrollBox.IsValid() && SavedScrollOffset > 0.0f)
		{
			RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
				[this, SavedScrollOffset](double, float) -> EActiveTimerReturnType
				{
					if (FlipbookGroupsScrollBox.IsValid())
					{
						FlipbookGroupsScrollBox->SetScrollOffset(SavedScrollOffset);
					}
					return EActiveTimerReturnType::Stop;
				}));
		}
		return;
	}

	TSet<FName>& CollapsedFlipbookGroups = Model->GetCollapsedFlipbookGroupsMutable();
	TSet<FName> ValidGroupNames;
	// The Ungrouped section persists its collapsed state under this sentinel name, which is
	// never a real FlipbookGroups entry. Exempt it so the stale-group purge below doesn't wipe it.
	ValidGroupNames.Add(FName("__Ungrouped"));
	for (const FFlipbookGroupInfo& Group : Asset->FlipbookGroups)
	{
		ValidGroupNames.Add(Group.GroupName);
	}
	TSet<FName> StaleNames;
	for (const FName& Name : CollapsedFlipbookGroups)
	{
		if (!ValidGroupNames.Contains(Name))
		{
			StaleNames.Add(Name);
		}
	}
	for (const FName& Name : StaleNames)
	{
		CollapsedFlipbookGroups.Remove(Name);
	}

	TMap<FName, TArray<const FFlipbookGroupInfo*>> Tree = Asset->GetFlipbookGroupTree();
	const TMap<FString, int32> FlipbookNameUsageCounts = BuildFlipbookNameUsageCounts(Asset.Get());

	TMap<FName, TArray<int32>> FlipbooksByGroup;
	TArray<int32> SortedIndices = GetSortedFlipbookIndices();
	for (int32 i : SortedIndices)
	{
		FlipbooksByGroup.FindOrAdd(Asset->Flipbooks[i].FlipbookGroup).Add(i);
	}

	FlipbookGroupsListBox->AddSlot()
	.AutoHeight()
	.Padding(0, 0, 0, 4)
	[
		BuildGroupSection(nullptr, NAME_None, 0, Tree, FlipbooksByGroup, FlipbookNameUsageCounts)
	];

	if (const TArray<const FFlipbookGroupInfo*>* RootGroups = Tree.Find(NAME_None))
	{
		for (const FFlipbookGroupInfo* GroupInfo : *RootGroups)
		{
			FlipbookGroupsListBox->AddSlot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				BuildGroupSection(GroupInfo, GroupInfo->GroupName, 0, Tree, FlipbooksByGroup, FlipbookNameUsageCounts)
			];
		}
	}

	if (PendingRenameFlipbookGroup != NAME_None)
	{
		FName PendingName = PendingRenameFlipbookGroup;
		PendingRenameFlipbookGroup = NAME_None;
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
			[this, PendingName](double, float) -> EActiveTimerReturnType
			{
				if (TSharedPtr<SInlineEditableTextBlock>* FoundText = FlipbookGroupNameTexts.Find(PendingName))
				{
					if (FoundText->IsValid())
					{
						(*FoundText)->EnterEditingMode();
					}
				}
				return EActiveTimerReturnType::Stop;
			}));
	}

	TriggerPendingRenameIfNeeded(FlipbookGroupFlipbookNameTexts);

	FName PendingScrollToGroup = Model->GetPendingScrollToGroup();
	if (PendingScrollToGroup != NAME_None)
	{
		FName ScrollTarget = PendingScrollToGroup;
		Model->SetPendingScrollToGroup(NAME_None);
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
			[this, ScrollTarget](double, float) -> EActiveTimerReturnType
			{
				if (FlipbookGroupsScrollBox.IsValid())
				{
					if (TSharedPtr<SInlineEditableTextBlock>* FoundText = FlipbookGroupNameTexts.Find(ScrollTarget))
					{
						if (FoundText->IsValid())
						{
							FlipbookGroupsScrollBox->ScrollDescendantIntoView((*FoundText).ToSharedRef(), true);
						}
					}
				}
				return EActiveTimerReturnType::Stop;
			}));
	}
	else if (FlipbookGroupsScrollBox.IsValid() && SavedScrollOffset > 0.0f)
	{
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
			[this, SavedScrollOffset](double, float) -> EActiveTimerReturnType
			{
				if (FlipbookGroupsScrollBox.IsValid())
				{
					FlipbookGroupsScrollBox->SetScrollOffset(SavedScrollOffset);
				}
				return EActiveTimerReturnType::Stop;
			}));
	}
}

// ==========================================
// GROUP SECTION RENDERING
// ==========================================

TSharedRef<SWidget> SFlipbookBrowserPanel::BuildGroupSection(
	const FFlipbookGroupInfo* GroupInfo,
	FName GroupName,
	int32 NestLevel,
	const TMap<FName, TArray<const FFlipbookGroupInfo*>>& Tree,
	const TMap<FName, TArray<int32>>& FlipbooksByGroup,
	const TMap<FString, int32>& FlipbookNameUsageCounts)
{
	if (!Model.IsValid()) return SNullWidget::NullWidget;

	const TArray<int32>* FlipbookIndices = FlipbooksByGroup.Find(GroupName);
	int32 FlipbookCount = FlipbookIndices ? FlipbookIndices->Num() : 0;
	int32 GroupIssueCount = 0;
	if (FlipbookIndices && Asset.IsValid())
	{
		for (int32 FlipbookIdx : *FlipbookIndices)
		{
			if (Asset->Flipbooks.IsValidIndex(FlipbookIdx)
				&& GetFlipbookValidationIssueCount(Asset->Flipbooks[FlipbookIdx], FlipbookNameUsageCounts) > 0)
			{
				++GroupIssueCount;
			}
		}
	}

	TArray<int32> FilteredIndices;
	if (FlipbookIndices)
	{
		for (int32 Idx : *FlipbookIndices)
		{
			if (PassesFlipbookGroupSearch(Asset->Flipbooks[Idx]))
			{
				FilteredIndices.Add(Idx);
			}
		}
	}

	const FString& SearchText = Model->GetFlipbookGroupSearchText();
	const int32 CompletionFilterMask = Model->GetCompletionFilterMask();
	const bool bHasActiveFilter = !SearchText.IsEmpty() || CompletionFilterMask != 0;
	bool bHasMatchingChildren = FilteredIndices.Num() > 0;
	if (!bHasMatchingChildren && bHasActiveFilter)
	{
		TArray<FName> GroupsToCheck;
		GroupsToCheck.Add(GroupName);
		while (GroupsToCheck.Num() > 0 && !bHasMatchingChildren)
		{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
			FName CheckGroup = GroupsToCheck.Pop(EAllowShrinking::No);
#else
			FName CheckGroup = GroupsToCheck.Pop(false);
#endif
			if (const TArray<const FFlipbookGroupInfo*>* ChildGroups = Tree.Find(CheckGroup))
			{
				for (const FFlipbookGroupInfo* ChildGroup : *ChildGroups)
				{
					const TArray<int32>* ChildFlipbooks = FlipbooksByGroup.Find(ChildGroup->GroupName);
					if (ChildFlipbooks)
					{
						for (int32 Idx : *ChildFlipbooks)
						{
							if (PassesFlipbookGroupSearch(Asset->Flipbooks[Idx]))
							{
								bHasMatchingChildren = true;
								break;
							}
						}
					}
					if (bHasMatchingChildren) break;
					GroupsToCheck.Add(ChildGroup->GroupName);
				}
			}
		}

		if (!bHasMatchingChildren && !SearchText.IsEmpty() && GroupName != NAME_None)
		{
			bHasMatchingChildren = GroupName.ToString().Contains(SearchText, ESearchCase::IgnoreCase);
		}
	}

	if (bHasActiveFilter && !bHasMatchingChildren)
	{
		return SNullWidget::NullWidget;
	}

	TSharedRef<SVerticalBox> BodyContent = SNew(SVerticalBox);

	const TSet<FName>& CollapsedFlipbookGroups = Model->GetCollapsedFlipbookGroups();
	const bool bFlipbookGroupGridView = Model->IsFlipbookGroupGridView();

	if (FilteredIndices.Num() > 0)
	{
		if (bFlipbookGroupGridView)
		{
			TSharedRef<SWrapBox> WrapBox = SNew(SWrapBox).UseAllottedSize(true);
			for (int32 FlipbookIdx : FilteredIndices)
			{
				WrapBox->AddSlot().Padding(4)[ BuildFlipbookCard(FlipbookIdx, FlipbookNameUsageCounts) ];
			}
			BodyContent->AddSlot().AutoHeight().Padding(4)[ WrapBox ];
		}
		else
		{
			for (int32 FlipbookIdx : FilteredIndices)
			{
				const FFlipbookProfileEntry& FBData = Asset->Flipbooks[FlipbookIdx];
				UPaperFlipbook* RowFlipbook = !FBData.Identity.Flipbook.IsNull() ? FBData.Identity.Flipbook.LoadSynchronous() : nullptr;
				FString ValidationTooltip;
				const int32 ValidationIssueCount = GetFlipbookValidationIssueCount(FBData, FlipbookNameUsageCounts, &ValidationTooltip);
				const FText ValidationTooltipText = ValidationTooltip.IsEmpty() ? FText::GetEmpty() : FText::FromString(ValidationTooltip);
				TSharedPtr<SInlineEditableTextBlock> RowNameText;

				auto IsRowSelected = [this, FlipbookIdx]()
				{
					if (!Model.IsValid()) return false;
					return FlipbookIdx == Model->GetSelectedFlipbookIndex() || Model->GetSelectedFlipbookCards().Contains(FlipbookIdx);
				};

				// WS3 U5: an ADDITIVE full-height left bar tinted to this flipbook's descriptive phase
				// (EditorMeta.PhaseTag) via THE shared GetPhaseTagBadge — same colors as the Map pills and
				// the built-in phase slots. Hidden (zero alpha) when the flipbook carries no phase tag, so
				// untagged rows look exactly as before. Built statically; a phase edit rebuilds the list.
				const Paper2DPlusAnimationMap::FPhaseTagBadge RowPhaseBadge =
					Paper2DPlusAnimationMap::GetPhaseTagBadge(FBData.EditorMeta.PhaseTag);
				const FText RowPhaseTooltip = RowPhaseBadge.IsValid()
					? FText::Format(LOCTEXT("RowPhaseTip", "Phase: {0}"), FText::FromString(RowPhaseBadge.Label))
					: FText::GetEmpty();

				TSharedRef<SWidget> RowContent = SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor_Lambda([IsRowSelected]() -> FSlateColor { return IsRowSelected() ? FLinearColor(0.18f, 0.30f, 0.50f, 1.0f) : FLinearColor(0.0f, 0.0f, 0.0f, 0.0f); })
					.Padding(1)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
						.BorderBackgroundColor_Lambda([IsRowSelected]() -> FSlateColor { return IsRowSelected() ? FLinearColor(0.18f, 0.30f, 0.50f, 0.92f) : FLinearColor(0.1f, 0.1f, 0.1f, 0.5f); })
						.Padding(4)
						[
							SNew(SHorizontalBox)
							// Phase tint bar (U5) — full-height, 4px, additive on the left edge.
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill).Padding(0, 0, 6, 0)
							[
								SNew(SBox).WidthOverride(4.0f)
								.ToolTipText(RowPhaseTooltip)
								[
									SNew(SBorder)
									.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
									.BorderBackgroundColor(RowPhaseBadge.IsValid()
										? FSlateColor(RowPhaseBadge.Color)
										: FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f)))
									.Padding(0)
								]
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
							[
								SNew(SBox).WidthOverride(32).HeightOverride(32)
								[
									SNew(SBorder).BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
									[
										RowFlipbook
											? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(RowFlipbook))
											: StaticCastSharedRef<SWidget>(SNew(SBorder)
												.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
												.HAlign(HAlign_Center).VAlign(VAlign_Center)
												[ SNew(STextBlock).Text(LOCTEXT("MissingFlipbookRow", "No FB")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 8)).ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f))) ])
									]
								]
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SAssignNew(RowNameText, SInlineEditableTextBlock)
								.Text(FText::FromString(FBData.Identity.FlipbookName))
								.OnTextCommitted_Lambda([this, FlipbookIdx](const FText& NewText, ETextCommit::Type CommitType)
								{
									if (CommitType != ETextCommit::OnCleared)
									{
										CommitFlipbookRename(FlipbookIdx, NewText.ToString());
									}
								})
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::AsNumber(FBData.CombatData.Frames.Num())).ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f))) ]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
							[
								SNew(SBorder)
								.Visibility(ValidationIssueCount > 0 ? EVisibility::Visible : EVisibility::Collapsed)
								.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
								.BorderBackgroundColor(FLinearColor(0.8f, 0.45f, 0.05f, 0.95f))
								.Padding(FMargin(4, 1))
								.ToolTipText(ValidationTooltipText)
								[ SNew(STextBlock).Text(FText::Format(LOCTEXT("FlipbookIssueBadgeCompact", "! {0}"), FText::AsNumber(ValidationIssueCount))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 8)).ColorAndOpacity(FSlateColor(FLinearColor::White)) ]
							]
						]
					];

				TSharedRef<SDragClickWrapper> RowWrapper = SNew(SDragClickWrapper)[ RowContent ];
				RowWrapper->OnClickedFunc = [this, FlipbookIdx](const FGeometry&, const FPointerEvent& MouseEvent) { OnFlipbookGroupCardClicked(FlipbookIdx, MouseEvent); };
				RowWrapper->OnRightClickedFunc = [this, FlipbookIdx](const FGeometry&, const FPointerEvent&)
				{
					if (!Model->GetSelectedFlipbookCards().Contains(FlipbookIdx))
					{
						Model->GetSelectedFlipbookCardsMutable().Empty();
						Model->GetSelectedFlipbookCardsMutable().Add(FlipbookIdx);
						Model->SetSelectionAnchorIndex(FlipbookIdx);
					}
					Model->SetSelectedFlipbook(FlipbookIdx);
					Invalidate(EInvalidateWidgetReason::Paint);
					OnShowFlipbookContextMenu.ExecuteIfBound(FlipbookIdx);
				};
				RowWrapper->OnDragDetectedFunc = [this, FlipbookIdx]() -> TSharedPtr<FDragDropOperation>
				{
					TArray<int32> DragIndices;
					if (Model->GetSelectedFlipbookCards().Contains(FlipbookIdx) && Model->GetSelectedFlipbookCards().Num() > 1)
					{
						DragIndices = Model->GetSelectedFlipbookCards().Array();
					}
					else
					{
						DragIndices.Add(FlipbookIdx);
					}
					FName FromGroup = NAME_None;
					if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FlipbookIdx))
					{
						FromGroup = Asset->Flipbooks[FlipbookIdx].FlipbookGroup;
					}
					return FFlipbookGroupDragDropOp::NewFromCardDrag(DragIndices, FromGroup, Asset.Get());
				};
				if (RowNameText.IsValid()) FlipbookGroupFlipbookNameTexts.Add(FlipbookIdx, RowNameText);
				FlipbookGroupWidgetMap.Add(FlipbookIdx, RowWrapper);

				// U12: keep the existing click/group-drag wrapper INSIDE the assignment target so
				// selection, context menus, and My Groups reorder retain their current gesture owner.
				BodyContent->AddSlot().AutoHeight().Padding(2, 1)
				[
					SNew(SExpectedTagAnimationDropTarget)
					.Target(FExpectedTagAnimationTarget::Capture(Asset.Get(), FlipbookIdx))
					.Model(Model)
					[
						RowWrapper
					]
				];
			}
		}
	}

	if (GroupInfo != nullptr)
	{
		if (const TArray<const FFlipbookGroupInfo*>* ChildGroups = Tree.Find(GroupName))
		{
			for (const FFlipbookGroupInfo* ChildGroup : *ChildGroups)
			{
				BodyContent->AddSlot().AutoHeight().Padding(0, 2)
				[ BuildGroupSection(ChildGroup, ChildGroup->GroupName, NestLevel + 1, Tree, FlipbooksByGroup, FlipbookNameUsageCounts) ];
			}
		}
	}

	if (GroupName == NAME_None)
	{
		TSharedRef<SFlipbookGroupDropTarget> UngroupedDropTarget = SNew(SFlipbookGroupDropTarget)
			[
				SNew(SExpandableArea)
				.AllowAnimatedTransition(false)
				.InitiallyCollapsed(!bHasActiveFilter && CollapsedFlipbookGroups.Contains(FName("__Ungrouped")))
				.OnAreaExpansionChanged_Lambda([this](bool bExpanded)
				{
					if (bExpanded)
						Model->GetCollapsedFlipbookGroupsMutable().Remove(FName("__Ungrouped"));
					else
						Model->GetCollapsedFlipbookGroupsMutable().Add(FName("__Ungrouped"));
				})
				.HeaderContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4, 0)
					[
						SNew(STextBlock).Text(LOCTEXT("Ungrouped", "Ungrouped"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
					[
						SNew(SBorder).BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed")).Padding(FMargin(6, 2))
						[ SNew(STextBlock).Text(FText::AsNumber(FilteredIndices.Num())).TextStyle(FAppStyle::Get(), "SmallText") ]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
					[
						SNew(SBorder)
						.Visibility(GroupIssueCount > 0 ? EVisibility::Visible : EVisibility::Collapsed)
						.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(FLinearColor(0.8f, 0.45f, 0.05f, 0.95f))
						.Padding(FMargin(6, 2))
						.ToolTipText(LOCTEXT("UngroupedIssueTooltip", "Flipbooks in this section need attention"))
						[ SNew(STextBlock).Text(FText::Format(LOCTEXT("UngroupedIssueCount", "! {0}"), FText::AsNumber(GroupIssueCount))).TextStyle(FAppStyle::Get(), "SmallText").ColorAndOpacity(FSlateColor(FLinearColor::White)) ]
					]
				]
				.BodyContent()[ BodyContent ]
			];
		UngroupedDropTarget->TargetGroup = NAME_None;
		UngroupedDropTarget->OnDropFunc = [this](const TArray<int32>& Indices, FName TargetGroup) { OnFlipbookGroupFlipbooksDrop(Indices, TargetGroup); };
		UngroupedDropTarget->OnGroupDropFunc = [this](FName SourceGroupName, FName TargetParentGroup) { OnGroupDrop(SourceGroupName, TargetParentGroup); };
		return SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[ UngroupedDropTarget ];
	}

	check(GroupInfo != nullptr);

	TSharedPtr<SInlineEditableTextBlock> GroupNameText;
	TSharedPtr<SGroupDragHandle> GroupDragHandle;

	TSharedRef<SFlipbookGroupDropTarget> NamedDropTarget = SNew(SFlipbookGroupDropTarget)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[ SNew(SSpacer).Size(FVector2D(FMath::Min(NestLevel * 16.0f, 80.0f), 0.0f)) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SExpandableArea)
				.AllowAnimatedTransition(false)
				.InitiallyCollapsed(!bHasActiveFilter && CollapsedFlipbookGroups.Contains(GroupName))
				.OnAreaExpansionChanged_Lambda([this, GroupName](bool bExpanded)
				{
					if (bExpanded)
						Model->GetCollapsedFlipbookGroupsMutable().Remove(GroupName);
					else
						Model->GetCollapsedFlipbookGroupsMutable().Add(GroupName);
				})
				.HeaderContent()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
					.OnMouseButtonDown_Lambda([this, GroupName](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
					{
						if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
						{
							ShowFlipbookGroupContextMenu(GroupName, MouseEvent.GetScreenSpacePosition());
							return FReply::Handled();
						}
						return FReply::Unhandled();
					})
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0)
						[ SAssignNew(GroupDragHandle, SGroupDragHandle).GroupColor(GroupInfo->Color) ]
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4, 0)
						[
							SAssignNew(GroupNameText, SInlineEditableTextBlock)
							.Text(FText::FromName(GroupInfo->GroupName))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
							.OnVerifyTextChanged_Lambda([this, GroupName](const FText& InText, FText& OutErrorMessage) -> bool
							{
								return OnVerifyFlipbookGroupNameChanged(InText, OutErrorMessage, GroupName);
							})
							.OnTextCommitted_Lambda([this, GroupName](const FText& InText, ETextCommit::Type CommitType)
							{
								OnFlipbookGroupNameCommitted(InText, CommitType, GroupName);
							})
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0)
						[
							SNew(SBorder).BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed")).Padding(FMargin(6, 2))
							[ SNew(STextBlock).Text(FText::AsNumber(FlipbookCount)).TextStyle(FAppStyle::Get(), "SmallText") ]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
						[
							SNew(SBorder)
							.Visibility(GroupIssueCount > 0 ? EVisibility::Visible : EVisibility::Collapsed)
							.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
							.BorderBackgroundColor(FLinearColor(0.8f, 0.45f, 0.05f, 0.95f))
							.Padding(FMargin(6, 2))
							.ToolTipText(LOCTEXT("GroupIssueTooltip", "Flipbooks in this group need attention"))
							[ SNew(STextBlock).Text(FText::Format(LOCTEXT("GroupIssueCount", "! {0}"), FText::AsNumber(GroupIssueCount))).TextStyle(FAppStyle::Get(), "SmallText").ColorAndOpacity(FSlateColor(FLinearColor::White)) ]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
						[
							SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton").ContentPadding(FMargin(1))
							.ToolTipText(LOCTEXT("DeleteGroupTip", "Delete this group"))
							.OnClicked_Lambda([this, GroupName]() { DeleteFlipbookGroup(GroupName); return FReply::Handled(); })
							[
								SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.Delete"))
								.DesiredSizeOverride(FVector2D(12, 12))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
							]
						]
					]
				]
				.BodyContent()[ BodyContent ]
			]
		];

	NamedDropTarget->TargetGroup = GroupName;
	NamedDropTarget->OnDropFunc = [this](const TArray<int32>& Indices, FName TargetGroup) { OnFlipbookGroupFlipbooksDrop(Indices, TargetGroup); };
	NamedDropTarget->OnGroupDropFunc = [this](FName SourceGroupName, FName TargetParentGroup) { OnGroupDrop(SourceGroupName, TargetParentGroup); };

	if (GroupDragHandle.IsValid()) GroupDragHandle->GroupName = GroupName;
	if (GroupNameText.IsValid()) FlipbookGroupNameTexts.Add(GroupName, GroupNameText);

	return StaticCastSharedRef<SWidget>(NamedDropTarget);
}

// ==========================================
// FLIPBOOK CARD
// ==========================================

TSharedRef<SWidget> SFlipbookBrowserPanel::BuildFlipbookCard(int32 FlipbookIndex, const TMap<FString, int32>& FlipbookNameUsageCounts)
{
	if (!Asset.IsValid() || !Asset->Flipbooks.IsValidIndex(FlipbookIndex) || !Model.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	const FFlipbookProfileEntry& FBData = Asset->Flipbooks[FlipbookIndex];
	UPaperFlipbook* LoadedFlipbook = !FBData.Identity.Flipbook.IsNull() ? FBData.Identity.Flipbook.LoadSynchronous() : nullptr;
	FString ValidationTooltip;
	const int32 ValidationIssueCount = GetFlipbookValidationIssueCount(FBData, FlipbookNameUsageCounts, &ValidationTooltip);
	const FText ValidationTooltipText = ValidationTooltip.IsEmpty() ? FText::GetEmpty() : FText::FromString(ValidationTooltip);
	const FGameplayTag CardHomeTag = GetHomeAnimationTag(FBData.Identity.FlipbookName);   // inline Animation-tag chip (P4)
	const FGameplayTag CardPhaseTag = FBData.EditorMeta.PhaseTag;                          // inline Phase-tag chip (P4)
	// TASK-108 U6 (R6): provenance-separated animation tags from the per-refresh batch cache (own =
	// solid, chain-inherited = ghosted, group-implied = outlined; "+N" overflow).
	// Find (pointer) + empty sentinel, NOT FindRef: FindRef copies the whole FAnimationTagSet (four
	// containers + an array) per card per refresh. Reference is safe — the map lives on the panel and
	// the one lambda using CardTagSet runs synchronously during this card's build.
	static const Paper2DPlusAnimationTagQuery::FAnimationTagSet EmptyCardTagSet;
	const Paper2DPlusAnimationTagQuery::FAnimationTagSet* FoundCardTagSet =
		CachedAnimationTagMap.Find(FBData.Identity.FlipbookName.ToLower());
	const Paper2DPlusAnimationTagQuery::FAnimationTagSet& CardTagSet =
		FoundCardTagSet ? *FoundCardTagSet : EmptyCardTagSet;
	const TArray<Paper2DPlusAnimationTagChips::FAnimationTagChipItem> CardChipItems =
		Paper2DPlusAnimationTagChips::BuildChipItems(CardTagSet.OwnTags, CardTagSet.ChainInheritedTags,
			CardTagSet.GroupImpliedTags);
	TSharedPtr<SInlineEditableTextBlock> CardNameText;

	// Compact leaf-name tag chip for the narrow card. The full SGameplayTagCombo overflowed with long tag
	// PATHS (and the engine combo only shortens property-bound tags, not these .Tag-bound ones), so we show
	// just the leaf (e.g. "Locomotion"), tinted by the project Tag Colors registry (grey when unset), and
	// open the engine SGameplayTagPicker to edit. Snapshot at build; the commit rebuilds the card (5.3+;
	// older engines keep the plain-text fallback).
	// Leaf name via the ONE shared cross-version helper (AnimationTagChipUtils.h — 5.6+ GetTagLeafName
	// fast path, last-dot parse before); thin FText wrapper because the chip sites below want FText.
	auto CardTagLeafText = [](const FGameplayTag& InTag) -> FText
	{
		return FText::FromString(Paper2DPlusAnimationTagChips::GetTagLeafString(InTag));
	};
	auto MakeCardTagChip = [this, CardTagLeafText](const FGameplayTag CurrentTag, const FString& Filter,
		TFunction<void(FGameplayTag)> OnPicked) -> TSharedRef<SWidget>
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
		bool bColored = false;
		const FLinearColor RegColor = UPaper2DPlusSettings::ResolveTagColor(CurrentTag, bColored);
		const FSlateColor ChipBg = bColored
			? FSlateColor(FLinearColor(RegColor.R, RegColor.G, RegColor.B, 0.7f))
			: FSlateColor(FLinearColor(0.16f, 0.16f, 0.16f, 1.0f));
		FSlateColor ChipText = FSlateColor(FLinearColor(0.82f, 0.82f, 0.82f));
		if (bColored)
		{
			const float Lum = 0.2126f * RegColor.R + 0.7152f * RegColor.G + 0.0722f * RegColor.B;
			ChipText = FSlateColor(Lum > 0.5f ? FLinearColor(0.05f, 0.05f, 0.05f) : FLinearColor(0.97f, 0.97f, 0.97f));
		}
		const FText LeafText = CurrentTag.IsValid()
			? CardTagLeafText(CurrentTag)
			: LOCTEXT("CardTagNone", "(none)");

		// Rounded pill to match the engine's gameplay-tag chip look (FSlateRoundedBoxBrush radius 3, see
		// GameplayTagStyle.cpp) — a sharp WhiteBrush reads as an out-of-place rectangle. White fill is
		// tinted by BorderBackgroundColor below.
		static const FSlateRoundedBoxBrush CardTagChipBrush(FLinearColor::White, 3.0f);

		// Holder lets the menu's OnTagChanged close the combo without capturing it before it exists.
		TSharedRef<TWeakPtr<SComboButton>> WeakComboHolder = MakeShared<TWeakPtr<SComboButton>>();
		// Weak panel guard for the DEFERRED commit below: OnPicked's call-site lambdas capture the raw
		// panel `this` (CommitCardAnimationTag/CommitCardPhaseTag), and the global next-tick timer outlives
		// a panel whose editor closes before the tick — pin-or-drop (the compounded weak-capture rule).
		TWeakPtr<SWidget> WeakPanel = AsShared();
		TSharedRef<SComboButton> Combo = SNew(SComboButton)
			.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
			.ContentPadding(FMargin(1.f, 0.f))
			.HasDownArrow(true)
			.ToolTipText(CurrentTag.IsValid() ? FText::FromName(CurrentTag.GetTagName()) : LOCTEXT("CardTagNoneTip", "No tag — click to assign"))
			.OnGetMenuContent_Lambda([Filter, CurrentTag, OnPicked, WeakComboHolder, WeakPanel]() -> TSharedRef<SWidget>
			{
				return SNew(SBox)
					.MinDesiredWidth(300.f)
					.Padding(2.f)
					[
						// Menu-hosted picker = selection-only: the guard eats right-clicks so the
						// engine row context menu can't crash the menu stack (see the class doc).
						SNew(SMenuHostedTagPickerGuard)
						[
						SNew(SGameplayTagPicker)
						.Filter(Filter)
						.MultiSelect(false)
						.TagContainers(TArray<FGameplayTagContainer>{ FGameplayTagContainer(CurrentTag) })
						.OnTagChanged_Lambda([OnPicked, WeakComboHolder, WeakPanel](const TArray<FGameplayTagContainer>& Containers)
						{
							const FGameplayTag NewTag = Containers.Num() > 0 ? Containers[0].First() : FGameplayTag();
							if (TSharedPtr<SComboButton> Cb = WeakComboHolder->Pin()) { Cb->SetIsOpen(false); }
							// DEFER the commit one tick: it rebuilds the card synchronously, which would destroy
							// this combo + its open picker WHILE the picker's OnTagChanged is still on the stack
							// (a reentrancy crash). Next-tick lets the picker callback fully unwind first.
							// The tick only fires OnPicked while the PANEL is still alive (WeakPanel) — the
							// raw `this` inside OnPicked is otherwise a use-after-free on close-before-tick.
							if (GEditor)
							{
								GEditor->GetTimerManager()->SetTimerForNextTick([OnPicked, NewTag, WeakPanel]()
								{
									if (WeakPanel.IsValid())
									{
										OnPicked(NewTag);
									}
								});
							}
							else if (WeakPanel.IsValid())
							{
								OnPicked(NewTag);
							}
						})
						]
					];
			})
			.ButtonContent()
			[
				SNew(SBorder)
				.BorderImage(&CardTagChipBrush)
				.BorderBackgroundColor(ChipBg)
				.Padding(FMargin(6.f, 1.f))
				.VAlign(VAlign_Center)
				.Clipping(EWidgetClipping::ClipToBounds)
				[
					SNew(STextBlock)
					.Text(LeafText)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
					.ColorAndOpacity(ChipText)
				]
			];
		*WeakComboHolder = Combo;
		return Combo;
#else
		return SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
			.Text(CurrentTag.IsValid() ? CardTagLeafText(CurrentTag) : LOCTEXT("CardTagNone", "(none)"));
#endif
	};

	// TASK-108 U6 (R6): the "Anims" chips row's EDIT affordance — the shared chips row as the content
	// of a borderless SComboButton whose menu is a MULTI-select SGameplayTagPicker over the entry's
	// OWN AnimationTags (inherited/group chips are display-only provenance; authoring is own-tags).
	// Same mechanics as MakeCardTagChip: SimpleComboButton, deferred one-tick commit (the commit
	// rebuilds the card synchronously — the picker callback must fully unwind first), weak-panel
	// pin-or-drop guard. Pre-5.3 (no public SGameplayTagPicker): the chips render read-only.
	auto MakeCardAnimTagsEditor = [this, FlipbookIndex, &CardTagSet, &CardChipItems]() -> TSharedRef<SWidget>
	{
		// MaxVisible=2 on the 150px card (ECABridge-calibrated): the "Anims" label + 3 chips overran
		// the card width and ClipToBounds swallowed the "+N" overflow chip — 2 chips + "+N" always fit.
		// (The Map node is wider and keeps 3.)
		TSharedRef<SWidget> ChipsRow = CardChipItems.Num() > 0
			? Paper2DPlusAnimationTagChips::MakeChipsRow(CardChipItems, /*MaxVisible=*/2)
			: StaticCastSharedRef<SWidget>(SNew(STextBlock)
				.Text(LOCTEXT("CardAnimTagsNone", "(none)"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f))));

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
		const FGameplayTagContainer OwnTagsSnapshot = CardTagSet.OwnTags; // snapshot-at-build (card rebuilds on commit)
		TSharedRef<TWeakPtr<SComboButton>> WeakComboHolder = MakeShared<TWeakPtr<SComboButton>>();
		TWeakPtr<SWidget> WeakPanel = AsShared();
		TSharedRef<SComboButton> Combo = SNew(SComboButton)
			.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
			.ContentPadding(FMargin(1.f, 0.f))
			.HasDownArrow(true)
			.ToolTipText(LOCTEXT("CardAnimTagsTip", "Animation category tags (Paper2DPlus.Animation). Solid = own tag, ghosted = inherited from a chain root, outlined = implied by the tag-mapping group. Click to edit the OWN tags (multi-select)."))
			.OnGetMenuContent_Lambda([this, FlipbookIndex, OwnTagsSnapshot, WeakComboHolder, WeakPanel]() -> TSharedRef<SWidget>
			{
				return SNew(SBox)
					.MinDesiredWidth(300.f)
					.Padding(2.f)
					[
						// Menu-hosted picker = selection-only (right-clicks eaten; see the guard doc).
						SNew(SMenuHostedTagPickerGuard)
						[
						SNew(SGameplayTagPicker)
						.Filter(TEXT("Paper2DPlus.Animation"))
						.MultiSelect(true)
						.TagContainers(TArray<FGameplayTagContainer>{ OwnTagsSnapshot })
						.OnTagChanged_Lambda([this, FlipbookIndex, WeakComboHolder, WeakPanel](const TArray<FGameplayTagContainer>& Containers)
						{
							const FGameplayTagContainer NewTags = Containers.Num() > 0 ? Containers[0] : FGameplayTagContainer();
							if (TSharedPtr<SComboButton> Cb = WeakComboHolder->Pin()) { Cb->SetIsOpen(false); }
							// DEFER one tick (the MakeCardTagChip reentrancy rule): the commit's refresh
							// destroys this combo + its open picker; unwind the picker callback first.
							if (GEditor)
							{
								GEditor->GetTimerManager()->SetTimerForNextTick([this, FlipbookIndex, NewTags, WeakPanel]()
								{
									if (WeakPanel.IsValid())
									{
										CommitCardAnimationTags(FlipbookIndex, NewTags);
									}
								});
							}
							else if (WeakPanel.IsValid())
							{
								CommitCardAnimationTags(FlipbookIndex, NewTags);
							}
						})
						]
					];
			})
			.ButtonContent()
			[
				ChipsRow
			];
		*WeakComboHolder = Combo;
		return Combo;
#else
		return ChipsRow; // read-only chips on pre-5.3 engines (no public picker)
#endif
	};

	auto IsCardSelected = [this, FlipbookIndex]()
	{
		if (!Model.IsValid()) return false;
		return FlipbookIndex == Model->GetSelectedFlipbookIndex() || Model->GetSelectedFlipbookCards().Contains(FlipbookIndex);
	};

	TSharedRef<SWidget> CardContent = SNew(SBox)
		.WidthOverride(SProfileCard::GetCanonicalCardWidth())
		.HeightOverride(192.f) // +18 for the U6 "Anims" chips row (was 174)
		[
			SNew(SProfileCard)
			.IsSelected_Lambda([IsCardSelected]() { return IsCardSelected(); })
			[
				SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 4)
					[
						SNew(SBox)
						.WidthOverride(SProfileCard::GetCanonicalThumbnailSize())
						.HeightOverride(SProfileCard::GetCanonicalThumbnailSize())
						[
							SNew(SBorder).BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
							[
								LoadedFlipbook
									? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook))
									: StaticCastSharedRef<SWidget>(SNew(SBorder)
										.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
										.HAlign(HAlign_Center).VAlign(VAlign_Center)
										[ SNew(STextBlock).Text(LOCTEXT("NoFlipbook", "No FB")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 8)).ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f))) ])
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(2, 0)
					[
						SAssignNew(CardNameText, SInlineEditableTextBlock)
						.Text(FText::FromString(FBData.Identity.FlipbookName))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.Justification(ETextJustify::Center)
						.OnTextCommitted_Lambda([this, FlipbookIndex](const FText& NewText, ETextCommit::Type CommitType)
						{
							if (CommitType != ETextCommit::OnCleared)
							{
								CommitFlipbookRename(FlipbookIndex, NewText.ToString());
							}
						})
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text_Lambda([this, FlipbookIndex]()
							{
								if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FlipbookIndex))
								{
									const FFlipbookProfileEntry& FB = Asset->Flipbooks[FlipbookIndex];
									if (FB.FrameEventData.FrameCues.Num() > 0)
									{
										return FText::Format(LOCTEXT("FrameCountWithFX", "{0}f  FX:{1}"),
											FText::AsNumber(FB.CombatData.Frames.Num()), FText::AsNumber(FB.FrameEventData.FrameCues.Num()));
									}
									return FText::Format(LOCTEXT("FrameCountBadge", "{0} frames"),
										FText::AsNumber(FB.CombatData.Frames.Num()));
								}
								return FText::GetEmpty();
							})
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0).VAlign(VAlign_Center)
						[
							SNew(SBorder)
							.Visibility(ValidationIssueCount > 0 ? EVisibility::Visible : EVisibility::Collapsed)
							.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
							.BorderBackgroundColor(FLinearColor(0.8f, 0.45f, 0.05f, 0.95f))
							.Padding(FMargin(4, 1))
							.ToolTipText(ValidationTooltipText)
							[ SNew(STextBlock).Text(FText::Format(LOCTEXT("FlipbookIssueBadge", "! {0}"), FText::AsNumber(ValidationIssueCount))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 7)).ColorAndOpacity(FSlateColor(FLinearColor::White)) ]
						]
					]

					// Inline Animation-tag chip (P4): re-home to a (possibly deeper) Paper2DPlus.Animation tag.
					+ SVerticalBox::Slot().AutoHeight().Padding(2, 4, 2, 0)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 3, 0)
						[
							SNew(STextBlock).Text(LOCTEXT("CardRowTagLabel", "Tag")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.45f, 0.5f)))
							.ToolTipText(LOCTEXT("CardRowTagTip", "Animation tag group (Paper2DPlus.Animation). Pick a deeper tag to re-home this flipbook — one home group per flipbook; clearing removes it."))
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							MakeCardTagChip(CardHomeTag, TEXT("Paper2DPlus.Animation"),
								[this, FlipbookIndex, CardHomeTag](FGameplayTag NewTag)
								{
									CommitCardAnimationTag(FlipbookIndex, CardHomeTag, NewTag);
								})
						]
					]

					// Inline Phase-tag chip (P4): the descriptive EditorMeta.PhaseTag.
					+ SVerticalBox::Slot().AutoHeight().Padding(2, 2, 2, 0)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 3, 0)
						[
							SNew(STextBlock).Text(LOCTEXT("CardRowPhaseLabel", "Phase")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.45f, 0.5f)))
							.ToolTipText(LOCTEXT("CardRowPhaseTip", "Descriptive animation-phase tag (Paper2DPlus.Phase) for this flipbook."))
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							MakeCardTagChip(CardPhaseTag, TEXT("Paper2DPlus.Phase"),
								[this, FlipbookIndex](FGameplayTag NewTag)
								{
									CommitCardPhaseTag(FlipbookIndex, NewTag);
								})
						]
					]

					// TASK-108 U6 (R6): the "Anims" category-tag chips row — provenance-styled (solid /
					// ghosted / outlined) with "+N" overflow; click opens the multi-select own-tags picker.
					+ SVerticalBox::Slot().AutoHeight().Padding(2, 2, 2, 0)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 3, 0)
						[
							SNew(STextBlock).Text(LOCTEXT("CardRowAnimTagsLabel", "Anims")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.45f, 0.5f)))
							.ToolTipText(LOCTEXT("CardRowAnimTagsTip", "Animation category tags (Paper2DPlus.Animation taxonomy). Solid = own, ghosted = chain-inherited, outlined = group-implied."))
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							MakeCardAnimTagsEditor()
						]
					]
			]
		];

	TSharedRef<SDragClickWrapper> Wrapper = SNew(SDragClickWrapper)[ CardContent ];
	Wrapper->OnClickedFunc = [this, FlipbookIndex](const FGeometry&, const FPointerEvent& MouseEvent) { OnFlipbookGroupCardClicked(FlipbookIndex, MouseEvent); };
	Wrapper->OnRightClickedFunc = [this, FlipbookIndex](const FGeometry&, const FPointerEvent&)
	{
		if (!Model->GetSelectedFlipbookCards().Contains(FlipbookIndex))
		{
			Model->GetSelectedFlipbookCardsMutable().Empty();
			Model->GetSelectedFlipbookCardsMutable().Add(FlipbookIndex);
			Model->SetSelectionAnchorIndex(FlipbookIndex);
		}
		Model->SetSelectedFlipbook(FlipbookIndex);
		Invalidate(EInvalidateWidgetReason::Paint);
		OnShowFlipbookContextMenu.ExecuteIfBound(FlipbookIndex);
	};
	// TASK-108 U7 (R10): the "By Tag Group" lens is derived/read-only ordering — cards never start a
	// drag there (no drop targets exist either, but an un-droppable drag ghost would still read as
	// draggable). Selection, context menu, rename, and the pickers above stay live. My Groups keeps
	// the drag unchanged.
	if (!bByTagLens)
	{
		Wrapper->OnDragDetectedFunc = [this, FlipbookIndex]() -> TSharedPtr<FDragDropOperation>
		{
			TArray<int32> DragIndices;
			if (Model->GetSelectedFlipbookCards().Contains(FlipbookIndex) && Model->GetSelectedFlipbookCards().Num() > 1)
			{
				DragIndices = Model->GetSelectedFlipbookCards().Array();
			}
			else
			{
				DragIndices.Add(FlipbookIndex);
			}
			FName FromGroup = NAME_None;
			if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FlipbookIndex))
			{
				FromGroup = Asset->Flipbooks[FlipbookIndex].FlipbookGroup;
			}
			return FFlipbookGroupDragDropOp::NewFromCardDrag(DragIndices, FromGroup, Asset.Get());
		};
	}
	if (CardNameText.IsValid()) FlipbookGroupFlipbookNameTexts.Add(FlipbookIndex, CardNameText);
	FlipbookGroupWidgetMap.Add(FlipbookIndex, Wrapper);

	// U12: the assignment target is deliberately outside SDragClickWrapper. Card click/context/group
	// drag behavior stays on the inner wrapper, while both organization lenses accept Expected Tags.
	return SNew(SExpectedTagAnimationDropTarget)
		.Target(FExpectedTagAnimationTarget::Capture(Asset.Get(), FlipbookIndex))
		.Model(Model)
		[
			Wrapper
		];
}

// ==========================================
// "BY TAG GROUP" LENS SECTION (TASK-108 U7)
// ==========================================

TSharedRef<SWidget> SFlipbookBrowserPanel::BuildTagLensSectionWidget(
	const Paper2DPlusAnimationMap::FTagLensSection& Section,
	const TMap<FString, int32>& FlipbookNameUsageCounts)
{
	if (!Model.IsValid() || !Asset.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	// Compose with the search box + completion filter (R10): filtered members drop out of the
	// section; a section whose members ALL filter away hides while a filter is active (the My Groups
	// group behavior), and shows (possibly empty) otherwise.
	TArray<int32> FilteredIndices;
	for (int32 MemberIndex : Section.MemberIndices)
	{
		if (Asset->Flipbooks.IsValidIndex(MemberIndex) && PassesFlipbookGroupSearch(Asset->Flipbooks[MemberIndex]))
		{
			FilteredIndices.Add(MemberIndex);
		}
	}
	const bool bHasActiveFilter =
		!Model->GetFlipbookGroupSearchText().TrimStartAndEnd().IsEmpty() || Model->GetCompletionFilterMask() != 0;
	if (bHasActiveFilter && FilteredIndices.Num() == 0)
	{
		return SNullWidget::NullWidget;
	}

	// Section accent: the shared registry→convention resolution on the GROUP KEY (R11's single seam);
	// the invalid Unmapped tag resolves to the helper's neutral gray.
	const Paper2DPlusAnimationMap::FTagChipColor Accent =
		Paper2DPlusAnimationMap::GetAnimationTagChipColor(Section.GroupTag);

	// Cards in the helper's order — chain clusters first (root→finisher), then the alphabetical rest.
	// Each card leads with a 4px phase-tint bar (GetPhaseTagBadge — the List-row left-bar precedent;
	// transparent when untagged). BuildFlipbookCard suppresses drag-out while the lens is active.
	TSharedRef<SWrapBox> WrapBox = SNew(SWrapBox).UseAllottedSize(true);
	for (int32 FlipbookIndex : FilteredIndices)
	{
		const Paper2DPlusAnimationMap::FPhaseTagBadge PhaseBadge =
			Paper2DPlusAnimationMap::GetPhaseTagBadge(Asset->Flipbooks[FlipbookIndex].EditorMeta.PhaseTag);
		const FText PhaseTooltip = PhaseBadge.IsValid()
			? FText::Format(LOCTEXT("LensCardPhaseTip", "Phase: {0}"), FText::FromString(PhaseBadge.Label))
			: FText::GetEmpty();

		WrapBox->AddSlot()
		.Padding(4)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Fill)
			.Padding(0, 0, 2, 0)
			[
				SNew(SBox)
				.WidthOverride(4.0f)
				.ToolTipText(PhaseTooltip)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(PhaseBadge.IsValid()
						? FSlateColor(PhaseBadge.Color)
						: FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f)))
					.Padding(0)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				BuildFlipbookCard(FlipbookIndex, FlipbookNameUsageCounts)
			]
		];
	}

	// Collapse state is panel-local (CollapsedLensSections) — the My Groups stale-group purge runs on
	// the MODEL's collapsed set and would wipe lens keys.
	const FName SectionKey(*(FString(TEXT("__TagLens_"))
		+ (Section.bUnmapped ? FString(TEXT("Unmapped")) : Section.GroupTag.GetTagName().ToString())));

	return SNew(SExpandableArea)
		.AllowAnimatedTransition(false)
		.InitiallyCollapsed(!bHasActiveFilter && CollapsedLensSections.Contains(SectionKey))
		.OnAreaExpansionChanged_Lambda([this, SectionKey](bool bExpanded)
		{
			if (bExpanded)
			{
				CollapsedLensSections.Remove(SectionKey);
			}
			else
			{
				CollapsedLensSections.Add(SectionKey);
			}
		})
		.HeaderContent()
		[
			SNew(SHorizontalBox)

			// Accent swatch — the group key's chip color (registry → convention → neutral).
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0, 6, 0)
			[
				SNew(SBox)
				.WidthOverride(10.0f)
				.HeightOverride(10.0f)
				[
					SNew(SColorBlock)
					.Color(Accent.Color)
					.ToolTipText(Section.bUnmapped
						? LOCTEXT("LensUnmappedSwatchTip", "Animations in no tag-mapping group")
						: FText::FromName(Section.GroupTag.GetTagName()))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Section.Title))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ToolTipText(Section.bUnmapped
					? LOCTEXT("LensUnmappedTip", "Fallback section: animations that belong to no tag-mapping group")
					: FText::FromName(Section.GroupTag.GetTagName()))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8, 0)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
				.Padding(FMargin(6, 2))
				[
					SNew(STextBlock)
					.Text(FText::AsNumber(FilteredIndices.Num()))
					.TextStyle(FAppStyle::Get(), "SmallText")
				]
			]
		]
		.BodyContent()
		[
			SNew(SBox)
			.Padding(4)
			[
				WrapBox
			]
		];
}

// ==========================================
// SELECTION
// ==========================================

void SFlipbookBrowserPanel::OnFlipbookGroupCardClicked(int32 FlipbookIndex, const FPointerEvent& MouseEvent)
{
	if (!Model.IsValid()) return;

	// A user click on a flipbook is a flipbook-selection intent, so any active edge selection ends
	// (review R-U3). This site-level clear is NOT redundant with the model-level hoisted clear in
	// SetSelectedFlipbook (Codex P2, PR #224): this handler has its OWN same-index early-out below
	// (`FlipbookIndex != GetSelectedFlipbookIndex()`) that skips SetSelectedFlipbook entirely, so
	// re-clicking the From flipbook would never reach the model's clear. No-op when nothing is set.
	Model->ClearSelectedTransition();

	if (FlipbookIndex != Model->GetSelectedFlipbookIndex())
	{
		Model->SetSelectedFlipbook(FlipbookIndex);
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	TSet<int32>& Cards = Model->GetSelectedFlipbookCardsMutable();
	if (MouseEvent.IsControlDown())
	{
		if (Cards.Contains(FlipbookIndex))
		{
			Cards.Remove(FlipbookIndex);
		}
		else
		{
			Cards.Add(FlipbookIndex);
		}
		Model->SetSelectionAnchorIndex(FlipbookIndex);
	}
	else if (MouseEvent.IsShiftDown() && Model->GetSelectionAnchorIndex() != INDEX_NONE)
	{
		if (!Asset.IsValid()) return;
		int32 AnchorIdx = Model->GetSelectionAnchorIndex();
		FName AnchorGroup = Asset->Flipbooks.IsValidIndex(AnchorIdx) ? Asset->Flipbooks[AnchorIdx].FlipbookGroup : NAME_None;
		FName ClickedGroup = Asset->Flipbooks.IsValidIndex(FlipbookIndex) ? Asset->Flipbooks[FlipbookIndex].FlipbookGroup : NAME_None;

		if (AnchorGroup != ClickedGroup)
		{
			Cards.Empty();
			Cards.Add(FlipbookIndex);
			Model->SetSelectionAnchorIndex(FlipbookIndex);
		}
		else
		{
			// Build the range source in DISPLAYED order (alphabetical, anchor-group-filtered),
			// restricted to currently-visible cards so the range matches exactly what the user sees.
			// Cards render from GetSortedFlipbookIndices() -> FilteredIndices (PassesFlipbookGroupSearch),
			// NOT from the asset's raw array order.
			TArray<int32> GroupFlipbooks;
			for (int32 Idx : GetSortedFlipbookIndices())
			{
				if (Asset->Flipbooks.IsValidIndex(Idx)
					&& Asset->Flipbooks[Idx].FlipbookGroup == AnchorGroup
					&& PassesFlipbookGroupSearch(Asset->Flipbooks[Idx]))
				{
					GroupFlipbooks.Add(Idx);
				}
			}
			int32 AnchorPos = GroupFlipbooks.Find(AnchorIdx);
			int32 ClickPos = GroupFlipbooks.Find(FlipbookIndex);
			if (AnchorPos != INDEX_NONE && ClickPos != INDEX_NONE)
			{
				int32 Start = FMath::Min(AnchorPos, ClickPos);
				int32 End = FMath::Max(AnchorPos, ClickPos);
				for (int32 i = Start; i <= End; ++i)
				{
					Cards.Add(GroupFlipbooks[i]);
				}
			}
		}
	}
	else
	{
		Cards.Empty();
		Cards.Add(FlipbookIndex);
		Model->SetSelectionAnchorIndex(FlipbookIndex);
	}
}

bool SFlipbookBrowserPanel::PassesFlipbookGroupSearch(const FFlipbookProfileEntry& FlipbookData) const
{
	if (!Model.IsValid()) return true;

	const FString Query = Model->GetFlipbookGroupSearchText().TrimStartAndEnd();
	if (!Query.IsEmpty())
	{
		// TASK-108 U6 (R7): match the NAME (pre-existing) OR any EFFECTIVE tag's leaf/full path —
		// effective tags come from the per-refresh batch cache, so "airborne" finds a Context.Airborne
		// entry and "search by group" works with zero authoring (group keys are effective tags).
		static const FGameplayTagContainer PassesSearch_EmptyTags; // cache miss (stale caller) = name-only
		const Paper2DPlusAnimationTagQuery::FAnimationTagSet* TagSet =
			CachedAnimationTagMap.Find(FlipbookData.Identity.FlipbookName.ToLower());
		if (!Paper2DPlusAnimationMap::AnimationSearchMatches(FlipbookData.Identity.FlipbookName,
			TagSet ? TagSet->EffectiveTags : PassesSearch_EmptyTags, Query))
		{
			return false;
		}
	}

	const int32 CompletionFilterMask = Model->GetCompletionFilterMask();
	if (CompletionFilterMask != 0)
	{
		const int32 Flags = FlipbookData.EditorMeta.CompletionFlags;
		// Live tabs only (bits 0,1,2,5,6). Retired bits 3 (Phases) & 4 (Effects) are stripped in
		// PostLoad, so 0x7F could never match a reloaded asset for the Complete filter.
		constexpr int32 AllTaskBits = UPaper2DPlusCharacterProfileAsset::LiveTaskBits;

		if (CompletionFilterMask & (1 << 7))
		{
			if ((Flags & AllTaskBits) != AllTaskBits) return false;
		}

		if (CompletionFilterMask & (1 << 8))
		{
			if ((Flags & AllTaskBits) == AllTaskBits) return false;
		}

		const int32 TaskFilterBits = CompletionFilterMask & AllTaskBits;
		if (TaskFilterBits != 0)
		{
			if ((~Flags & TaskFilterBits) == 0) return false;
		}
	}

	return true;
}

// ==========================================
// COMPLETION FILTER
// ==========================================

TSharedRef<SWidget> SFlipbookBrowserPanel::BuildCompletionFilterButton(TFunction<void()> OnChanged)
{
	return SNew(SComboButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.HasDownArrow(false)
		.ContentPadding(FMargin(2, 0))
		.ToolTipText_Lambda([this]()
		{
			return (Model.IsValid() && Model->GetCompletionFilterMask() != 0)
				? LOCTEXT("FilterActiveTip", "Filters active (click to modify)")
				: LOCTEXT("FilterTip", "Filter by completion status");
		})
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.Filter")).DesiredSizeOverride(FVector2D(14, 14))
				.ColorAndOpacity_Lambda([this]()
				{
					return (Model.IsValid() && Model->GetCompletionFilterMask() != 0)
						? FSlateColor(FLinearColor(0.4f, 0.7f, 1.0f))
						: FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f));
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					if (!Model.IsValid() || Model->GetCompletionFilterMask() == 0) return FText::GetEmpty();
					int32 Count = 0;
					int32 Mask = Model->GetCompletionFilterMask();
					while (Mask) { Count += (Mask & 1); Mask >>= 1; }
					return FText::AsNumber(Count);
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.7f, 1.0f)))
			]
		]
		.MenuContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 6, 8, 2)
			[ SNew(STextBlock).Text(LOCTEXT("StatusFiltersLabel", "STATUS")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 8)).ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f))) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (Model.IsValid() && (Model->GetCompletionFilterMask() & (1 << 8))) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, OnChanged](ECheckBoxState) { if (Model.IsValid()) Model->SetCompletionFilterMask(Model->GetCompletionFilterMask() ^ (1 << 8)); OnChanged(); })
				[ SNew(STextBlock).Text(LOCTEXT("FiltIncomplete", "Incomplete")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (Model.IsValid() && (Model->GetCompletionFilterMask() & (1 << 7))) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, OnChanged](ECheckBoxState) { if (Model.IsValid()) Model->SetCompletionFilterMask(Model->GetCompletionFilterMask() ^ (1 << 7)); OnChanged(); })
				[ SNew(STextBlock).Text(LOCTEXT("FiltComplete", "Complete")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)[ SNew(SSeparator) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2, 8, 2)
			[ SNew(STextBlock).Text(LOCTEXT("NeedsWorkLabel", "NEEDS WORK")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 8)).ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f))) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (Model.IsValid() && (Model->GetCompletionFilterMask() & (1 << 0))) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, OnChanged](ECheckBoxState) { if (Model.IsValid()) Model->SetCompletionFilterMask(Model->GetCompletionFilterMask() ^ (1 << 0)); OnChanged(); })
				[ SNew(STextBlock).Text(LOCTEXT("FiltHitboxes", "Hitboxes")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (Model.IsValid() && (Model->GetCompletionFilterMask() & (1 << 1))) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, OnChanged](ECheckBoxState) { if (Model.IsValid()) Model->SetCompletionFilterMask(Model->GetCompletionFilterMask() ^ (1 << 1)); OnChanged(); })
				[ SNew(STextBlock).Text(LOCTEXT("FiltAlignment", "Sprite Alignment")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (Model.IsValid() && (Model->GetCompletionFilterMask() & (1 << 2))) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, OnChanged](ECheckBoxState) { if (Model.IsValid()) Model->SetCompletionFilterMask(Model->GetCompletionFilterMask() ^ (1 << 2)); OnChanged(); })
				[ SNew(STextBlock).Text(LOCTEXT("FiltTiming", "Frame Timing")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (Model.IsValid() && (Model->GetCompletionFilterMask() & (1 << 5))) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, OnChanged](ECheckBoxState) { if (Model.IsValid()) Model->SetCompletionFilterMask(Model->GetCompletionFilterMask() ^ (1 << 5)); OnChanged(); })
				[ SNew(STextBlock).Text(LOCTEXT("FiltRootMotion", "Root Motion")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2, 8, 4)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return (Model.IsValid() && (Model->GetCompletionFilterMask() & (1 << 6))) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, OnChanged](ECheckBoxState) { if (Model.IsValid()) Model->SetCompletionFilterMask(Model->GetCompletionFilterMask() ^ (1 << 6)); OnChanged(); })
				[ SNew(STextBlock).Text(LOCTEXT("FiltTags", "Tag Mappings")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2, 8, 6)
			[
				SNew(SButton).ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.OnClicked_Lambda([this, OnChanged]() { if (Model.IsValid()) Model->SetCompletionFilterMask(0); OnChanged(); return FReply::Handled(); })
				.IsEnabled_Lambda([this]() { return Model.IsValid() && Model->GetCompletionFilterMask() != 0; })
				[ SNew(STextBlock).Text(LOCTEXT("ClearFilters", "Clear All Filters")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)) ]
			]
		];
}

// ==========================================
// GROUP MANAGEMENT
// ==========================================

void SFlipbookBrowserPanel::CreateFlipbookGroup(FName ParentGroup)
{
	if (!Asset.IsValid()) return;
	FString BaseName = TEXT("NewGroup");
	FName NewName(*BaseName);
	int32 Counter = 1;
	while (Asset->HasFlipbookGroup(NewName))
	{
		NewName = FName(*FString::Printf(TEXT("%s%d"), *BaseName, Counter++));
	}
	BeginTransaction(LOCTEXT("CreateGroup", "Create Flipbook Group"));
	Asset->AddFlipbookGroup(NewName, ParentGroup);
	EndTransaction();
	PendingRenameFlipbookGroup = NewName;
	if (Model.IsValid()) Model->SetPendingScrollToGroup(NewName);
	RefreshFlipbookGroupsPanel();
}

void SFlipbookBrowserPanel::DeleteFlipbookGroup(
	FName GroupName,
	bool bRequireConfirmation)
{
	if (!Asset.IsValid()) return;
	const FFlipbookGroupInfo* GroupInfo = nullptr;
	TArray<FName> ChildGroupNames;
	for (const FFlipbookGroupInfo& Group : Asset->FlipbookGroups)
	{
		if (Group.GroupName == GroupName)
		{
			GroupInfo = &Group;
		}
		if (Group.ParentGroup == GroupName)
		{
			ChildGroupNames.Add(Group.GroupName);
		}
	}

	if (!GroupInfo)
	{
		return;
	}

	const TArray<int32> DirectFlipbookIndices = Asset->GetFlipbookIndicesForFlipbookGroup(GroupName);
	DestructiveActions::FDestructiveActionPrompt Prompt;
	Prompt.Title = LOCTEXT("DeleteGroupConfirmTitle", "Delete Flipbook Group");
	Prompt.Body = FText::Format(
		LOCTEXT("DeleteGroupConfirmBody", "Delete flipbook group '{0}'?"),
		FText::FromName(GroupName));
	Prompt.Consequence = LOCTEXT("DeleteGroupConfirmConsequence", "Child groups are promoted to this group's parent. Direct flipbooks move to Ungrouped.");
	Prompt.AffectedItems.Add(FString::Printf(TEXT("Group: %s"), *GroupName.ToString()));
	for (const FName& ChildGroupName : ChildGroupNames)
	{
		Prompt.AffectedItems.Add(FString::Printf(TEXT("Child group promoted: %s"), *ChildGroupName.ToString()));
	}
	for (const int32 FlipbookIndex : DirectFlipbookIndices)
	{
		if (Asset->Flipbooks.IsValidIndex(FlipbookIndex))
		{
			Prompt.AffectedItems.Add(FString::Printf(TEXT("Flipbook ungrouped: %s"), *Asset->Flipbooks[FlipbookIndex].Identity.FlipbookName));
		}
	}
	if (bRequireConfirmation && !DestructiveActions::Confirm(Prompt))
	{
		return;
	}

	BeginTransaction(LOCTEXT("DeleteGroup", "Delete Flipbook Group"));
	Asset->RemoveFlipbookGroup(GroupName);
	EndTransaction();
	RefreshFlipbookGroupsPanel();
}

void SFlipbookBrowserPanel::ShowFlipbookGroupContextMenu(FName GroupName, const FVector2D& CursorPos)
{
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.AddMenuEntry(LOCTEXT("RenameGroup", "Rename"), LOCTEXT("RenameGroupTooltip", "Rename this group"), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SFlipbookBrowserPanel::TriggerFlipbookGroupRename, GroupName)));
	MenuBuilder.AddSubMenu(LOCTEXT("ChangeColor", "Change Color"), LOCTEXT("ChangeColorTooltip", "Change the group header color"),
		FNewMenuDelegate::CreateLambda([this, GroupName](FMenuBuilder& SubMenu)
		{
			static const TPair<FText, FLinearColor> Palette[] = {
				{LOCTEXT("Red", "Red"), FLinearColor(0.8f, 0.2f, 0.2f)},
				{LOCTEXT("Orange", "Orange"), FLinearColor(0.9f, 0.5f, 0.1f)},
				{LOCTEXT("Yellow", "Yellow"), FLinearColor(0.9f, 0.8f, 0.2f)},
				{LOCTEXT("Green", "Green"), FLinearColor(0.2f, 0.7f, 0.3f)},
				{LOCTEXT("Teal", "Teal"), FLinearColor(0.2f, 0.7f, 0.7f)},
				{LOCTEXT("Blue", "Blue"), FLinearColor(0.3f, 0.5f, 0.8f)},
				{LOCTEXT("Indigo", "Indigo"), FLinearColor(0.4f, 0.3f, 0.8f)},
				{LOCTEXT("Purple", "Purple"), FLinearColor(0.6f, 0.3f, 0.7f)},
				{LOCTEXT("Pink", "Pink"), FLinearColor(0.8f, 0.3f, 0.5f)},
				{LOCTEXT("Gray", "Gray"), FLinearColor(0.5f, 0.5f, 0.5f)},
			};
			for (const auto& Color : Palette)
			{
				SubMenu.AddMenuEntry(Color.Key, FText::GetEmpty(), FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this, &SFlipbookBrowserPanel::OnFlipbookGroupColorCommitted, Color.Value, GroupName)));
			}
			SubMenu.AddMenuSeparator();
			SubMenu.AddMenuEntry(LOCTEXT("CustomColor", "Custom..."), LOCTEXT("CustomColorTooltip", "Open color picker"), FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, GroupName]()
				{
					FLinearColor CurrentColor(0.3f, 0.5f, 0.8f);
					for (const FFlipbookGroupInfo& Group : Asset->FlipbookGroups)
					{
						if (Group.GroupName == GroupName) { CurrentColor = Group.Color; break; }
					}
					OnOpenFlipbookGroupColorPicker(GroupName, CurrentColor);
				})));
		}));
	MenuBuilder.AddMenuEntry(LOCTEXT("AddSubGroup", "Add Sub-group"), LOCTEXT("AddSubGroupTooltip", "Create a new group nested under this one"), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, GroupName]() { CreateFlipbookGroup(GroupName); })));
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(LOCTEXT("DeleteGroupMenu", "Delete Group"), LOCTEXT("DeleteGroupMenuTooltip", "Delete this group"), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, GroupName]() { DeleteFlipbookGroup(GroupName); })));

	FSlateApplication::Get().PushMenu(SharedThis(this), FWidgetPath(), MenuBuilder.MakeWidget(), CursorPos, FPopupTransitionEffect::ContextMenu);
}

void SFlipbookBrowserPanel::TriggerFlipbookGroupRename(FName GroupName)
{
	PendingRenameFlipbookGroup = GroupName;
	RefreshFlipbookGroupsPanel();
}

bool SFlipbookBrowserPanel::OnVerifyFlipbookGroupNameChanged(const FText& InText, FText& OutErrorMessage, FName CurrentGroupName)
{
	FName NewName(*InText.ToString());
	if (NewName == NAME_None)
	{
		OutErrorMessage = LOCTEXT("EmptyGroupName", "Group name cannot be empty");
		return false;
	}
	if (Asset.IsValid() && Asset->HasFlipbookGroup(NewName) && NewName != CurrentGroupName)
	{
		OutErrorMessage = LOCTEXT("DuplicateGroupName", "A group with this name already exists");
		return false;
	}
	return true;
}

void SFlipbookBrowserPanel::OnFlipbookGroupNameCommitted(const FText& InText, ETextCommit::Type CommitType, FName OriginalGroupName)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		FName NewName(*InText.ToString());
		if (NewName != NAME_None && NewName != OriginalGroupName && Asset.IsValid())
		{
			BeginTransaction(LOCTEXT("RenameGroup", "Rename Flipbook Group"));
			Asset->RenameFlipbookGroup(OriginalGroupName, NewName);
			EndTransaction();
			RefreshFlipbookGroupsPanel();
		}
	}
}

void SFlipbookBrowserPanel::OnOpenFlipbookGroupColorPicker(FName GroupName, FLinearColor CurrentColor)
{
	FColorPickerArgs PickerArgs;
	PickerArgs.bIsModal = true;
	PickerArgs.ParentWidget = SharedThis(this);
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2)
	PickerArgs.InitialColor = CurrentColor;
#endif
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(
		this, &SFlipbookBrowserPanel::OnFlipbookGroupColorCommitted, GroupName);
	OpenColorPicker(PickerArgs);
}

void SFlipbookBrowserPanel::OnFlipbookGroupColorCommitted(FLinearColor NewColor, FName GroupName)
{
	if (!Asset.IsValid()) return;
	BeginTransaction(LOCTEXT("ChangeGroupColor", "Change Group Color"));
	Asset->SetFlipbookGroupColor(GroupName, NewColor);
	EndTransaction();
	RefreshFlipbookGroupsPanel();
}

void SFlipbookBrowserPanel::AutoGroupByPrefix()
{
	if (!Asset.IsValid()) return;
	TMap<FString, TArray<int32>> PrefixToFlipbooks;
	for (int32 i = 0; i < Asset->Flipbooks.Num(); ++i)
	{
		if (Asset->Flipbooks[i].FlipbookGroup != NAME_None) continue;
		const FString& Name = Asset->Flipbooks[i].Identity.FlipbookName;
		int32 UnderscoreIdx = Name.Find(TEXT("_"));
		if (UnderscoreIdx != INDEX_NONE)
		{
			FString Prefix = Name.Left(UnderscoreIdx);
			if (Prefix.Len() >= 2) { PrefixToFlipbooks.FindOrAdd(Prefix).Add(i); continue; }
		}
		int32 LastBoundary = 0;
		for (int32 CharIdx = 1; CharIdx < Name.Len(); ++CharIdx)
		{
			TCHAR Current = Name[CharIdx];
			TCHAR Previous = Name[CharIdx - 1];
			bool bBoundary = false;
			if (FChar::IsLower(Previous) && FChar::IsUpper(Current)) bBoundary = true;
			if (FChar::IsAlpha(Previous) && FChar::IsDigit(Current)) bBoundary = true;
			if (FChar::IsDigit(Previous) && FChar::IsAlpha(Current)) bBoundary = true;
			if (bBoundary) LastBoundary = CharIdx;
		}
		if (LastBoundary > 1)
		{
			FString Prefix = Name.Left(LastBoundary);
			PrefixToFlipbooks.FindOrAdd(Prefix).Add(i);
		}
	}
	int32 GroupsCreated = 0;
	int32 FlipbooksGrouped = 0;
	int32 FlipbooksAddedToExisting = 0;
	BeginTransaction(LOCTEXT("AutoGroupByPrefix", "Auto-group by Prefix"));
	for (const auto& Pair : PrefixToFlipbooks)
	{
		if (Pair.Value.Num() < 2) continue;
		FName GName(*Pair.Key);
		if (Asset->HasFlipbookGroup(GName))
		{
			for (int32 FlipbookIdx : Pair.Value) { Asset->MoveFlipbookToFlipbookGroup(FlipbookIdx, GName); FlipbooksAddedToExisting++; }
		}
		else
		{
			Asset->AddFlipbookGroup(GName);
			for (int32 FlipbookIdx : Pair.Value) Asset->MoveFlipbookToFlipbookGroup(FlipbookIdx, GName);
			GroupsCreated++;
			FlipbooksGrouped += Pair.Value.Num();
		}
	}
	EndTransaction();
	FString Message;
	if (GroupsCreated > 0 || FlipbooksAddedToExisting > 0)
	{
		if (GroupsCreated > 0) Message += FString::Printf(TEXT("Created %d groups for %d flipbooks."), GroupsCreated, FlipbooksGrouped);
		if (FlipbooksAddedToExisting > 0)
		{
			if (!Message.IsEmpty()) Message += TEXT(" ");
			Message += FString::Printf(TEXT("Added %d flipbooks to existing groups."), FlipbooksAddedToExisting);
		}
	}
	else
	{
		Message = TEXT("No prefixes found with 2+ matching ungrouped flipbooks.");
	}
	FNotificationInfo Info(FText::FromString(Message));
	Info.ExpireDuration = 4.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
	RefreshFlipbookGroupsPanel();
}

void SFlipbookBrowserPanel::OnFlipbookGroupFlipbooksDrop(const TArray<int32>& FlipbookIndices, FName TargetGroup)
{
	if (!Asset.IsValid() || FlipbookIndices.Num() == 0 || !Model.IsValid()) return;

	bool bAnyNeedsMove = false;
	for (int32 Idx : FlipbookIndices)
	{
		if (!Asset->Flipbooks.IsValidIndex(Idx)) return;
		if (Asset->Flipbooks[Idx].FlipbookGroup != TargetGroup) bAnyNeedsMove = true;
	}
	if (!bAnyNeedsMove) return;

	BeginTransaction(LOCTEXT("MoveToGroup", "Move Flipbooks to Group"));
	for (int32 Idx : FlipbookIndices)
	{
		Asset->MoveFlipbookToFlipbookGroup(Idx, TargetGroup);
	}
	EndTransaction();
	Model->GetCollapsedFlipbookGroupsMutable().Remove(TargetGroup == NAME_None ? FName("__Ungrouped") : TargetGroup);
	RefreshFlipbookGroupsPanel();
}

void SFlipbookBrowserPanel::OnGroupDrop(FName SourceGroupName, FName TargetParentGroup)
{
	if (!Asset.IsValid() || SourceGroupName == NAME_None) return;
	if (SourceGroupName == TargetParentGroup) return;
	if (TargetParentGroup != NAME_None && Asset->IsDescendantOfFlipbookGroup(TargetParentGroup, SourceGroupName)) return;
	for (const FFlipbookGroupInfo& Group : Asset->FlipbookGroups)
	{
		if (Group.GroupName == SourceGroupName)
		{
			if (Group.ParentGroup == TargetParentGroup) return;
			break;
		}
	}
	BeginTransaction(LOCTEXT("ReparentGroup", "Move Group"));
	Asset->ReparentFlipbookGroup(SourceGroupName, TargetParentGroup);
	EndTransaction();
	if (Model.IsValid())
	{
		Model->GetCollapsedFlipbookGroupsMutable().Remove(TargetParentGroup == NAME_None ? FName("__Ungrouped") : TargetParentGroup);
	}
	RefreshFlipbookGroupsPanel();
}


#undef LOCTEXT_NAMESPACE
