// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "CharacterCoverage/SExpectedTagsPanel.h"

#include "AnimationTagChipUtils.h"
#include "CharacterCoverage/SExpectedTagDragDropWidgets.h"
#include "CharacterProfileEditorModel.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Paper2DPlusCharacterCatalogAsset.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Containers/Ticker.h"
#include "Paper2DPlusSettings.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "ExpectedTagsPanel"

namespace
{
	FText ExpectedTagsPanel_AnimationNamesText(const TArray<FString>& Names)
	{
		return FText::FromString(FString::Join(Names, TEXT(", ")));
	}
}

class SExpectedTagsPanelDragSource final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SExpectedTagsPanelDragSource) {}
		SLATE_ARGUMENT(TWeakPtr<SExpectedTagsPanel>, Panel)
		SLATE_ARGUMENT(FGameplayTag, ExpectedTag)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Panel = InArgs._Panel;
		ExpectedTag = InArgs._ExpectedTag;
		ChildSlot[InArgs._Content.Widget];
	}

	virtual FReply OnPreviewMouseButtonDown(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override
	{
		(void)MyGeometry;
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			if (const TSharedPtr<SExpectedTagsPanel> PinnedPanel = Panel.Pin())
			{
				PinnedPanel->ArmExpectedTagDrag(
					ExpectedTag,
					MouseEvent.GetScreenSpacePosition());
			}
		}
		return FReply::Unhandled();
	}

private:
	TWeakPtr<SExpectedTagsPanel> Panel;
	FGameplayTag ExpectedTag;
};

FExpectedTagsCatalogResolution FExpectedTagsCatalogResolution::NotConfigured()
{
	return FExpectedTagsCatalogResolution();
}

FExpectedTagsCatalogResolution
FExpectedTagsCatalogResolution::ConfiguredButUnavailable(
	const FSoftObjectPath& InConfiguredPath)
{
	FExpectedTagsCatalogResolution Result;
	Result.Status = EExpectedTagsCatalogResolutionStatus::ConfiguredButUnavailable;
	Result.ConfiguredPath = InConfiguredPath;
	return Result;
}

FExpectedTagsCatalogResolution FExpectedTagsCatalogResolution::Loaded(
	UPaper2DPlusCharacterCatalogAsset* InCatalog)
{
	FExpectedTagsCatalogResolution Result;
	Result.Status = EExpectedTagsCatalogResolutionStatus::Loaded;
	Result.ConfiguredPath = FSoftObjectPath(InCatalog);
	Result.Catalog = InCatalog;
	return Result;
}

void SExpectedTagsPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	CatalogProvider = InArgs._CatalogProvider;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Heading", "Expected Animation Tags"))
				.Font(FAppStyle::GetFontStyle("BoldFont"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 7.0f)
			[
				SNew(STextBlock)
					.Text(LOCTEXT(
						"Help",
						"Catalog expectations across the whole Character Profile. "
						"Drag a tag onto an animation, or use Assign."))
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(RowsHost, SVerticalBox)
				]
			]
		]
	];

	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}
	if (Model.IsValid())
	{
		AssetDataChangedHandle = Model->OnAssetDataChanged.AddSP(
			this,
			&SExpectedTagsPanel::HandleModelRefresh);
		ExternalModifiedHandle = Model->OnAssetExternallyModified.AddSP(
			this,
			&SExpectedTagsPanel::HandleModelRefresh);
	}
	CatalogSettingsChangedHandle =
		UPaper2DPlusSettings::OnCharacterCatalogSettingsChanged().AddSP(
			this,
			&SExpectedTagsPanel::HandleCatalogSettingsChanged);
	TagColorsChangedHandle = UPaper2DPlusSettings::OnTagColorsChanged().AddSP(
		this,
		&SExpectedTagsPanel::HandleTagColorsChanged);

	Refresh();
}

SExpectedTagsPanel::~SExpectedTagsPanel()
{
	Shutdown();
}

void SExpectedTagsPanel::Shutdown()
{
	if (bShutdown)
	{
		return;
	}
	bShutdown = true;

	CancelPendingPickerAssignment();
	DisarmExpectedTagDrag();
	ReleaseOwnedCatalogWatch();

	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
	if (Model.IsValid())
	{
		if (AssetDataChangedHandle.IsValid())
		{
			Model->OnAssetDataChanged.Remove(AssetDataChangedHandle);
		}
		if (ExternalModifiedHandle.IsValid())
		{
			Model->OnAssetExternallyModified.Remove(ExternalModifiedHandle);
		}
	}
	if (CatalogSettingsChangedHandle.IsValid())
	{
		UPaper2DPlusSettings::OnCharacterCatalogSettingsChanged().Remove(
			CatalogSettingsChangedHandle);
	}
	if (TagColorsChangedHandle.IsValid())
	{
		UPaper2DPlusSettings::OnTagColorsChanged().Remove(TagColorsChangedHandle);
	}

	AssetDataChangedHandle.Reset();
	ExternalModifiedHandle.Reset();
	CatalogSettingsChangedHandle.Reset();
	TagColorsChangedHandle.Reset();
	CatalogLifetimeGuard.Reset();
	RowsHost.Reset();
	RowPickerCombos.Reset();
	Model.Reset();
}

FExpectedTagsCatalogResolution SExpectedTagsPanel::ResolveDefaultCatalog()
{
	const UPaper2DPlusSettings* Settings = UPaper2DPlusSettings::Get();
	if (!Settings || Settings->DefaultCharacterCatalog.IsNull())
	{
		return FExpectedTagsCatalogResolution::NotConfigured();
	}

	const FSoftObjectPath ConfiguredPath =
		Settings->DefaultCharacterCatalog.ToSoftObjectPath();
	UPaper2DPlusCharacterCatalogAsset* Catalog =
		Settings->DefaultCharacterCatalog.LoadSynchronous();
	return Catalog
		? FExpectedTagsCatalogResolution::Loaded(Catalog)
		: FExpectedTagsCatalogResolution::ConfiguredButUnavailable(ConfiguredPath);
}

void SExpectedTagsPanel::Refresh()
{
	if (bShutdown)
	{
		return;
	}

	++RefreshSerial;
	CatalogAccessorCallsInLastRefresh = 0;
	ResolverCallsInLastRefresh = 0;
	CoverageRows.Reset();
	RowPickerCombos.Reset();

	FExpectedTagsCatalogResolution Resolution =
		CatalogProvider
			? CatalogProvider()
			: ResolveDefaultCatalog();
	UPaper2DPlusCharacterCatalogAsset* Catalog =
		Resolution.Status == EExpectedTagsCatalogResolutionStatus::Loaded
			? Resolution.Catalog.Get()
			: nullptr;
	if (Resolution.Status == EExpectedTagsCatalogResolutionStatus::Loaded && !Catalog)
	{
		Resolution.Status =
			EExpectedTagsCatalogResolutionStatus::ConfiguredButUnavailable;
	}

	CatalogLifetimeGuard.Reset(Catalog);
	UpdateCatalogWatch(Catalog);

	UPaper2DPlusCharacterProfileAsset* Profile =
		Model.IsValid() ? Model->GetAsset() : nullptr;
	if (!Profile)
	{
		PanelState = EExpectedTagsPanelState::ProfileUnavailable;
		DisarmExpectedTagDrag();
		RebuildBody();
		return;
	}

	if (Resolution.Status == EExpectedTagsCatalogResolutionStatus::NotConfigured)
	{
		PanelState = EExpectedTagsPanelState::NoAuthoritativeCatalog;
		DisarmExpectedTagDrag();
		RebuildBody();
		return;
	}
	if (Resolution.Status
		== EExpectedTagsCatalogResolutionStatus::ConfiguredButUnavailable)
	{
		PanelState = EExpectedTagsPanelState::ConfiguredCatalogUnavailable;
		DisarmExpectedTagDrag();
		RebuildBody();
		return;
	}

	FGameplayTagContainer ExpectedTags;
	++CatalogAccessorCallsInLastRefresh;
	const bool bProfileIsCatalogMember =
		Catalog->GetExpectedAnimationTagsForCharacter(
			TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>(Profile),
			ExpectedTags);

	// Both non-Ready outcomes are decidable from the Catalog lookup alone, so they
	// early-out before the shared resolver runs and its result would be discarded.
	if (!bProfileIsCatalogMember)
	{
		PanelState = EExpectedTagsPanelState::ProfileNotInCatalog;
		DisarmExpectedTagDrag();
		RebuildBody();
		return;
	}
	if (ExpectedTags.IsEmpty())
	{
		PanelState = EExpectedTagsPanelState::CatalogHasNoExpectations;
		DisarmExpectedTagDrag();
		RebuildBody();
		return;
	}

	++ResolverCallsInLastRefresh;
	FCharacterCoverageResolveResult Coverage =
		FCharacterCoverageResolver::Resolve(Profile, ExpectedTags);

	PanelState = EExpectedTagsPanelState::Ready;
	CoverageRows = MoveTemp(Coverage.Rows);
	if (bExpectedTagDragArmed && !IsArmedDragStillValid())
	{
		DisarmExpectedTagDrag();
	}
	RebuildBody();
}

void SExpectedTagsPanel::RebuildBody()
{
	if (!RowsHost.IsValid())
	{
		return;
	}

	++BodyRebuildCount;
	RowsHost->ClearChildren();
	RowPickerCombos.Reset();
	if (PanelState != EExpectedTagsPanelState::Ready)
	{
		RowsHost->AddSlot()
		.AutoHeight()
		.Padding(FMargin(2.0f, 8.0f))
		[
			SNew(STextBlock)
				.Text(GetEmptyStateText(PanelState))
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
		return;
	}

	for (const FCharacterCoverageRow& Row : CoverageRows)
	{
		RowsHost->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 5.0f))
		[
			BuildCoverageRow(Row)
		];
	}
}

TSharedRef<SWidget> SExpectedTagsPanel::BuildCoverageRow(
	const FCharacterCoverageRow& Row)
{
	TSharedPtr<SComboButton> PickerCombo;
	TSharedRef<SWidget> RowWidget =
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(FMargin(7.0f, 5.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.35f)
			.VAlign(VAlign_Center)
			[
				SNew(SExpectedTagsPanelDragSource)
					.Panel(SharedThis(this))
					.ExpectedTag(Row.ExpectedTag)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							BuildExpectedTagChip(Row)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 2.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
								.Text(FText::FromName(Row.ExpectedTag.GetTagName()))
								.ToolTipText(FText::FromName(
									Row.ExpectedTag.GetTagName()))
								.ColorAndOpacity(
									FSlateColor::UseSubduedForeground())
						]
					]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.5f)
			.Padding(8.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
					.Text(GetRowStatusText(Row))
					.AutoWrapText(true)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(PickerCombo, SComboButton)
					.OnGetMenuContent(
						this,
						&SExpectedTagsPanel::BuildAnimationPickerMenu,
						Row.ExpectedTag)
					.ButtonContent()
					[
						SNew(STextBlock)
							.Text(GetPickerButtonText(Row))
					]
			]
		];
	RowPickerCombos.Add(Row.ExpectedTag.GetTagName(), PickerCombo);
	return RowWidget;
}

TSharedRef<SWidget> SExpectedTagsPanel::BuildExpectedTagChip(
	const FCharacterCoverageRow& Row) const
{
	using namespace Paper2DPlusAnimationTagChips;

	FAnimationTagChipItem Item;
	Item.Tag = Row.ExpectedTag;
	if (Row.Status == ECharacterCoverageStatus::NearMiss)
	{
		Item.Provenance = Row.ChainInheritedAnimationNames.Num() > 0
			? EAnimationTagChipProvenance::ChainInherited
			: EAnimationTagChipProvenance::GroupImplied;
	}

	TArray<FAnimationTagChipItem> Items;
	Items.Add(Item);
	TSharedRef<SWidget> Chips = MakeChipsRow(Items, 1);
	if (Row.Status == ECharacterCoverageStatus::Missing)
	{
		return SNew(SBox)
			.RenderOpacity(0.45f)
			[
				Chips
			];
	}
	return Chips;
}

TSharedRef<SWidget> SExpectedTagsPanel::BuildAnimationPickerMenu(
	FGameplayTag ExpectedTag)
{
	TSharedRef<SVerticalBox> PickerRows = SNew(SVerticalBox);
	UPaper2DPlusCharacterProfileAsset* Profile =
		Model.IsValid() ? Model->GetAsset() : nullptr;
	int32 ValidTargetCount = 0;

	if (Profile)
	{
		const TWeakPtr<SExpectedTagsPanel> WeakPanel = SharedThis(this);
		for (int32 Index = 0; Index < Profile->Flipbooks.Num(); ++Index)
		{
			const FFlipbookProfileEntry& Entry = Profile->Flipbooks[Index];
			const FExpectedTagAnimationTarget Target =
				FExpectedTagAnimationTarget::Capture(Profile, Index);
			if (!Target.IsStamped())
			{
				continue;
			}

			++ValidTargetCount;
			const FText AnimationLabel =
				FText::FromString(Entry.Identity.FlipbookName);
			PickerRows->AddSlot()
			.AutoHeight()
			[
				SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(AnimationLabel)
					.ToolTipText(FText::Format(
						LOCTEXT(
							"AssignPickerTip",
							"Author {0} directly on {1}."),
						FText::FromName(ExpectedTag.GetTagName()),
						AnimationLabel))
					.OnClicked_Lambda(
						[WeakPanel, ExpectedTag, Target]()
						{
							// These values are copied before closing the menu. The deferred
							// assignment may rebuild every row after this callback unwinds.
							const FGameplayTag ExpectedTagCopy = ExpectedTag;
							const FExpectedTagAnimationTarget TargetCopy = Target;
							if (const TSharedPtr<SExpectedTagsPanel> Panel =
								WeakPanel.Pin())
							{
								Panel->HandleAnimationPicked(
									ExpectedTagCopy,
									TargetCopy);
							}
							return FReply::Handled();
						})
			];
		}
	}

	if (ValidTargetCount == 0)
	{
		PickerRows->AddSlot()
		.AutoHeight()
		.Padding(FMargin(8.0f, 5.0f))
		[
			SNew(STextBlock)
				.Text(LOCTEXT(
					"NoAssignableAnimations",
					"No saved animations are available for assignment."))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}

	return SNew(SBox)
		.MinDesiredWidth(220.0f)
		.MaxDesiredHeight(360.0f)
		[
			SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					PickerRows
				]
		];
}

FText SExpectedTagsPanel::GetPickerButtonText(
	const FCharacterCoverageRow& Row) const
{
	return Row.Status == ECharacterCoverageStatus::Covered
		? LOCTEXT("AssignAnotherButton", "Assign another...")
		: LOCTEXT("AssignButton", "Assign...");
}

EExpectedTagsRowPresentation SExpectedTagsPanel::GetRowPresentation(
	const FCharacterCoverageRow& Row)
{
	switch (Row.Status)
	{
	case ECharacterCoverageStatus::Covered:
		return Row.bSupersetOnly
			? EExpectedTagsRowPresentation::Qualified
			: EExpectedTagsRowPresentation::Covered;
	case ECharacterCoverageStatus::NearMiss:
		return EExpectedTagsRowPresentation::NearMiss;
	case ECharacterCoverageStatus::Missing:
	default:
		return EExpectedTagsRowPresentation::Missing;
	}
}

FText SExpectedTagsPanel::GetRowStatusText(
	const FCharacterCoverageRow& Row)
{
	switch (GetRowPresentation(Row))
	{
	case EExpectedTagsRowPresentation::Covered:
		return FText::Format(
			LOCTEXT("CoveredStatus", "Covered — authored by {0}."),
			ExpectedTagsPanel_AnimationNamesText(Row.AuthoredAnimationNames));
	case EExpectedTagsRowPresentation::Qualified:
		return FText::Format(
			LOCTEXT(
				"QualifiedStatus",
				"Qualified — authored by {0}, but only alongside additional tags."),
			ExpectedTagsPanel_AnimationNamesText(Row.AuthoredAnimationNames));
	case EExpectedTagsRowPresentation::NearMiss:
		{
			const bool bHasGroupCarrier =
				Row.GroupImpliedAnimationNames.Num() > 0;
			const bool bHasChainCarrier =
				Row.ChainInheritedAnimationNames.Num() > 0;
			if (bHasGroupCarrier && bHasChainCarrier)
			{
				return FText::Format(
					LOCTEXT(
						"NearMissGroupAndChainStatus",
						"Near Miss — group-implied on {0}; chain-inherited on "
						"{1}; assign to make authored."),
					ExpectedTagsPanel_AnimationNamesText(
						Row.GroupImpliedAnimationNames),
					ExpectedTagsPanel_AnimationNamesText(
						Row.ChainInheritedAnimationNames));
			}
			if (bHasGroupCarrier)
			{
				return FText::Format(
					LOCTEXT(
						"NearMissGroupStatus",
						"Near Miss — group-implied on {0}; assign to make "
						"authored."),
					ExpectedTagsPanel_AnimationNamesText(
						Row.GroupImpliedAnimationNames));
			}
			return FText::Format(
				LOCTEXT(
					"NearMissChainStatus",
					"Near Miss — chain-inherited on {0}; assign to make "
					"authored."),
				ExpectedTagsPanel_AnimationNamesText(
					Row.ChainInheritedAnimationNames));
		}
	case EExpectedTagsRowPresentation::Missing:
	default:
		return LOCTEXT(
			"MissingStatus",
			"Missing — no animation authors this tag.");
	}
}

FText SExpectedTagsPanel::GetEmptyStateText(
	EExpectedTagsPanelState State)
{
	switch (State)
	{
	case EExpectedTagsPanelState::NoAuthoritativeCatalog:
		return LOCTEXT(
			"NoCatalogState",
			"No authoritative Catalog is configured. Choose a Default Character "
			"Catalog in Project Settings to define expected animation tags.");
	case EExpectedTagsPanelState::ConfiguredCatalogUnavailable:
		return LOCTEXT(
			"UnavailableCatalogState",
			"The configured Default Character Catalog could not be loaded. "
			"Coverage is unavailable until the reference is fixed.");
	case EExpectedTagsPanelState::ProfileNotInCatalog:
		return LOCTEXT(
			"ProfileNotInCatalogState",
			"This Character Profile is not in the authoritative Catalog. Add it "
			"there to receive expected animation tags.");
	case EExpectedTagsPanelState::CatalogHasNoExpectations:
		return LOCTEXT(
			"NoExpectationsState",
			"The authoritative Catalog defines no expected animation tags for "
			"this character.");
	case EExpectedTagsPanelState::ProfileUnavailable:
		return LOCTEXT(
			"ProfileUnavailableState",
			"No Character Profile is available for coverage.");
	case EExpectedTagsPanelState::Ready:
	default:
		return FText::GetEmpty();
	}
}

void SExpectedTagsPanel::HandleModelRefresh()
{
	Refresh();
}

void SExpectedTagsPanel::HandleCatalogSettingsChanged()
{
	Refresh();
}

void SExpectedTagsPanel::HandleTagColorsChanged()
{
	// Tag colors affect paint only. Chip tints are baked when a row widget is built, so
	// rebuild the rows from the cached coverage without re-running the Catalog/resolver pass.
	RebuildBody();
}

void SExpectedTagsPanel::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		Refresh();
	}
}

void SExpectedTagsPanel::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

void SExpectedTagsPanel::UpdateCatalogWatch(
	UPaper2DPlusCharacterCatalogAsset* LoadedCatalog)
{
	if (!Model.IsValid())
	{
		bOwnsCatalogWatch = false;
		CatalogWatchInstalledByPanel.Reset();
		return;
	}

	if (CatalogWatchInstalledByPanel.Get() == LoadedCatalog && bOwnsCatalogWatch == (LoadedCatalog != nullptr))
	{
		return;
	}

	CatalogWatchInstalledByPanel = LoadedCatalog;
	bOwnsCatalogWatch = LoadedCatalog != nullptr;

	// One subscription for the panel's whole lifetime; the handler filters by the Catalog currently
	// tracked above, so a Catalog swap needs no re-registration.
	if (bOwnsCatalogWatch && !CatalogObjectModifiedHandle.IsValid())
	{
		CatalogObjectModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddSP(
			this,
			&SExpectedTagsPanel::HandleObjectModified);
	}
}

void SExpectedTagsPanel::ReleaseOwnedCatalogWatch()
{
	if (CatalogObjectModifiedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectModified.Remove(CatalogObjectModifiedHandle);
		CatalogObjectModifiedHandle.Reset();
	}
	bOwnsCatalogWatch = false;
	bCatalogRefreshPending = false;
	CatalogWatchInstalledByPanel.Reset();
}

void SExpectedTagsPanel::HandleObjectModified(UObject* ModifiedObject)
{
	const UPaper2DPlusCharacterCatalogAsset* WatchedCatalog = CatalogWatchInstalledByPanel.Get();
	if (bShutdown || !WatchedCatalog || !ModifiedObject || bCatalogRefreshPending)
	{
		return;
	}
	// Match the Catalog itself or anything owned by it, so an edit to a nested authored struct is seen
	// on the same terms as an edit to the asset.
	if (ModifiedObject != WatchedCatalog && !ModifiedObject->IsIn(WatchedCatalog))
	{
		return;
	}

	// Never re-resolve coverage from inside the notification: it arrives mid-transaction, BEFORE the
	// mutation the caller is about to make, so a synchronous refresh would project the pre-edit state.
	// Deferred on the core ticker rather than an active timer because active timers only run while the
	// widget paints — this panel shares a tab stack and must still be correct when it is the hidden
	// sibling. Same mechanism the shared model uses for its own external-modify notification.
	bCatalogRefreshPending = true;
	TWeakPtr<SExpectedTagsPanel> WeakSelf = SharedThis(this);
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakSelf](float) -> bool
		{
			if (const TSharedPtr<SExpectedTagsPanel> Self = WeakSelf.Pin())
			{
				Self->FlushCatalogRefresh(0.0, 0.0f);
			}
			return false;
		}),
		0.0f);
}

EActiveTimerReturnType SExpectedTagsPanel::FlushCatalogRefresh(double CurrentTime, float DeltaTime)
{
	(void)CurrentTime;
	(void)DeltaTime;
	bCatalogRefreshPending = false;
	if (!bShutdown)
	{
		Refresh();
	}
	return EActiveTimerReturnType::Stop;
}

void SExpectedTagsPanel::ArmExpectedTagDrag(
	const FGameplayTag& ExpectedTag,
	const FVector2D& ScreenSpacePosition)
{
	UPaper2DPlusCharacterProfileAsset* Profile =
		Model.IsValid() ? Model->GetAsset() : nullptr;
	if (!Profile || !ExpectedTag.IsValid())
	{
		DisarmExpectedTagDrag();
		return;
	}

	bExpectedTagDragArmed = true;
	ArmedExpectedTag = ExpectedTag;
	ArmedDragStartPosition = ScreenSpacePosition;
	ArmedSourceAsset = Profile;
}

void SExpectedTagsPanel::DisarmExpectedTagDrag()
{
	bExpectedTagDragArmed = false;
	ArmedExpectedTag = FGameplayTag();
	ArmedDragStartPosition = FVector2D::ZeroVector;
	ArmedSourceAsset.Reset();
}

bool SExpectedTagsPanel::IsArmedDragStillValid() const
{
	if (!bExpectedTagDragArmed
		|| !ArmedExpectedTag.IsValid()
		|| !ArmedSourceAsset.IsValid()
		|| !Model.IsValid()
		|| Model->GetAsset() != ArmedSourceAsset.Get()
		|| PanelState != EExpectedTagsPanelState::Ready)
	{
		return false;
	}
	return CoverageRows.ContainsByPredicate(
		[this](const FCharacterCoverageRow& Row)
		{
			return Row.ExpectedTag == ArmedExpectedTag;
		});
}

FReply SExpectedTagsPanel::OnPreviewMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	(void)MyGeometry;
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// The child drag source runs later in the preview tunnel and re-arms its exact row.
		// A click anywhere else only clears stale state from a release outside this panel.
		DisarmExpectedTagDrag();
	}
	return FReply::Unhandled();
}

FReply SExpectedTagsPanel::OnMouseMove(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	(void)MyGeometry;
	if (!bExpectedTagDragArmed)
	{
		return FReply::Unhandled();
	}
	if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		DisarmExpectedTagDrag();
		return FReply::Unhandled();
	}
	if (!FSlateApplication::Get().HasTraveledFarEnoughToTriggerDrag(
		MouseEvent,
		ArmedDragStartPosition))
	{
		return FReply::Unhandled();
	}
	if (!IsArmedDragStillValid())
	{
		DisarmExpectedTagDrag();
		return FReply::Unhandled();
	}

	const FGameplayTag ExpectedTag = ArmedExpectedTag;
	UPaper2DPlusCharacterProfileAsset* SourceAsset = ArmedSourceAsset.Get();
	DisarmExpectedTagDrag();
	return FReply::Handled().BeginDragDrop(
		FExpectedTagDragDropOp::New(ExpectedTag, SourceAsset));
}

FReply SExpectedTagsPanel::OnMouseButtonUp(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	(void)MyGeometry;
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		DisarmExpectedTagDrag();
	}
	return FReply::Unhandled();
}

void SExpectedTagsPanel::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		DisarmExpectedTagDrag();
	}
	SCompoundWidget::OnMouseLeave(MouseEvent);
}

void SExpectedTagsPanel::HandleAnimationPicked(
	FGameplayTag ExpectedTag,
	FExpectedTagAnimationTarget Target)
{
	// Preserve all picker output before dismissing the menu. Closing can invalidate row widgets.
	const FGameplayTag ExpectedTagCopy = ExpectedTag;
	const FExpectedTagAnimationTarget TargetCopy = Target;
	if (const TWeakPtr<SComboButton>* WeakCombo =
		RowPickerCombos.Find(ExpectedTagCopy.GetTagName()))
	{
		if (const TSharedPtr<SComboButton> Combo = WeakCombo->Pin())
		{
			Combo->SetIsOpen(false);
		}
	}
	QueuePickerAssignment(ExpectedTagCopy, TargetCopy);
}

void SExpectedTagsPanel::QueuePickerAssignment(
	const FGameplayTag& ExpectedTag,
	const FExpectedTagAnimationTarget& Target)
{
	CancelPendingPickerAssignment();
	PendingPickerAssignment = FPendingPickerAssignment{ ExpectedTag, Target };
	PickerCommitTimerHandle = RegisterActiveTimer(
		0.0f,
		FWidgetActiveTimerDelegate::CreateSP(
			this,
			&SExpectedTagsPanel::FlushPickerAssignment));
}

EActiveTimerReturnType SExpectedTagsPanel::FlushPickerAssignment(
	double CurrentTime,
	float DeltaTime)
{
	(void)CurrentTime;
	(void)DeltaTime;
	PickerCommitTimerHandle.Reset();

	if (bShutdown || !PendingPickerAssignment.IsSet())
	{
		PendingPickerAssignment.Reset();
		return EActiveTimerReturnType::Stop;
	}

	const FPendingPickerAssignment Assignment =
		PendingPickerAssignment.GetValue();
	PendingPickerAssignment.Reset();
	FExpectedTagAssignment::Assign(
		Assignment.ExpectedTag,
		Assignment.Target.OwningAsset,
		Assignment.Target,
		Model);
	return EActiveTimerReturnType::Stop;
}

void SExpectedTagsPanel::CancelPendingPickerAssignment()
{
	if (const TSharedPtr<FActiveTimerHandle> Timer =
		PickerCommitTimerHandle.Pin())
	{
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
	PickerCommitTimerHandle.Reset();
	PendingPickerAssignment.Reset();
}

#if WITH_DEV_AUTOMATION_TESTS
FText SExpectedTagsPanel::GetEmptyStateTextForTests() const
{
	return GetEmptyStateText(PanelState);
}

EExpectedTagAssignmentResult
SExpectedTagsPanel::AssignTagToAnimationForTests(
	const FGameplayTag& ExpectedTag,
	int32 FlipbookIndex)
{
	UPaper2DPlusCharacterProfileAsset* Profile =
		Model.IsValid() ? Model->GetAsset() : nullptr;
	const FExpectedTagAnimationTarget Target =
		FExpectedTagAnimationTarget::Capture(Profile, FlipbookIndex);
	return FExpectedTagAssignment::Assign(
		ExpectedTag,
		Profile,
		Target,
		Model);
}

EExpectedTagsRowPresentation
SExpectedTagsPanel::GetRowPresentationForTests(
	const FCharacterCoverageRow& Row)
{
	return GetRowPresentation(Row);
}

FText SExpectedTagsPanel::GetRowStatusTextForTests(
	const FCharacterCoverageRow& Row)
{
	return GetRowStatusText(Row);
}
#endif

#undef LOCTEXT_NAMESPACE
