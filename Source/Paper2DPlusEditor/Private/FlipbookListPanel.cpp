// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FlipbookListPanel.h"
#include "CharacterProfileEditorModel.h"
#include "FlipbookListBuilder.h"
#include "AnimationProfilePickerSource.h"
#include "AnimationProfileSwitcher.h"
#include "ProfileItemPicker.h"
#include "ProfileNavigatorPanel.h"
#include "EditorCanvasUtils.h"
#include "SDragClickWrapper.h"
#include "SSpriteEditorDragDropWidgets.h"
#include "SlateShortcutUtils.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Paper2DPlusCharacterLayerAsset.h"
#include "PaperFlipbook.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SNullWidget.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "FlipbookListPanel"

void SFlipbookListPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	LayerAsset = InArgs._LayerAsset;
	AnimationPickerSource = MakeShared<FAnimationProfilePickerSource>(Model);
	if (Model.IsValid())
	{
		Asset = Model->GetAsset();

		ModelFlipbookSelectionHandle = Model->OnFlipbookSelectionChanged.AddLambda([this](int32)
		{
			if (FlipbookListBox.IsValid())
				FlipbookListBox->Invalidate(EInvalidateWidgetReason::Paint);
		});

		ModelGroupCollapseHandle = Model->OnGroupCollapseChanged.AddLambda([this]()
		{
			RefreshFlipbookList();
		});

		ModelSearchTextHandle = Model->OnSearchTextChanged.AddRaw(this, &SFlipbookListPanel::OnSharedSearchTextChanged);

		ModelAssetDataChangedHandle = Model->OnAssetDataChanged.AddLambda([this]()
		{
			Asset = Model.IsValid() ? Model->GetAsset() : nullptr;
			RefreshFlipbookList();
		});

		ModelAssetExternallyModifiedHandle = Model->OnAssetExternallyModified.AddLambda([this]()
		{
			RefreshFlipbookList();
		});
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		// Compact current-animation control is always present. Its popup owns an independent search
		// query, so opening it never filters the pinned navigator or another editor.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 4, 4, 3)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CurrentAnimationHeader", "Current Animation"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(CompactAnimationPicker, SProfileItemPicker)
				.Source(AnimationPickerSource)
				.EmptySelectionText(LOCTEXT("NoCurrentAnimation", "No animation selected"))
				.OnGenerateItemPreview(FOnGenerateProfileNavigatorItemPreview::CreateStatic(
					&BuildAnimationProfileItemPreview))
			]
		]

		// The full virtualized navigator is optional and remembers expansion per toolkit kind. Pins and
		// recents remain keyed only by Character Profile in the shared catalog store.
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(NavigatorExpandArea, SExpandableArea)
			.InitiallyCollapsed(!LoadNavigatorExpanded())
			.OnAreaExpansionChanged_Lambda([this](bool bExpanded) { SaveNavigatorExpanded(bExpanded); })
			.HeaderPadding(FMargin(4, 2))
			.HeaderContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NavigatorHeader", "Animation Navigator"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]
			.BodyContent()
			[
				SAssignNew(AnimationNavigator, SProfileNavigatorPanel)
				.Source(AnimationPickerSource)
				.Mode(EProfileNavigatorMode::Pinned)
				.OnGenerateItemPreview(FOnGenerateProfileNavigatorItemPreview::CreateStatic(
					&BuildAnimationProfileItemPreview))
				.InitialQuery(Model.IsValid() ? Model->GetFlipbookGroupSearchText() : FString())
				.OnQueryChanged(FOnProfileNavigatorQueryChanged::CreateLambda([this](const FString& Query)
				{
					if (Model.IsValid()) Model->SetFlipbookGroupSearchText(Query);
				}))
				.OnItemDoubleClicked(FOnProfileNavigatorItemDoubleClicked::CreateSP(
					this, &SFlipbookListPanel::HandleNavigatorItemDoubleClicked))
				.OnGenerateItemExtension(FOnGenerateProfileNavigatorItemExtension::CreateSP(
					this, &SFlipbookListPanel::BuildNavigatorItemExtension))
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]

		// Frame Data lives in a floating window since its tab retired (layout _v8). The sidebar is the
		// one always-visible panel, so the launcher button lives here. Read-only — no transaction.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 2, 4, 4)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("FrameDataButtonTip", "Open the frame-data table (startup/active/recovery, damage, reach, cancel windows) in a floating window"))
			.OnClicked_Lambda([this]()
			{
				if (Model.IsValid())
				{
					Model->OpenFrameDataWindow();
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FrameDataButton", "Frame Data…"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		]
	];

	RefreshFlipbookList();
}

SFlipbookListPanel::~SFlipbookListPanel()
{
	if (Model.IsValid())
	{
		Model->OnFlipbookSelectionChanged.Remove(ModelFlipbookSelectionHandle);
		Model->OnGroupCollapseChanged.Remove(ModelGroupCollapseHandle);
		Model->OnSearchTextChanged.Remove(ModelSearchTextHandle);
		Model->OnAssetDataChanged.Remove(ModelAssetDataChangedHandle);
		Model->OnAssetExternallyModified.Remove(ModelAssetExternallyModifiedHandle);
	}
}

void SFlipbookListPanel::AddToQueueAndReveal(int32 FlipbookIndex)
{
	if (!Model.IsValid()) return;
	Model->AddToQueue(FlipbookIndex);
	Model->RevealPlaybackQueue();
}

void SFlipbookListPanel::RefreshFlipbookList()
{
	if (AnimationNavigator.IsValid())
	{
		AnimationNavigator->RefreshResults();
		return;
	}
	if (!FlipbookListBox.IsValid() || !Model.IsValid() || !Asset.IsValid()) return;
	FlipbookListBox->ClearChildren();

	const FString& SearchFilter = Model->GetFlipbookGroupSearchText();

	auto ItemBuilder = [this](int32 i) -> TSharedRef<SWidget>
	{
		const FFlipbookProfileEntry& Anim = Asset->Flipbooks[i];
		UPaperFlipbook* LoadedFlipbook = Anim.Identity.Flipbook.Get();
		FString AnimName = Anim.Identity.FlipbookName;

		const int32 FrameCount = LoadedFlipbook ? LoadedFlipbook->GetNumKeyFrames() : 0;
		const FText SubtitleText = LoadedFlipbook
			? (FrameCount == 1 ? LOCTEXT("OneFrame", "1 frame")
			                   : FText::Format(LOCTEXT("NFramesFmt", "{0} frames"), FText::AsNumber(FrameCount)))
			: LOCTEXT("NoFlipbookSub", "no flipbook");

		auto IsRowSelected = [this, i]() { return Model.IsValid() && i == Model->GetSelectedFlipbookIndex(); };

		TSharedRef<SDragClickWrapper> Wrapper = SNew(SDragClickWrapper)
		[
			// Outer vertical box supplies a small gap below each row so entries read as cards.
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 3)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))   // flat fill — no muddy textured bevel
				.BorderBackgroundColor_Lambda([IsRowSelected]()
				{
					return IsRowSelected()
						? FLinearColor(0.13f, 0.18f, 0.26f, 1.0f)    // soft, desaturated selection blue
						: FLinearColor(0.065f, 0.07f, 0.08f, 1.0f);  // subtly lifted off pure black
				})
				.Padding(0)
				[
					SNew(SHorizontalBox)

					// Left selection accent bar (full row height, 3px).
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox).WidthOverride(3.0f)
						[
							SNew(SColorBlock)
							.Color_Lambda([IsRowSelected]()
							{
								return IsRowSelected()
									? FLinearColor(0.30f, 0.62f, 1.0f, 1.0f)
									: FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
							})
						]
					]

					// Framed thumbnail so the checkerboard reads as a contained preview, not noise.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(7, 5, 8, 5)
					[
						SNew(SBox)
						.WidthOverride(40)
						.HeightOverride(40)
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
							.Padding(1)
							[
								LoadedFlipbook
									? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(LoadedFlipbook))
									: StaticCastSharedRef<SWidget>(SNew(SBox)
										.HAlign(HAlign_Center).VAlign(VAlign_Center)
										[
											SNew(STextBlock)
											.Text(LOCTEXT("NoFB", "No FB"))
											.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
											.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
										])
							]
						]
					]

					// Name + dim frame-count subtitle.
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					.Padding(0, 4, 8, 4)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(AnimName))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.92f, 0.93f)))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 1, 0, 0)
						[
							SNew(STextBlock)
							.Text(SubtitleText)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.52f)))
						]
					]

					// Per-flipbook actions dropdown (▾). An SComboButton inside the SDragClickWrapper is safe:
					// Slate routes pointer input leaf-first, so the combo handles its own click before it can
					// bubble to the wrapper's row select/drag (same pattern as the browser cards' tag chips).
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 6, 0)
					[
						SNew(SComboButton)
						// Visible ▾ affordance is layer-editor-only (the requested surface); the profile editor
						// keeps the same actions on right-click via the shared BuildFlipbookRowMenu.
						.Visibility(LayerAsset.IsValid() ? EVisibility::Visible : EVisibility::Collapsed)
						.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
						.ContentPadding(FMargin(2, 0))
						.HasDownArrow(true)
						.ToolTipText(LOCTEXT("FlipbookRowMenuTip", "Actions for this animation"))
						.OnGetMenuContent_Lambda([this, i]() { return BuildFlipbookRowMenu(i); })
					]
				]
			]
		];

		Wrapper->OnClickedFunc = [this, i](const FGeometry&, const FPointerEvent&)
		{
			if (Model.IsValid()) Model->SetSelectedFlipbook(i);
		};

		Wrapper->OnRightClickedFunc = [this, i](const FGeometry&, const FPointerEvent& MouseEvent)
		{
			if (Model.IsValid()) Model->SetSelectedFlipbook(i);

			// Right-click shows the SAME menu as the row's ▾ dropdown button.
			FSlateApplication::Get().PushMenu(
				SharedThis(this),
				FWidgetPath(),
				BuildFlipbookRowMenu(i),
				MouseEvent.GetScreenSpacePosition(),
				FPopupTransitionEffect::ContextMenu);
		};

		Wrapper->OnDragDetectedFunc = [this, i, AnimName]() -> TSharedPtr<FDragDropOperation>
		{
			return FQueueDragDropOp::NewFromFlipbookList(i, AnimName,
				Model.IsValid() ? Model->GetAsset() : nullptr);
		};

		Wrapper->OnDoubleClickedFunc = [this, i]()
		{
			AddToQueueAndReveal(i);
		};

		return Wrapper;
	};

	TFunction<bool(int32)> FilterFn = nullptr;
	if (!SearchFilter.IsEmpty())
	{
		FilterFn = [this, &SearchFilter](int32 Idx) -> bool
		{
			return Asset->Flipbooks[Idx].Identity.FlipbookName.Contains(SearchFilter, ESearchCase::IgnoreCase);
		};
	}

	FFlipbookListBuilder::Build(FlipbookListBox, Model, ItemBuilder, [this]() { RefreshFlipbookList(); }, FilterFn);
}

TSharedRef<SWidget> SFlipbookListPanel::BuildNavigatorItemExtension(const FProfileItemIdentity& Identity)
{
	const int32 Index = AnimationPickerSource.IsValid() ? AnimationPickerSource->ResolveIndex(Identity) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return SNullWidget::NullWidget;
	}
	const TWeakPtr<SFlipbookListPanel> WeakPanel = SharedThis(this);
	return SNew(SComboButton)
		.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
		.ContentPadding(FMargin(2, 0))
		.HasDownArrow(true)
		.ToolTipText(LOCTEXT("NavigatorRowActions", "Animation actions"))
		.AccessibleText(LOCTEXT("NavigatorRowActionsAccessible", "Open animation actions"))
		.OnGetMenuContent_Lambda([WeakPanel, Identity]()
		{
			const TSharedPtr<SFlipbookListPanel> Self = WeakPanel.Pin();
			if (!Self.IsValid()) return SNullWidget::NullWidget;
			const int32 CurrentIndex = Self->AnimationPickerSource.IsValid()
				? Self->AnimationPickerSource->ResolveIndex(Identity) : INDEX_NONE;
			return CurrentIndex == INDEX_NONE ? SNullWidget::NullWidget : Self->BuildFlipbookRowMenu(CurrentIndex);
		});
}

void SFlipbookListPanel::HandleNavigatorItemDoubleClicked(const FProfileItemIdentity& Identity)
{
	const int32 Index = AnimationPickerSource.IsValid() ? AnimationPickerSource->ResolveIndex(Identity) : INDEX_NONE;
	if (Index != INDEX_NONE)
	{
		AddToQueueAndReveal(Index);
	}
}

bool SFlipbookListPanel::LoadNavigatorExpanded() const
{
	bool bExpanded = true;
	if (GConfig)
	{
		const TCHAR* Key = LayerAsset.IsValid() ? TEXT("LayerNavigatorExpanded") : TEXT("CharacterNavigatorExpanded");
		GConfig->GetBool(TEXT("Paper2DPlus.ProfileEditorLayout"), Key, bExpanded, GEditorPerProjectIni);
	}
	return bExpanded;
}

#if WITH_DEV_AUTOMATION_TESTS
int32 SFlipbookListPanel::GenerateNavigatorRowsForTests(const FVector2D& ViewportSize)
{
	return AnimationNavigator.IsValid()
		? AnimationNavigator->GenerateRowsForViewportForTests(ViewportSize)
		: 0;
}

int32 SFlipbookListPanel::GetNavigatorPreviewCountForTests() const
{
	return AnimationNavigator.IsValid()
		? AnimationNavigator->GetConstructedPreviewCountForTests()
		: 0;
}
#endif

void SFlipbookListPanel::SaveNavigatorExpanded(bool bExpanded) const
{
	if (!GConfig) return;
	const TCHAR* Key = LayerAsset.IsValid() ? TEXT("LayerNavigatorExpanded") : TEXT("CharacterNavigatorExpanded");
	GConfig->SetBool(TEXT("Paper2DPlus.ProfileEditorLayout"), Key, bExpanded, GEditorPerProjectIni);
	// Intentionally no explicit Flush; toolkit layout is cosmetic and engine-owned persistence is safer.
}

TSharedRef<SWidget> SFlipbookListPanel::BuildFlipbookRowMenu(int32 FlipbookIndex)
{
	// A popup can outlive reorder/rename/delete. Capture the shared object-path identity (name fallback),
	// never the build-time array index, and weakly pin the host for every delayed menu command.
	FProfileItemIdentity StableIdentity;
	StableIdentity.SourceType = AnimationPickerSource.IsValid()
		? AnimationPickerSource->GetSourceType() : FName(TEXT("Animation"));
	if (Asset.IsValid() && Asset->Flipbooks.IsValidIndex(FlipbookIndex))
	{
		const FFlipbookProfileEntry& Entry = Asset->Flipbooks[FlipbookIndex];
		StableIdentity.ObjectPath = Entry.Identity.Flipbook.ToSoftObjectPath();
		StableIdentity.FallbackKey = Entry.Identity.FlipbookName;
	}
	const TWeakPtr<SFlipbookListPanel> WeakPanel = SharedThis(this);

	FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("FlipbookMenuHeader", "Animation"));

	// Preview — select this flipbook so the editor scopes to it.
	MenuBuilder.AddMenuEntry(
		LOCTEXT("FlipbookMenuPreview", "Preview"),
		LOCTEXT("FlipbookMenuPreviewTip", "Select this animation to preview it and scope the editor to it."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakPanel, StableIdentity]()
		{
			const TSharedPtr<SFlipbookListPanel> Self = WeakPanel.Pin();
			if (!Self.IsValid()) return;
			const int32 Idx = Self->AnimationPickerSource.IsValid()
				? Self->AnimationPickerSource->ResolveIndex(StableIdentity) : INDEX_NONE;
			if (Idx != INDEX_NONE && Self->Model.IsValid()) Self->Model->SetSelectedFlipbook(Idx);
		}))
	);

	// Add to Queue — same action as the legacy right-click, mirrored here so the ▾ dropdown is a superset.
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddToQueue", "Add to Queue"),
		LOCTEXT("FlipbookMenuQueueTip", "Append this animation to the playback queue."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakPanel, StableIdentity]()
		{
			const TSharedPtr<SFlipbookListPanel> Self = WeakPanel.Pin();
			if (!Self.IsValid()) return;
			const int32 Idx = Self->AnimationPickerSource.IsValid()
				? Self->AnimationPickerSource->ResolveIndex(StableIdentity) : INDEX_NONE;
			if (Idx != INDEX_NONE)
			{
				Self->AddToQueueAndReveal(Idx);
			}
		}))
	);

	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

FReply SFlipbookListPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Paper2DPlusEditor::SlateShortcutUtils::ShouldIgnoreShortcutForFocusedWidget())
	{
		return FReply::Unhandled();
	}

	if (InKeyEvent.GetKey() == EKeys::SpaceBar && !InKeyEvent.IsControlDown())
	{
		if (Model.IsValid() && Model->IsQueueActive())
		{
			Model->SetQueuePlaying(!Model->IsQueuePlaying());
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

void SFlipbookListPanel::OnSharedSearchTextChanged(const FString& NewText)
{
	if (AnimationNavigator.IsValid())
	{
		AnimationNavigator->SetQueryForTests(NewText);
	}
	else
	{
		RefreshFlipbookList();
	}
}

#undef LOCTEXT_NAMESPACE
