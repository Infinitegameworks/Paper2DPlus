// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "EffectProfileEditor/EffectProfileLibraryPanel.h"

#include "AnimationTagChipUtils.h"
#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Editor.h"
#include "EditorCanvasUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IContentBrowserSingleton.h"
#include "Misc/EngineVersionComparison.h"
#include "Modules/ModuleManager.h"
#include "PaperFlipbook.h"
#include "ProfileCard.h"
#include "ProfileNavigatorPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWindow.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
#include "SGameplayTagPicker.h"
#endif
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "EffectProfileLibraryPanel"

void SEffectProfileLibraryPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	OnOpenValidation = InArgs._OnOpenValidation;
	LastIntakeMessage = LOCTEXT("IntakeReady", "Ready. Add existing flipbooks or drop them anywhere in this Library.");
	if (Model.IsValid())
	{
		SourceChangedHandle = Model->OnSourceChanged().AddSP(this, &SEffectProfileLibraryPanel::HandleModelChanged);
	}

	const TSharedPtr<IProfileItemPickerSource> PickerSource =
		StaticCastSharedPtr<IProfileItemPickerSource>(Model);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(4.0f)
		.AccessibleText(LOCTEXT("LibraryAccessible", "Effect flipbook library. Search, filter, select, add, or drop flipbooks."))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("LibraryHint", "Search, filter, or drop flipbooks"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f)
				[
					SNew(SButton)
					.ContentPadding(FMargin(5.0f, 1.0f))
					.Text(LOCTEXT("AddExisting", "+ Add Flipbooks…"))
					.ToolTipText(LOCTEXT("AddExistingTip", "Choose one or many Paper Flipbooks. Existing members and incompatible assets are reported and skipped."))
					.AccessibleText(LOCTEXT("AddExistingAccessible", "Add one or more existing flipbooks to this effect library"))
					.OnClicked(this, &SEffectProfileLibraryPanel::HandleAddExistingClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ContentPadding(FMargin(4.0f, 1.0f))
					.Text(LOCTEXT("OpenValidation", "Validate"))
					.ToolTipText(LOCTEXT("OpenValidationTip", "Open the shared validation panel without repairing the asset."))
					.AccessibleText(LOCTEXT("OpenValidationAccessible", "Open read-only Effect Profile validation"))
					.OnClicked(this, &SEffectProfileLibraryPanel::HandleValidationClicked)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				BuildDescriptorFilterControl()
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SAssignNew(Navigator, SProfileNavigatorPanel)
					.Source(PickerSource)
					.Mode(EProfileNavigatorMode::Pinned)
					.OnItemDoubleClicked(FOnProfileNavigatorItemDoubleClicked::CreateSP(
						this, &SEffectProfileLibraryPanel::HandleItemDoubleClicked))
					.OnGenerateItemPreview(FOnGenerateProfileNavigatorItemPreview::CreateSP(
						this, &SEffectProfileLibraryPanel::BuildItemPreview))
					.OnGenerateItemContent(FOnGenerateProfileNavigatorItemContent::CreateSP(
						this, &SEffectProfileLibraryPanel::BuildItemContent))
					.OnGenerateItemExtension(FOnGenerateProfileNavigatorItemExtension::CreateSP(
						this, &SEffectProfileLibraryPanel::BuildRowExtension))
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SNew(SBorder)
					.Visibility(this, &SEffectProfileLibraryPanel::GetEmptyLibraryVisibility)
					.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
					.Padding(14.0f)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.AccessibleText(LOCTEXT("EmptyAccessible", "The Effect Library is empty. Add existing flipbooks or drag them here."))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("EmptyTitle", "This visual library is empty"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 5.0f, 0.0f, 10.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("EmptyGuidance", "Add existing Paper Flipbooks, or drag a Content Browser selection into this panel.\nThe first accepted flipbook becomes the selected effect."))
							.Justification(ETextJustify::Center)
							.AutoWrapText(true)
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							SNew(SButton)
							.Text(LOCTEXT("EmptyAdd", "Add Existing Flipbooks..."))
							.OnClicked(this, &SEffectProfileLibraryPanel::HandleAddExistingClicked)
						]
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f, 3.0f, 2.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return bDragOver ? GetDragStateText() : LastIntakeMessage; })
				.ToolTipText_Lambda([this]() { return bDragOver ? GetDragStateText() : LastIntakeMessage; })
				.AutoWrapText(false)
				.Clipping(EWidgetClipping::ClipToBounds)
				.Visibility_Lambda([this]()
				{
					return bDragOver || bHasIntakeResult
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				.AccessibleText_Lambda([this]() { return bDragOver ? GetDragStateText() : LastIntakeMessage; })
			]
		]
	];
}

SEffectProfileLibraryPanel::~SEffectProfileLibraryPanel()
{
	if (Model.IsValid())
	{
		Model->OnSourceChanged().Remove(SourceChangedHandle);
	}
}

void SEffectProfileLibraryPanel::HandleModelChanged()
{
	++RefreshCount;
	Invalidate(EInvalidateWidgetReason::Layout);
}

FReply SEffectProfileLibraryPanel::HandleAddExistingClicked()
{
	OpenAssetPicker();
	return FReply::Handled();
}

FReply SEffectProfileLibraryPanel::HandleValidationClicked()
{
	OnOpenValidation.ExecuteIfBound();
	return FReply::Handled();
}

void SEffectProfileLibraryPanel::OpenAssetPicker()
{
	if (!Model.IsValid())
	{
		return;
	}
	FContentBrowserModule& ContentBrowserModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	FAssetPickerConfig PickerConfig;
	PickerConfig.SelectionMode = ESelectionMode::Multi;
	PickerConfig.Filter.bRecursiveClasses = true;
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
		.Title(LOCTEXT("PickerTitle", "Add Effect Flipbooks"))
		.ClientSize(FVector2D(760.0f, 560.0f))
		.SupportsMaximize(true)
		.SupportsMinimize(false);
	const TWeakPtr<SWindow> WeakWindow = Window;
	const TWeakPtr<SEffectProfileLibraryPanel> WeakPanel = SharedThis(this);
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
				.Text(LOCTEXT("PickerAdd", "Add Selected"))
				.OnClicked_Lambda([WeakPanel, WeakWindow, GetSelection]()
				{
					if (TSharedPtr<SEffectProfileLibraryPanel> Self = WeakPanel.Pin())
					{
						const TArray<FAssetData> Selected = GetSelection->IsBound()
							? GetSelection->Execute() : TArray<FAssetData>();
						Self->ApplyIntakeResult(Self->Model->AddAssetData(Selected));
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
				.Text(LOCTEXT("PickerCancel", "Cancel"))
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

void SEffectProfileLibraryPanel::ApplyIntakeResult(const FEffectProfileIntakeResult& Result)
{
	LastIntakeMessage = Result.BuildSummary();
	bHasIntakeResult = true;
	FNotificationInfo Info(LastIntakeMessage);
	Info.ExpireDuration = Result.HasRejections() ? 5.0f : 3.0f;
	if (TSharedPtr<SNotificationItem> Notification =
		FSlateNotificationManager::Get().AddNotification(Info))
	{
		Notification->SetCompletionState(Result.HasRejections()
			? SNotificationItem::CS_Pending
			: SNotificationItem::CS_Success);
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SEffectProfileLibraryPanel::OnDragEnter(
	const FGeometry& MyGeometry,
	const FDragDropEvent& DragDropEvent)
{
	if (DragDropEvent.GetOperationAs<FAssetDragDropOp>().IsValid())
	{
		if (!bDragOver)
		{
			bDragOver = true;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
	}
	SCompoundWidget::OnDragEnter(MyGeometry, DragDropEvent);
}

void SEffectProfileLibraryPanel::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	if (bDragOver)
	{
		bDragOver = false;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	SCompoundWidget::OnDragLeave(DragDropEvent);
}

FReply SEffectProfileLibraryPanel::OnDragOver(
	const FGeometry& MyGeometry,
	const FDragDropEvent& DragDropEvent)
{
	return DragDropEvent.GetOperationAs<FAssetDragDropOp>().IsValid()
		? FReply::Handled()
		: SCompoundWidget::OnDragOver(MyGeometry, DragDropEvent);
}

FReply SEffectProfileLibraryPanel::OnDrop(
	const FGeometry& MyGeometry,
	const FDragDropEvent& DragDropEvent)
{
	if (bDragOver)
	{
		bDragOver = false;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	const TSharedPtr<FAssetDragDropOp> Operation =
		DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	if (!Operation.IsValid() || !Model.IsValid())
	{
		return SCompoundWidget::OnDrop(MyGeometry, DragDropEvent);
	}
	ApplyIntakeResult(Model->AddAssetData(Operation->GetAssets()));
	return FReply::Handled();
}

FText SEffectProfileLibraryPanel::GetDragStateText() const
{
	return LOCTEXT("DropState", "Drop now to add valid flipbooks. Duplicates and incompatible assets will be named in the result.");
}

TSharedRef<SWidget> SEffectProfileLibraryPanel::BuildDescriptorFilterControl()
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	TSharedRef<TWeakPtr<SComboButton>> WeakComboHolder = MakeShared<TWeakPtr<SComboButton>>();
	const TWeakPtr<SEffectProfileLibraryPanel> WeakPanel = SharedThis(this);
	TSharedRef<SComboButton> Combo = SNew(SComboButton)
		.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
		.ContentPadding(FMargin(6.0f, 2.0f))
		.HasDownArrow(true)
		.ToolTipText(LOCTEXT("DescriptorFilterTip", "Require all selected Paper2DPlus.Effect.Descriptor tags. Search also matches descriptor leaf and full paths."))
		.AccessibleText_Lambda([this]()
		{
			const int32 Count = Model.IsValid() ? Model->GetDescriptorFilter().Num() : 0;
			return FText::Format(LOCTEXT("DescriptorFilterAccessible", "Descriptor filter. {0} selected."), FText::AsNumber(Count));
		})
		.OnGetMenuContent_Lambda([WeakPanel, WeakComboHolder]()
		{
			const TSharedPtr<SEffectProfileLibraryPanel> Self = WeakPanel.Pin();
			const FGameplayTagContainer Snapshot = Self.IsValid() && Self->Model.IsValid()
				? Self->Model->GetDescriptorFilter() : FGameplayTagContainer();
			return SNew(SBox)
				.MinDesiredWidth(330.0f)
				.Padding(2.0f)
				[
					SNew(SMenuHostedTagPickerGuard)
					[
						SNew(SGameplayTagPicker)
						.Filter(TEXT("Paper2DPlus.Effect.Descriptor"))
						.MultiSelect(true)
						.TagContainers(TArray<FGameplayTagContainer>{ Snapshot })
						.OnTagChanged_Lambda([WeakPanel, WeakComboHolder](const TArray<FGameplayTagContainer>& Containers)
						{
							const FGameplayTagContainer NewFilter = Containers.Num() > 0
								? Containers[0] : FGameplayTagContainer();
							if (TSharedPtr<SComboButton> PinnedCombo = WeakComboHolder->Pin())
							{
								PinnedCombo->SetIsOpen(false);
							}
							if (GEditor)
							{
								GEditor->GetTimerManager()->SetTimerForNextTick([WeakPanel, NewFilter]()
								{
									if (TSharedPtr<SEffectProfileLibraryPanel> Pinned = WeakPanel.Pin())
									{
										Pinned->Model->SetDescriptorFilter(NewFilter);
									}
								});
							}
							else if (TSharedPtr<SEffectProfileLibraryPanel> Pinned = WeakPanel.Pin())
							{
								Pinned->Model->SetDescriptorFilter(NewFilter);
							}
						})
					]
				];
		})
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				const int32 Count = Model.IsValid() ? Model->GetDescriptorFilter().Num() : 0;
				return Count == 0
					? LOCTEXT("AllDescriptors", "Descriptor filter: All")
					: FText::Format(LOCTEXT("DescriptorCount", "Descriptor filter: {0} selected"), FText::AsNumber(Count));
			})
		];
	*WeakComboHolder = Combo;
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f)[ Combo ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("ClearDescriptorFilter", "Clear filter"))
			.IsEnabled_Lambda([this]() { return Model.IsValid() && !Model->GetDescriptorFilter().IsEmpty(); })
			.OnClicked_Lambda([this]() { if (Model.IsValid()) Model->ClearDescriptorFilter(); return FReply::Handled(); })
		];
#else
	return SNew(STextBlock)
		.Text(LOCTEXT("DescriptorSearchFallback", "Descriptor filter: type a descriptor name or full tag path in Search. Advanced tag picking remains available in Advanced Details."))
		.AutoWrapText(true);
#endif
}

TSharedRef<SWidget> SEffectProfileLibraryPanel::BuildRowExtension(
	const FProfileItemIdentity& Identity)
{
	if (!Model.IsValid())
	{
		return SNullWidget::NullWidget;
	}
	const FText IssueText = Model->GetRowIssueStateText(Identity.ObjectPath);
	return SNew(STextBlock)
		.Text(IssueText)
		.Visibility(IssueText.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)
		.ToolTipText(LOCTEXT("IssueBadgeTip", "This row has validation findings. Open Validation for remediation."))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8));
}

TSharedPtr<SWidget> SEffectProfileLibraryPanel::BuildItemPreview(
	const FProfilePickerItem& Item)
{
	UPaperFlipbook* Flipbook = Model.IsValid() ? Model->ResolveFlipbook(Item.Identity) : nullptr;
	return SNew(SBox)
		.WidthOverride(SProfileCard::GetCanonicalThumbnailSize())
		.HeightOverride(SProfileCard::GetCanonicalThumbnailSize())
		[
			Flipbook
				? StaticCastSharedRef<SWidget>(SNew(SFlipbookThumbnail).Flipbook(Flipbook))
				: StaticCastSharedRef<SWidget>(
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NoThumbnail", "No preview"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
					])
		];
}

TSharedRef<SWidget> SEffectProfileLibraryPanel::BuildItemContent(
	const FProfileItemIdentity& Identity,
	const TSharedRef<SWidget>& DefaultContent)
{
	const TWeakPtr<FEffectProfileEditorModel> WeakModel = Model;
	return SNew(SProfileCard)
		.IsSelected_Lambda([WeakModel, Identity]()
		{
			const TSharedPtr<FEffectProfileEditorModel> PinnedModel = WeakModel.Pin();
			return PinnedModel.IsValid()
				&& PinnedModel->GetSelectedIdentity().Matches(Identity);
		})
		[
			DefaultContent
		];
}

void SEffectProfileLibraryPanel::HandleItemDoubleClicked(
	const FProfileItemIdentity& Identity)
{
	if (Model.IsValid())
	{
		Model->SelectItem(Identity);
		Model->OpenSelectedSource();
	}
}

EVisibility SEffectProfileLibraryPanel::GetEmptyLibraryVisibility() const
{
	return IsShowingEmptyStateForTests() ? EVisibility::Visible : EVisibility::Collapsed;
}

bool SEffectProfileLibraryPanel::IsShowingEmptyStateForTests() const
{
	return !Model.IsValid() || Model->GetLibraryEntryCount() == 0;
}

int32 SEffectProfileLibraryPanel::GetResultCountForTests() const
{
	return Navigator.IsValid() ? Navigator->GetResultCountForTests() : 0;
}

int32 SEffectProfileLibraryPanel::GenerateRowsForViewportForTests(
	const FVector2D& ViewportSize)
{
	return Navigator.IsValid()
		? Navigator->GenerateRowsForViewportForTests(ViewportSize) : 0;
}

#undef LOCTEXT_NAMESPACE
