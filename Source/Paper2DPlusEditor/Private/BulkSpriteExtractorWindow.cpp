// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "BulkSpriteExtractorWindow.h"
#include "SpriteExtractorWindow.h"
#include "Engine/Texture2D.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Framework/Application/SlateApplication.h"
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
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "CharacterProfileAssetFactory.h"
#include "ObjectTools.h"
#include "Widgets/Input/SCheckBox.h"

/** SBulkSpriteExtractorWindow — Multi-texture batch sprite extraction with grid detection, auto-pad, folder organization, and cross-sheet alignment. */

#define LOCTEXT_NAMESPACE "BulkSpriteExtractor"

namespace BulkSpriteExtractor_Internal
{
	/** Single-instance tracking — module-level weak ref to the active window.
	 *  OpenBulkExtractor() focuses the existing window if this resolves; otherwise opens a new one. */
	static TWeakPtr<SWindow> ActiveWindow;

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
}

FReply SBulkSpriteExtractorWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (Key == EKeys::Enter)
	{
		return OnAcceptGridClicked();
	}

	if (Key == EKeys::Left || Key == EKeys::Right)
	{
		if (TextureStates.Num() <= 1) return FReply::Handled();
		const int32 CurrentIndex = SelectedTexture.IsValid() ? TextureStates.IndexOfByKey(SelectedTexture) : 0;
		const int32 Delta = (Key == EKeys::Left) ? -1 : 1;
		const int32 NextIndex = (CurrentIndex + Delta + TextureStates.Num()) % TextureStates.Num();
		SelectTextureByIndex(NextIndex);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SBulkSpriteExtractorWindow::SelectTextureByIndex(int32 Index)
{
	if (!TextureStates.IsValidIndex(Index)) return;
	SelectedTexture = TextureStates[Index];
	if (TextureListView.IsValid())
	{
		TextureListView->SetSelection(SelectedTexture, ESelectInfo::Direct);
	}
	OnTextureSelectionChanged(SelectedTexture, ESelectInfo::Direct);
}

void SBulkSpriteExtractorWindow::ReRunDetectionOnSelected()
{
	if (!SelectedTexture.IsValid()) return;
	SelectedTexture->bDetectionRun = false;
	SelectedTexture->Status = EBulkExtractorTextureStatus::Pending;
	CachedPadCount = -1;
	RunDetectionAndInference(SelectedTexture);
	if (CenterCanvas.IsValid())
	{
		CenterCanvas->SetDetectedSprites(SelectedTexture->DetectedSprites);
		CenterCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	}
	if (TextureListView.IsValid()) TextureListView->RequestListRefresh();
}

void SBulkSpriteExtractorWindow::Construct(const FArguments& InArgs)
{
	TargetProfile = InArgs._TargetProfile;

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
	if (TextureStates.Num() > 0)
	{
		SelectedTexture = TextureStates[0];
	}

	// ----- Left pane: texture list -----
	TextureListView = SNew(SListView<TSharedPtr<FBulkExtractorTextureState>>)
		.ListItemsSource(&TextureStates)
		.OnGenerateRow(this, &SBulkSpriteExtractorWindow::GenerateTextureRow)
		.OnSelectionChanged(this, &SBulkSpriteExtractorWindow::OnTextureSelectionChanged)
		.SelectionMode(ESelectionMode::Single);

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
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4, 0, 4, 4)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				TextureListView.ToSharedRef()
			]
		];

	// ----- Center pane: canvas (live detection + grid overlay) -----
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
				return FText::FromString(SelectedTexture->Texture.GetAssetName());
			})
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
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
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
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
						if (UPaper2DPlusCharacterProfileAsset* Profile = Cast<UPaper2DPlusCharacterProfileAsset>(AssetData.GetAsset()))
						{
							TargetProfile = Profile;
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
			.Visibility_Lambda([this]() { return (bLinkToProfile && FolderAssignments.Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed; })
			.IsChecked_Lambda([this]() { return bAutoCreateGroups ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bAutoCreateGroups = (NewState == ECheckBoxState::Checked); })
			.ToolTipText(LOCTEXT("AutoCreateGroupsTip", "Create flipbook groups on the profile matching the folder organizer structure"))
			[
				SNew(STextBlock).Text(LOCTEXT("AutoCreateGroupsLabel", "Auto-create groups from folders"))
			]
		]
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
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
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
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(6)
			[
				SNew(SVerticalBox)
				// Mode toggle
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("DetectionModeLabel", "Mode"))
					]
					+ SHorizontalBox::Slot().FillWidth(0.5f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
							.Text(LOCTEXT("IslandMode", "Island"))
							.ButtonColorAndOpacity_Lambda([this]() { return bUseGridDetection ? FLinearColor(0.15f, 0.15f, 0.15f) : FLinearColor(0.2f, 0.4f, 0.7f); })
							.OnClicked_Lambda([this]() -> FReply { bUseGridDetection = false; ReRunDetectionOnSelected(); return FReply::Handled(); })
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0, 0, 0)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
							.Text(LOCTEXT("GridMode", "Grid"))
							.ButtonColorAndOpacity_Lambda([this]() { return bUseGridDetection ? FLinearColor(0.2f, 0.4f, 0.7f) : FLinearColor(0.15f, 0.15f, 0.15f); })
							.OnClicked_Lambda([this]() -> FReply { bUseGridDetection = true; ReRunDetectionOnSelected(); return FReply::Handled(); })
						]
					]
				]
				// Grid mode: columns
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]() { return bUseGridDetection ? EVisibility::Visible : EVisibility::Collapsed; })
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
					.Visibility_Lambda([this]() { return bUseGridDetection ? EVisibility::Visible : EVisibility::Collapsed; })
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
					.Visibility_Lambda([this]() { return bUseGridDetection ? EVisibility::Collapsed : EVisibility::Visible; })
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
					.Visibility_Lambda([this]() { return bUseGridDetection ? EVisibility::Collapsed : EVisibility::Visible; })
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
					.Visibility_Lambda([this]() { return bUseGridDetection ? EVisibility::Collapsed : EVisibility::Visible; })
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
					.Text_Lambda([this]() { return bUseGridDetection ? LOCTEXT("ApplyGridBtn", "Apply Grid") : LOCTEXT("ReDetectButton", "Re-Detect"); })
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
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
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

	// ----- Bottom bar: Batch Rename / Auto-Pad / Extract All -----
	const FText ExtractLabel = LOCTEXT("ExtractAllButton", "Extract All");

	TSharedRef<SHorizontalBox> BottomBar = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f)
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
			.ToolTipText(LOCTEXT("ExtractAllTooltip", "Extract sprites + flipbooks from all textures. If a CharacterProfile is set, attaches flipbooks to it."))
			.OnClicked(this, &SBulkSpriteExtractorWindow::OnCommitClicked)
			.IsEnabled_Lambda([this]() -> bool
			{
				if (!AreAllGridsConfirmed()) return false;
				return true;
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
			+ SSplitter::Slot().Value(0.62f) [ CenterPane ]
			+ SSplitter::Slot().Value(0.17f) [ RightPane ]
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
				return EActiveTimerReturnType::Stop;
			}));
	}
}

TSharedRef<ITableRow> SBulkSpriteExtractorWindow::GenerateTextureRow(
	TSharedPtr<FBulkExtractorTextureState> InItem,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const FString DisplayName = InItem.IsValid() && !InItem->Texture.IsNull()
		? InItem->Texture.GetAssetName()
		: TEXT("(null texture)");

	return SNew(STableRow<TSharedPtr<FBulkExtractorTextureState>>, OwnerTable)
	[
		SNew(SHorizontalBox)
		// Status chip (colored dot + label)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 2)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor_Lambda([InItem]()
			{
				return InItem.IsValid() ? FSlateColor(GetStatusColor(InItem->Status)) : FSlateColor(FLinearColor::Gray);
			})
			.Padding(FMargin(6, 2))
			[
				SNew(STextBlock)
				.Text_Lambda([InItem]()
				{
					return InItem.IsValid() ? GetStatusLabel(InItem->Status) : FText::FromString(TEXT("?"));
				})
				.ColorAndOpacity(FSlateColor(FLinearColor::Black))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
			]
		]
		// Texture name
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(4, 2)
		[
			SNew(STextBlock)
			.Text(FText::FromString(DisplayName))
		]
		// Remove button
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.OnClicked_Lambda([this, InItem]() -> FReply
			{
				TextureStates.Remove(InItem);
				if (SelectedTexture == InItem)
				{
					SelectedTexture = TextureStates.Num() > 0 ? TextureStates[0] : nullptr;
					if (SelectedTexture.IsValid()) SelectTextureByIndex(0);
				}
				if (TextureListView.IsValid()) TextureListView->RequestListRefresh();
				return FReply::Handled();
			})
			.ToolTipText(LOCTEXT("RemoveTextureTooltip", "Remove this texture from the batch"))
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("\u00D7")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.3f, 0.3f)))
			]
		]
	];
}

void SBulkSpriteExtractorWindow::OnTextureSelectionChanged(
	TSharedPtr<FBulkExtractorTextureState> InItem,
	ESelectInfo::Type SelectInfo)
{
	SelectedTexture = InItem;
	if (!SelectedTexture.IsValid()) return;

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

	// Point the canvas at the new texture + sprite array.
	if (CenterCanvas.IsValid())
	{
		CenterCanvas->SetTexture(SelectedTexture->Texture.LoadSynchronous());
		CenterCanvas->SetDetectedSprites(SelectedTexture->DetectedSprites);
		CenterCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	}

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

	if (bUseGridDetection)
	{
		const FIntPoint TexDims = FSpriteExtractionUtils::GetDetectionDimensions(Texture);
		const int32 Cols = FMath::Max(1, GridSpriteCount);
		const int32 Rows = FMath::Max(1, GridRowCount);
		const int32 CellW = TexDims.X / Cols;
		const int32 CellH = TexDims.Y / Rows;

		State->DetectedSprites.Empty();
		int32 Idx = 0;
		for (int32 Row = 0; Row < Rows; Row++)
		{
			for (int32 Col = 0; Col < Cols; Col++)
			{
				FDetectedSprite DS;
				DS.Bounds = FIntRect(Col * CellW, Row * CellH, (Col + 1) * CellW, (Row + 1) * CellH);
				DS.OriginalBounds = DS.Bounds;
				DS.bSelected = true;
				DS.Index = Idx++;
				State->DetectedSprites.Add(DS);
			}
		}

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

		if (State->DetectedSprites.Num() < 2)
		{
			State->Status = EBulkExtractorTextureStatus::Error;
			return;
		}

		State->InferredGrid = FSpriteExtractionUtils::InferGridDimensions(State->DetectedSprites);
		State->OverrideGrid = State->InferredGrid;
		State->Status = EBulkExtractorTextureStatus::Inferred;
	}
}

FReply SBulkSpriteExtractorWindow::OnAcceptGridClicked()
{
	if (!SelectedTexture.IsValid() || !SelectedTexture->bDetectionRun) return FReply::Handled();

	SelectedTexture->Status = EBulkExtractorTextureStatus::Confirmed;
	SelectedTexture->ConfirmedDetectionParams = DetectionParams;
	CachedPadCount = -1;

	// Force the left-pane row + canvas overlay to reflect the new state.
	if (TextureListView.IsValid()) TextureListView->RequestListRefresh();
	if (CenterCanvas.IsValid()) CenterCanvas->Invalidate(EInvalidateWidgetReason::Paint);

	return FReply::Handled();
}

void SBulkSpriteExtractorWindow::OnColsChanged(int32 NewValue)
{
	if (!SelectedTexture.IsValid() || NewValue <= 0) return;
	SelectedTexture->OverrideGrid.X = NewValue;
	SelectedTexture->bUserOverridden = true;
	CachedPadCount = -1;
	if (SelectedTexture->Status == EBulkExtractorTextureStatus::Confirmed
		|| SelectedTexture->Status == EBulkExtractorTextureStatus::Inferred)
	{
		SelectedTexture->Status = EBulkExtractorTextureStatus::Overridden;
	}
	if (CenterCanvas.IsValid()) CenterCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	if (TextureListView.IsValid()) TextureListView->RequestListRefresh();
}

void SBulkSpriteExtractorWindow::OnRowsChanged(int32 NewValue)
{
	if (!SelectedTexture.IsValid() || NewValue <= 0) return;
	SelectedTexture->OverrideGrid.Y = NewValue;
	SelectedTexture->bUserOverridden = true;
	CachedPadCount = -1;
	if (SelectedTexture->Status == EBulkExtractorTextureStatus::Confirmed
		|| SelectedTexture->Status == EBulkExtractorTextureStatus::Inferred)
	{
		SelectedTexture->Status = EBulkExtractorTextureStatus::Overridden;
	}
	if (CenterCanvas.IsValid()) CenterCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	if (TextureListView.IsValid()) TextureListView->RequestListRefresh();
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

bool SBulkSpriteExtractorWindow::IsCurrentGridDivisible() const
{
	if (!SelectedTexture.IsValid()) return false;
	UTexture2D* Tex = SelectedTexture->Texture.LoadSynchronous();
	if (!Tex) return false;
	const FIntPoint Dims = FSpriteExtractionUtils::GetDetectionDimensions(Tex);
	const FIntPoint Grid = SelectedTexture->GetEffectiveGrid();
	if (Grid.X <= 0 || Grid.Y <= 0) return false;
	return (Dims.X % Grid.X == 0) && (Dims.Y % Grid.Y == 0);
}

FText SBulkSpriteExtractorWindow::GetDivisibilityWarning() const
{
	if (!SelectedTexture.IsValid() || !SelectedTexture->bDetectionRun) return FText::GetEmpty();
	UTexture2D* Tex = SelectedTexture->Texture.LoadSynchronous();
	if (!Tex) return FText::GetEmpty();
	const FIntPoint Dims = FSpriteExtractionUtils::GetDetectionDimensions(Tex);
	const FIntPoint Grid = SelectedTexture->GetEffectiveGrid();
	if (Grid.X <= 0 || Grid.Y <= 0) return FText::GetEmpty();
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
		if (S->Status != EBulkExtractorTextureStatus::Confirmed
			&& S->Status != EBulkExtractorTextureStatus::SkippedNoPad
			&& S->Status != EBulkExtractorTextureStatus::Padded)
		{
			return false;
		}
	}
	return true;
}

TMap<UTexture2D*, FIntPoint> SBulkSpriteExtractorWindow::BuildPerTextureCellSizeMap() const
{
	TMap<UTexture2D*, FIntPoint> Out;
	for (const TSharedPtr<FBulkExtractorTextureState>& S : TextureStates)
	{
		if (!S.IsValid()) continue;
		UTexture2D* Tex = S->Texture.LoadSynchronous();
		if (!Tex) continue;
		const FIntPoint Dims = FSpriteExtractionUtils::GetDetectionDimensions(Tex);
		const FIntPoint Grid = S->GetEffectiveGrid();
		if (Grid.X <= 0 || Grid.Y <= 0) continue;
		const FIntPoint CellSize(Dims.X / Grid.X, Dims.Y / Grid.Y);
		UE_LOG(LogTemp, Log, TEXT("BulkExtractor CellSizeMap: %s — TexDims=%dx%d Grid=%dx%d CellSize=%dx%d"),
			*Tex->GetName(), Dims.X, Dims.Y, Grid.X, Grid.Y, CellSize.X, CellSize.Y);
		Out.Add(Tex, CellSize);
	}
	return Out;
}

FReply SBulkSpriteExtractorWindow::OnAutoPadClicked()
{
	// 1. Compute per-axis group max from confirmed per-texture cell sizes.
	const TMap<UTexture2D*, FIntPoint> CellSizeMap = BuildPerTextureCellSizeMap();
	const FIntPoint MaxCell = FSpriteExtractionUtils::ComputeGroupMaxCellSize(CellSizeMap);

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
		if (TextureListView.IsValid()) TextureListView->RequestListRefresh();
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
				FString SizeInfo = CellSize
					? FString::Printf(TEXT("%dx%d -> %dx%d"), CellSize->X, CellSize->Y, MaxCell.X, MaxCell.Y)
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
					.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
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
								.Image(FAppStyle::GetBrush("Icons.X"))
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

	// 4. Compute ground plane from reference cells (textures already at max cell size).
	TArray<int32> RefBottomPads;
	for (const TPair<UTexture2D*, FIntPoint>& Entry : CellSizeMap)
	{
		if (Entry.Value.X >= MaxCell.X && Entry.Value.Y >= MaxCell.Y)
		{
			UTexture2D* RefTex = Entry.Key;
			FIntPoint RefGrid(1, 1);
			for (const TSharedPtr<FBulkExtractorTextureState>& S : TextureStates)
			{
				if (S.IsValid() && S->Texture.LoadSynchronous() == RefTex)
				{
					RefGrid = S->GetEffectiveGrid();
					break;
				}
			}
			TArray<FColor> RefPixels;
			int32 RefW = 0, RefH = 0;
			if (FSpriteExtractionUtils::LoadTextureData(RefTex, RefPixels, RefW, RefH) && RefGrid.X > 0 && RefGrid.Y > 0)
			{
				const int32 RefCellW = RefW / RefGrid.X;
				const int32 RefCellH = RefH / RefGrid.Y;
				for (int32 Row = 0; Row < RefGrid.Y; Row++)
				{
					for (int32 Col = 0; Col < RefGrid.X; Col++)
					{
						const int32 CX0 = Col * RefCellW;
						const int32 CY0 = Row * RefCellH;
						const FIntRect CellRect(CX0, CY0, CX0 + RefCellW, CY0 + RefCellH);
						const FIntRect Tight = FSpriteExtractionUtils::FindTightContentBounds(
							RefPixels, RefW, RefH, CellRect);
						if (Tight.Width() > 0 && Tight.Height() > 0)
						{
							RefBottomPads.Add(RefCellH - (Tight.Max.Y - CY0));
						}
					}
				}
			}
		}
	}
	int32 GroundPlane = 0;
	if (RefBottomPads.Num() > 0)
	{
		RefBottomPads.Sort();
		GroundPlane = RefBottomPads[RefBottomPads.Num() / 2];
	}
	LastComputedGroundPlaneOffset = GroundPlane;

	// 5. Run the three-phase pad pipeline. Failures rollback all snapshots and abort.
	const bool bOk = RunThreePhasePadPipeline(ToPadList, MaxCell, GroundPlane);
	if (!bOk)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("PadFailed", "Auto-pad failed and was rolled back. No texture changes were committed."));
	}
	if (TextureListView.IsValid()) TextureListView->RequestListRefresh();
	if (CenterCanvas.IsValid() && SelectedTexture.IsValid())
	{
		CenterCanvas->SetDetectedSprites(SelectedTexture->DetectedSprites);
		CenterCanvas->Invalidate(EInvalidateWidgetReason::Paint);
	}
	return FReply::Handled();
}

bool SBulkSpriteExtractorWindow::RunThreePhasePadPipeline(
	const TArray<UTexture2D*>& ToPadList,
	FIntPoint MaxCellDims,
	int32 GroundPlaneOffset)
{
	// Snapshot manifests from any prior invocation are cleared — only one active run per window.
	CurrentPadManifests.Empty();
	PerTexturePasteOffsets.Empty();
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
		if (SlowTask.ShouldCancel()) return false;

		TArray<FColor> DummyPixels;
		int32 DummyW = 0, DummyH = 0;
		if (!FSpriteExtractionUtils::LoadTextureData(Tex, DummyPixels, DummyW, DummyH))
		{
			UE_LOG(LogTemp, Error, TEXT("BulkExtractor: preflight failed on %s — Source data unavailable."), *Tex->GetName());
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

		const FIntPoint PasteOffset = FSpriteExtractionUtils::PadTextureInPlace(
			Tex, MaxCellDims, TexGrid, GroundPlaneOffset);

		const FIntPoint PostPadDims = FSpriteExtractionUtils::GetDetectionDimensions(Tex);
		UE_LOG(LogTemp, Log, TEXT("BulkExtractor Pad: %s — PostPadDims=%dx%d PasteOffset=%d,%d"),
			*Tex->GetName(), PostPadDims.X, PostPadDims.Y, PasteOffset.X, PasteOffset.Y);

		const FIntPoint ExpectedTexDims(TexGrid.X * MaxCellDims.X, TexGrid.Y * MaxCellDims.Y);
		if (PasteOffset == FIntPoint::ZeroValue && (PostPadDims.X != ExpectedTexDims.X || PostPadDims.Y != ExpectedTexDims.Y))
		{
			// Zero offset on a texture that should have grown signals failure.
			UE_LOG(LogTemp, Error, TEXT("BulkExtractor: pad failed on %s — rolling back."), *Tex->GetName());
			// Rollback in reverse order.
			for (int32 j = i - 1; j >= 0; j--)
			{
				FSpriteExtractionUtils::RestoreTextureSnapshot(CurrentPadManifests[j]);
			}
			// Leave manifest directory for manual recovery if user wants it; caller decides cleanup.
			return false;
		}
		PerTexturePasteOffsets.Add(Tex, PasteOffset);

		// Update per-texture state chip and re-run detection on the padded texture
		// so the canvas shows tight content bounds within the new cells.
		for (const TSharedPtr<FBulkExtractorTextureState>& S : TextureStates)
		{
			if (S.IsValid() && S->Texture.LoadSynchronous() == Tex)
			{
				S->Status = EBulkExtractorTextureStatus::Padded;
				// Restore per-texture detection params before re-running so the result
				// matches what the user confirmed during manual review.
				DetectionParams = S->ConfirmedDetectionParams;
				RunDetectionAndInference(S);
				break;
			}
		}
	}

	// Textures that didn't need padding advance to SkippedNoPad for visibility.
	for (const TSharedPtr<FBulkExtractorTextureState>& S : TextureStates)
	{
		if (S.IsValid() && S->Status == EBulkExtractorTextureStatus::Confirmed)
		{
			S->Status = EBulkExtractorTextureStatus::SkippedNoPad;
		}
	}

	return true;
}

FReply SBulkSpriteExtractorWindow::OnCommitClicked()
{
	UPaper2DPlusCharacterProfileAsset* Profile = TargetProfile.LoadSynchronous();

	if (!bLinkToProfile)
	{
		Profile = nullptr;
	}

	const bool bOk = CommitBulkExtract();
	if (!bOk)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("CommitFailed", "Commit failed. See Output Log for details."));
		return FReply::Handled();
	}

	if (Profile)
	{
		Profile->LastAlignmentCheckTimestamp = FDateTime::UtcNow();
		Profile->AlignmentStatus = EAlignmentStatus::Completed;
		Profile->MarkPackageDirty();
	}

	// Unit 7: offer to delete source textures after successful trim extraction.
	// Only in trim mode — full-cell sprites reference the original texture directly.
	if (bTrimSprites && TextureStates.Num() > 0)
	{
		TArray<UObject*> TexturesToDelete;
		FString TextureListStr;
		for (const TSharedPtr<FBulkExtractorTextureState>& S : TextureStates)
		{
			if (!S.IsValid()) continue;
			UTexture2D* Tex = S->Texture.LoadSynchronous();
			if (!Tex) continue;
			TexturesToDelete.Add(Tex);
			TextureListStr += FString::Printf(TEXT("  - %s\n"), *Tex->GetName());
		}

		if (TexturesToDelete.Num() > 0)
		{
			const FText ConfirmMsg = FText::Format(
				LOCTEXT("DeleteSourceTexturesPrompt",
					"Delete {0} source texture(s)? This cannot be undone.\n\n{1}\nTrimmed sprites use packed textures and no longer reference these originals."),
				TexturesToDelete.Num(), FText::FromString(TextureListStr));

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 4
			const EAppReturnType::Type Choice = FMessageDialog::Open(EAppMsgType::YesNo, ConfirmMsg);
#else
			const EAppReturnType::Type Choice = FMessageDialog::Open(EAppMsgType::YesNo, ConfirmMsg,
				LOCTEXT("DeleteSourceTexturesTitle", "Delete Source Textures"));
#endif
			if (Choice == EAppReturnType::Yes)
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

bool SBulkSpriteExtractorWindow::CommitBulkExtract()
{
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
	if (BaseOutputPath.IsEmpty()) return false;

	const FString Prefix = NamePrefix;
	const FIntPoint MaxCell = FSpriteExtractionUtils::ComputeGroupMaxCellSize(BuildPerTextureCellSizeMap());

	int32 TotalSpritesCreated = 0;
	int32 TotalFlipbooksCreated = 0;
	TMap<int32, FString> FlipbookIndexToFolderPath;

	// --- Main loop: create textures, sprites, flipbooks, profile entries ---
	for (const TSharedPtr<FBulkExtractorTextureState>& State : TextureStates)
	{
		if (!State.IsValid()) continue;
		UTexture2D* Tex = State->Texture.LoadSynchronous();
		if (!Tex) continue;

		const FString TexBase = State->DisplayName.IsEmpty() ? Tex->GetName() : State->DisplayName;
		const FString FlipbookName = Prefix.IsEmpty() ? TexBase : FString::Printf(TEXT("%s_%s"), *Prefix, *TexBase);

		FString OutputPath;
		if (!State->SubfolderPath.IsEmpty())
		{
			OutputPath = BaseOutputPath / State->SubfolderPath;
		}
		else if (bCreateSubfolder)
		{
			FString FolderBase = TexBase;
			if (!FolderRemoveStr.IsEmpty()) FolderBase = FolderBase.Replace(*FolderRemoveStr, TEXT(""));
			if (!FolderFindStr.IsEmpty()) FolderBase = FolderBase.Replace(*FolderFindStr, *FolderReplaceStr);
			FString FolderName;
			if (!FolderCustomPrefix.IsEmpty()) FolderName += FolderCustomPrefix;
			if (bFolderIncludePrefix && !Prefix.IsEmpty()) FolderName += Prefix + TEXT("_");
			FolderName += FolderBase;
			if (bFolderIncludeBatchSuffix && !LastBatchSuffix.IsEmpty()) FolderName += LastBatchSuffix;
			if (!FolderCustomSuffix.IsEmpty()) FolderName += FolderCustomSuffix;
			OutputPath = BaseOutputPath / FolderName;
		}
		else
		{
			OutputPath = BaseOutputPath;
		}

		const FIntPoint Grid = State->GetEffectiveGrid();
		const FIntPoint TexDims = FSpriteExtractionUtils::GetDetectionDimensions(Tex);
		const int32 NumSprites = Grid.X * Grid.Y;
		const int32 CellW = (Grid.X > 0) ? (TexDims.X / Grid.X) : TexDims.X;
		const int32 CellH = (Grid.Y > 0) ? (TexDims.Y / Grid.Y) : TexDims.Y;

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

		TArray<FIntRect> TightRegions;
		int32 TrimMaxExtLeft = 0, TrimMaxExtRight = 0, TrimMaxExtTop = 0, TrimMaxExtBottom = 0;

		if (bTrimSprites)
		{
			// --- Per-texture midline + max extents (same algorithm as ComputeUniformBounds) ---
			const int32 MidX = CellW / 2;
			const int32 MidY = CellH / 2;

			TArray<FColor> Pixels;
			int32 PxW = 0, PxH = 0;
			if (!FSpriteExtractionUtils::LoadTextureData(Tex, Pixels, PxW, PxH)) continue;

			TightRegions.Reserve(NumSprites);
			for (int32 i = 0; i < NumSprites; i++)
			{
				FIntRect Tight = FSpriteExtractionUtils::FindTightContentBounds(
					Pixels, PxW, PxH, CellBoundsArray[i], State->ConfirmedDetectionParams.AlphaThreshold);
				if (Tight.Width() <= 0 || Tight.Height() <= 0)
				{
					Tight = CellBoundsArray[i];
				}
				TightRegions.Add(Tight);

				const int32 CellMidX = CellBoundsArray[i].Min.X + MidX;
				const int32 CellMidY = CellBoundsArray[i].Min.Y + MidY;
				TrimMaxExtLeft   = FMath::Max(TrimMaxExtLeft,   CellMidX - Tight.Min.X);
				TrimMaxExtRight  = FMath::Max(TrimMaxExtRight,  Tight.Max.X - CellMidX);
				TrimMaxExtTop    = FMath::Max(TrimMaxExtTop,    CellMidY - Tight.Min.Y);
				TrimMaxExtBottom = FMath::Max(TrimMaxExtBottom,  Tight.Max.Y - CellMidY);
			}

			const int32 UniformW = TrimMaxExtLeft + TrimMaxExtRight;
			const int32 UniformH = TrimMaxExtTop + TrimMaxExtBottom;

			// Build uniform regions: midline ± max extents for each cell
			TArray<FIntRect> UniformRegions;
			UniformRegions.Reserve(NumSprites);
			for (int32 i = 0; i < NumSprites; i++)
			{
				const int32 CellMidX = CellBoundsArray[i].Min.X + MidX;
				const int32 CellMidY = CellBoundsArray[i].Min.Y + MidY;
				UniformRegions.Add(FIntRect(
					FMath::Max(0, CellMidX - TrimMaxExtLeft),
					FMath::Max(0, CellMidY - TrimMaxExtTop),
					FMath::Min(PxW, CellMidX + TrimMaxExtRight),
					FMath::Min(PxH, CellMidY + TrimMaxExtBottom)));
			}

			const FString TrimmedTexName = Prefix.IsEmpty()
				? FString::Printf(TEXT("%s_Tex"), *TexBase)
				: FString::Printf(TEXT("%s_%s_Tex"), *Prefix, *TexBase);
			UTexture2D* TrimmedTex = FSpriteExtractionUtils::CreatePackedTexture(
				Tex, UniformRegions, TrimmedTexName, OutputPath);
			if (!TrimmedTex) continue;

			FSpriteExtractionUtils::ApplyPaper2DSettings(TrimmedTex);

			UE_LOG(LogTemp, Log, TEXT("BulkExtract Trim[%s]: %d sprites → %dx%d (midline extents L=%d R=%d T=%d B=%d), source cells %dx%d, pivot at (%d,%d)"),
				*TexBase, NumSprites, UniformW, UniformH,
				TrimMaxExtLeft, TrimMaxExtRight, TrimMaxExtTop, TrimMaxExtBottom,
				CellW, CellH, TrimMaxExtLeft, TrimMaxExtTop);

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
					// Set pivot at the grid midline within the packed sprite.
					// This is (MaxExtLeft, MaxExtTop) — the point that corresponds
					// to the padded cell center. All textures share this reference.
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

		TotalSpritesCreated += CreatedSprites.Num();
		if (CreatedSprites.Num() == 0) continue;

		// --- Build flipbook ---
		const FString FBPkgPath = OutputPath / FlipbookName;
		UPackage* FBPackage = CreatePackage(*FBPkgPath);
		if (!FBPackage) continue;
		UPaperFlipbook* Flipbook = NewObject<UPaperFlipbook>(FBPackage, *FlipbookName, RF_Public | RF_Standalone);
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
		NewEntry.SourceTexture = Tex;
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

			if (bTrimSprites && TightRegions.IsValidIndex(i))
			{
				Info.SourceOffset = TightRegions[i].Min;
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
		NewEntry.AlignmentData.GroundPlaneOffset = LastComputedGroundPlaneOffset;
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
		TotalFlipbooksCreated++;
	}

	// Auto-create flipbook groups from folder organizer structure
	if (bAutoCreateGroups && Profile && FlipbookIndexToFolderPath.Num() > 0)
	{
		TSet<FString> CreatedGroups;
		for (const auto& Pair : FlipbookIndexToFolderPath)
		{
			// Parse folder path segments (skip "Root" prefix if present)
			TArray<FString> Segments;
			Pair.Value.ParseIntoArray(Segments, TEXT("/"));
			if (Segments.Num() > 0 && Segments[0] == TEXT("Root")) Segments.RemoveAt(0);

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

			if (Segments.Num() > 0)
			{
				FName LeafGroup(*Segments.Last());
				Profile->MoveFlipbookToFlipbookGroup(Pair.Key, LeafGroup);
			}
		}
	}

	if (bTrimSprites)
	{
		// Trim path: restore original textures from pad snapshots (padded state was transient).
		for (const FPadSnapshotManifest& Manifest : CurrentPadManifests)
		{
			FSpriteExtractionUtils::RestoreTextureSnapshot(Manifest);
		}
	}
	else if (Profile)
	{
		// Full-cell path: apply cross-sheet alignment (expands sprite SourceUV to uniform cells).
		TArray<UPaper2DPlusCharacterProfileAsset*> ProfileList = { Profile };
		FSpriteExtractionUtils::ApplyCrossSheetAlignment(ProfileList, MaxCell, PerTexturePasteOffsets);
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

	return TotalFlipbooksCreated > 0;
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
	case EBulkExtractorTextureStatus::SkippedNoPad: return FLinearColor(0.55f, 1.0f, 0.55f);
	case EBulkExtractorTextureStatus::Padded:       return FLinearColor(0.4f, 0.9f, 0.9f);
	case EBulkExtractorTextureStatus::Error:        return FLinearColor(1.0f, 0.5f, 0.5f);
	}
	return FLinearColor::Gray;
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

	// Shared rename state
	TSharedRef<FString> FindStr = MakeShared<FString>();
	TSharedRef<FString> ReplaceStr = MakeShared<FString>();
	TSharedRef<FString> RemoveStr = MakeShared<FString>();
	TSharedRef<FString> SuffixStr = MakeShared<FString>();

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

	TSharedPtr<SVerticalBox> NameListBox;

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
							if (NameListBox.IsValid()) NameListBox->Invalidate(EInvalidateWidgetReason::Paint);
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
						.HintText(LOCTEXT("FindHint", "Find..."))
						.OnTextChanged_Lambda([FindStr, ApplyPreview, NameListBox](const FText& T)
						{
							*FindStr = T.ToString();
							ApplyPreview();
							if (NameListBox.IsValid()) NameListBox->Invalidate(EInvalidateWidgetReason::Paint);
						})
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SEditableTextBox)
						.HintText(LOCTEXT("ReplaceHint", "Replace..."))
						.OnTextChanged_Lambda([ReplaceStr, ApplyPreview, NameListBox](const FText& T)
						{
							*ReplaceStr = T.ToString();
							ApplyPreview();
							if (NameListBox.IsValid()) NameListBox->Invalidate(EInvalidateWidgetReason::Paint);
						})
					]
				]

				// Remove
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 2)
				[ SNew(STextBlock).Text(LOCTEXT("RemoveLabel", "Remove Text")) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SEditableTextBox)
					.HintText(LOCTEXT("RemoveHint", "Text to remove..."))
					.OnTextChanged_Lambda([RemoveStr, ApplyPreview, NameListBox](const FText& T)
					{
						*RemoveStr = T.ToString();
						ApplyPreview();
						if (NameListBox.IsValid()) NameListBox->Invalidate(EInvalidateWidgetReason::Paint);
					})
				]

				// Add Suffix
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 2)
				[ SNew(STextBlock).Text(LOCTEXT("AddSuffixLabel", "Add Suffix")) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SEditableTextBox)
					.HintText(LOCTEXT("SuffixHint", "Suffix..."))
					.OnTextChanged_Lambda([SuffixStr, ApplyPreview, NameListBox](const FText& T)
					{
						*SuffixStr = T.ToString();
						ApplyPreview();
						if (NameListBox.IsValid()) NameListBox->Invalidate(EInvalidateWidgetReason::Paint);
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
						if (NameListBox.IsValid()) NameListBox->Invalidate(EInvalidateWidgetReason::Paint);
					})
					[
						SNew(STextBlock).Text(LOCTEXT("IncludePrefixProfile", "Include prefix in profile name"))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
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
						.OnClicked_Lambda([this, PreviewNames, RenameWindow, SuffixStr]() -> FReply
						{
							for (int32 i = 0; i < TextureStates.Num() && i < PreviewNames->Num(); i++)
							{
								FString SanitizedName = (*PreviewNames)[i];
								FSpriteExtractionUtils::SanitizeAssetName(SanitizedName);
								TextureStates[i]->DisplayName = SanitizedName;
							}
							LastBatchSuffix = *SuffixStr;
							if (TextureListView.IsValid()) TextureListView->RequestListRefresh();
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
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8, 0)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(NameListBox, SVerticalBox)
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
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6, 3))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TextureStates[i]->DisplayName))
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

// ---- Folder Organizer ----

struct FBulkFolderNode
{
	FString Name;
	int32 TextureIndex = INDEX_NONE;
	TArray<TSharedPtr<FBulkFolderNode>> Children;
	TWeakPtr<FBulkFolderNode> Parent;

	bool IsFolder() const { return TextureIndex == INDEX_NONE; }

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

	TSharedPtr<FBulkFolderNode> DraggedNode;
	bool bFromUnassigned = false;

	static TSharedRef<FFolderNodeDragDropOp> New(TSharedPtr<FBulkFolderNode> InNode, bool bUnassigned)
	{
		TSharedRef<FFolderNodeDragDropOp> Op = MakeShared<FFolderNodeDragDropOp>();
		Op->DraggedNode = InNode;
		Op->bFromUnassigned = bUnassigned;
		Op->Construct();
		return Op;
	}
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

	// Unassigned list — textures not yet placed in folders.
	// If FolderAssignments is non-empty (from a previous Apply), reconstruct the tree from it.
	TSharedRef<TArray<TSharedPtr<FBulkFolderNode>>> Unassigned = MakeShared<TArray<TSharedPtr<FBulkFolderNode>>>();
	if (FolderAssignments.Num() > 0)
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

			const FString* AssignedPath = FolderAssignments.Find(i);
			if (AssignedPath && !AssignedPath->IsEmpty())
			{
				// Split off the leaf (texture name) from the path — the path includes the texture name.
				// e.g. "Attacks/Slash/TextureName" -> folder is "Attacks/Slash"
				FString FolderPath;
				FString LeafName;
				if (AssignedPath->Split(TEXT("/"), &FolderPath, &LeafName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
				{
					TSharedPtr<FBulkFolderNode> Folder = FindOrCreateFolder(FolderPath);
					Node->Parent = Folder;
					Folder->Children.Add(Node);
				}
				else
				{
					// Single-level path: the path IS the texture name sitting directly under root,
					// meaning it was directly under a top-level folder that is the path itself.
					// Actually, GetPath() returns the full path from root including the node's own name.
					// If we stored "FolderName/TextureName", split works. If stored just "TextureName"
					// (leaf at root level), then there's no parent folder — put in Unassigned.
					Unassigned->Add(Node);
				}
			}
			else
			{
				Unassigned->Add(Node);
			}
		}
	}
	else
	{
		for (int32 i = 0; i < TextureStates.Num(); i++)
		{
			TSharedPtr<FBulkFolderNode> Node = MakeShared<FBulkFolderNode>();
			Node->Name = TextureStates[i]->DisplayName;
			Node->TextureIndex = i;
			Unassigned->Add(Node);
		}
	}

	// Tree items for STreeView
	TSharedRef<TArray<TSharedPtr<FBulkFolderNode>>> TreeItems = MakeShared<TArray<TSharedPtr<FBulkFolderNode>>>();

	TSharedPtr<SListView<TSharedPtr<FBulkFolderNode>>> UnassignedList;
	TSharedPtr<STreeView<TSharedPtr<FBulkFolderNode>>> FolderTree;
	TSharedRef<TSharedPtr<FBulkFolderNode>> SelectedFolder = MakeShared<TSharedPtr<FBulkFolderNode>>();

	// Collapse state: folder paths currently collapsed. Default is expanded.
	TSharedRef<TSet<FString>> CollapsedFolders = MakeShared<TSet<FString>>();

	// Weak ref to the name widget of the most recently created folder (for auto-focus).
	TSharedRef<TWeakPtr<SEditableTextBox>> NewFolderNameWidget = MakeShared<TWeakPtr<SEditableTextBox>>();

	auto RefreshTree = [TreeItems, Root, &FolderTree, CollapsedFolders]()
	{
		*TreeItems = Root->Children;
		if (FolderTree.IsValid())
		{
			FolderTree->RequestTreeRefresh();
			// Restore collapse state: expand all except those in CollapsedFolders
			TFunction<void(TSharedPtr<FBulkFolderNode>)> RestoreCollapseState = [&](TSharedPtr<FBulkFolderNode> N)
			{
				if (N.IsValid() && N->Children.Num() > 0)
				{
					const bool bCollapsed = CollapsedFolders->Contains(N->GetPath());
					FolderTree->SetItemExpansion(N, !bCollapsed);
					for (auto& C : N->Children) RestoreCollapseState(C);
				}
			};
			for (auto& C : Root->Children) RestoreCollapseState(C);
		}
	};

	auto RefreshAll = [&UnassignedList, RefreshTree]()
	{
		RefreshTree();
		if (UnassignedList.IsValid()) UnassignedList->RequestListRefresh();
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
								.Text_Lambda([this]() -> FText
								{
									FString Path = OutputPathOverride;
									if (Path.IsEmpty())
									{
										UPaper2DPlusCharacterProfileAsset* P = TargetProfile.LoadSynchronous();
										if (P) Path = FPackageName::GetLongPackagePath(P->GetPackage()->GetName()) / TEXT("Sprites");
										else if (TextureStates.Num() > 0 && TextureStates[0].IsValid())
										{
											UTexture2D* T = TextureStates[0]->Texture.LoadSynchronous();
											if (T) Path = FPackageName::GetLongPackagePath(T->GetPackage()->GetName());
										}
									}
									if (Path.IsEmpty()) Path = TEXT("/Game/Sprites");
									return FText::FromString(Path + TEXT("/"));
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
									SNew(SImage).Image(FAppStyle::GetBrush("Icons.FolderOpen"))
								]
							]
						]
						// Create subfolders
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[
							SNew(SCheckBox)
							.IsChecked_Lambda([this]() { return bCreateSubfolder ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
							.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bCreateSubfolder = (S == ECheckBoxState::Checked); })
							.ToolTipText(LOCTEXT("OrgSubfolderTip", "Create a subfolder per texture under the output path."))
							[
								SNew(STextBlock).Text(LOCTEXT("OrgSubfolderLabel", "Create subfolders"))
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							]
						]
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
							.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
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

			// Center: unassigned textures
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
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
					.Padding(2)
					[
						SAssignNew(UnassignedList, SListView<TSharedPtr<FBulkFolderNode>>)
						.ListItemsSource(Unassigned.operator->())
						.SelectionMode(ESelectionMode::Multi)
						.OnGenerateRow_Lambda([](TSharedPtr<FBulkFolderNode> Item, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
						{
							return SNew(STableRow<TSharedPtr<FBulkFolderNode>>, Owner)
							.OnDragDetected_Lambda([Item](const FGeometry&, const FPointerEvent&) -> FReply
							{
								return FReply::Handled().BeginDragDrop(FFolderNodeDragDropOp::New(Item, true));
							})
							[
								SNew(SBorder)
								.BorderImage(FAppStyle::GetBrush("NoBorder"))
								.Padding(FMargin(4, 3))
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
									[
										SNew(SImage)
										.Image(FAppStyle::GetBrush("Icons.FilledCircle"))
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
						.Text(LOCTEXT("NewFolder", "+ Folder"))
						.OnClicked_Lambda([this, Root, SelectedFolder, RefreshAll, NewFolderNameWidget, CollapsedFolders]() -> FReply
						{
							TSharedPtr<FBulkFolderNode> NewFolder = MakeShared<FBulkFolderNode>();
							NewFolder->Name = TEXT("NewFolder");
							TSharedPtr<FBulkFolderNode> Target = SelectedFolder->IsValid() && (*SelectedFolder)->IsFolder() ? *SelectedFolder : nullptr;
							if (Target.IsValid())
							{
								NewFolder->Parent = Target;
								Target->Children.Add(NewFolder);
								// Ensure the parent is expanded so the new folder is visible.
								CollapsedFolders->Remove(Target->GetPath());
							}
							else
							{
								NewFolder->Parent = Root;
								Root->Children.Add(NewFolder);
							}
							RefreshAll();
							// Defer focus to the new folder's name widget by one frame
							// so the tree row has been generated.
							RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
								[NewFolderNameWidget](double, float) -> EActiveTimerReturnType
								{
									TSharedPtr<SEditableTextBox> NameBox = NewFolderNameWidget->Pin();
									if (NameBox.IsValid())
									{
										FSlateApplication::Get().SetKeyboardFocus(NameBox, EFocusCause::SetDirectly);
									}
									return EActiveTimerReturnType::Stop;
								}));
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.Text(LOCTEXT("DeleteFolder", "- Delete"))
						.OnClicked_Lambda([Root, SelectedFolder, Unassigned, RefreshAll, CollapsedFolders]() -> FReply
						{
							TSharedPtr<FBulkFolderNode> Sel = SelectedFolder->IsValid() ? *SelectedFolder : nullptr;
							if (!Sel.IsValid() || !Sel->IsFolder()) return FReply::Handled();
							// Remove deleted folder and all sub-folder paths from collapse tracking.
							TFunction<void(TSharedPtr<FBulkFolderNode>)> Flatten = [&](TSharedPtr<FBulkFolderNode> N)
							{
								CollapsedFolders->Remove(N->GetPath());
								for (auto& C : N->Children)
								{
									if (C->Children.Num() > 0) Flatten(C);
									if (!C->IsFolder()) { C->Parent.Reset(); C->Children.Empty(); Unassigned->Add(C); }
								}
							};
							Flatten(Sel);
							TSharedPtr<FBulkFolderNode> P = Sel->Parent.Pin();
							if (P.IsValid()) P->Remove(Sel);
							*SelectedFolder = nullptr;
							RefreshAll();
							return FReply::Handled();
						})
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4, 0, 8, 0)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
					.Padding(2)
					.OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Unhandled(); })
					[
						SAssignNew(FolderTree, STreeView<TSharedPtr<FBulkFolderNode>>)
						.TreeItemsSource(TreeItems.operator->())
						.SelectionMode(ESelectionMode::Single)
						.OnGetChildren_Lambda([](TSharedPtr<FBulkFolderNode> Item, TArray<TSharedPtr<FBulkFolderNode>>& OutChildren)
						{
							if (Item.IsValid()) OutChildren = Item->Children;
						})
						.OnSelectionChanged_Lambda([SelectedFolder](TSharedPtr<FBulkFolderNode> Item, ESelectInfo::Type)
						{
							*SelectedFolder = Item;
						})
						.OnExpansionChanged_Lambda([CollapsedFolders](TSharedPtr<FBulkFolderNode> Item, bool bExpanded)
						{
							if (Item.IsValid() && Item->IsFolder())
							{
								if (bExpanded)
								{
									CollapsedFolders->Remove(Item->GetPath());
								}
								else
								{
									CollapsedFolders->Add(Item->GetPath());
								}
							}
						})
						.OnGenerateRow_Lambda([Unassigned, Root, RefreshAll, NewFolderNameWidget, CollapsedFolders](TSharedPtr<FBulkFolderNode> Item, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
						{
							const bool bFolder = Item->IsFolder();

							// Build the name widget up front so we can stash it for new-folder auto-focus.
							TSharedPtr<SEditableTextBox> NameWidget;
							SAssignNew(NameWidget, SEditableTextBox)
								.Text(FText::FromString(Item->Name))
								.OnTextCommitted_Lambda([Item, CollapsedFolders](const FText& NewText, ETextCommit::Type)
								{
									if (Item.IsValid())
									{
										const FString OldPath = Item->GetPath();
										FString Sanitized = NewText.ToString();
										FSpriteExtractionUtils::SanitizeAssetName(Sanitized);
										Item->Name = Sanitized;
										if (CollapsedFolders->Contains(OldPath))
										{
											CollapsedFolders->Remove(OldPath);
											CollapsedFolders->Add(Item->GetPath());
										}
									}
								})
								.IsReadOnly(!bFolder)
								.Font(FCoreStyle::GetDefaultFontStyle(bFolder ? "Bold" : "Regular", 9))
								.BackgroundColor(FLinearColor::Transparent)
								.ForegroundColor(FSlateColor::UseForeground());

							// If this is a newly created folder, stash the widget for deferred focus.
							if (bFolder && Item->Name == TEXT("NewFolder"))
							{
								*NewFolderNameWidget = NameWidget;
							}

							return SNew(STableRow<TSharedPtr<FBulkFolderNode>>, Owner)
							.OnDragDetected_Lambda([Item](const FGeometry&, const FPointerEvent&) -> FReply
							{
								return FReply::Handled().BeginDragDrop(FFolderNodeDragDropOp::New(Item, false));
							})
							.OnCanAcceptDrop_Lambda([](const FDragDropEvent&, EItemDropZone, TSharedPtr<FBulkFolderNode>) -> TOptional<EItemDropZone>
							{
								return EItemDropZone::OntoItem;
							})
							.OnAcceptDrop_Lambda([Unassigned, Root, RefreshAll](const FDragDropEvent& Event, EItemDropZone, TSharedPtr<FBulkFolderNode> TargetItem) -> FReply
							{
								TSharedPtr<FFolderNodeDragDropOp> Op = Event.GetOperationAs<FFolderNodeDragDropOp>();
								if (!Op.IsValid() || !Op->DraggedNode.IsValid() || !TargetItem.IsValid()) return FReply::Unhandled();
								if (Op->DraggedNode == TargetItem) return FReply::Unhandled();

								TSharedPtr<FBulkFolderNode> Dragged = Op->DraggedNode;
								TSharedPtr<FBulkFolderNode> Target = TargetItem;
								if (!Target.IsValid()) Target = Root;

								// Prevent dropping into own descendant
								{
									TSharedPtr<FBulkFolderNode> Check = Target;
									while (Check.IsValid())
									{
										if (Check == Dragged) return FReply::Unhandled();
										Check = Check->Parent.Pin();
									}
								}

								// Remove from source
								if (Op->bFromUnassigned)
								{
									Unassigned->Remove(Dragged);
								}
								else
								{
									TSharedPtr<FBulkFolderNode> OldParent = Dragged->Parent.Pin();
									if (OldParent.IsValid()) OldParent->Remove(Dragged);
								}

								Dragged->Parent = Target;
								Target->Children.Add(Dragged);
								RefreshAll();
								return FReply::Handled();
							})
							[
								SNew(SBorder)
								.BorderImage(FAppStyle::GetBrush("NoBorder"))
								.Padding(FMargin(2, 2))
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
									[
										SNew(SImage)
										.Image(FAppStyle::GetBrush(bFolder ? "Icons.FolderClosed" : "Icons.FilledCircle"))
										.ColorAndOpacity(bFolder
											? FSlateColor(FLinearColor(0.9f, 0.7f, 0.2f))
											: FSlateColor(FLinearColor(0.4f, 0.6f, 0.9f)))
										.DesiredSizeOverride(bFolder ? FVector2D(14, 14) : FVector2D(8, 8))
									]
									+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
									[
										NameWidget.ToSharedRef()
									]
								]
							];
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
				.OnClicked_Lambda([this, Root, OrgWindow, bDlgFolderIncPrefix, bDlgFolderIncSuffix, DlgFolderPrefix, DlgFolderSuffix, DlgFolderFind, DlgFolderReplace, DlgFolderRemove, bDlgFolderIncBatchSuffix]() -> FReply
				{
					// Final guard: sanitize all folder names before committing paths.
					TFunction<void(TSharedPtr<FBulkFolderNode>)> SanitizeFolders = [&SanitizeFolders](TSharedPtr<FBulkFolderNode> Node)
					{
						if (Node->IsFolder())
						{
							FSpriteExtractionUtils::SanitizeAssetName(Node->Name);
						}
						for (auto& C : Node->Children) SanitizeFolders(C);
					};
					SanitizeFolders(Root);

					TFunction<void(TSharedPtr<FBulkFolderNode>)> Assign = [this, &Assign](TSharedPtr<FBulkFolderNode> Node)
					{
						for (auto& C : Node->Children)
						{
							if (!C->IsFolder() && TextureStates.IsValidIndex(C->TextureIndex))
							{
								TextureStates[C->TextureIndex]->SubfolderPath = C->GetPath();
								FolderAssignments.Add(C->TextureIndex, C->GetPath());
							}
							if (C->Children.Num() > 0)
							{
								Assign(C);
							}
						}
					};
					for (auto& S : TextureStates) S->SubfolderPath.Empty();
					FolderAssignments.Empty();
					Assign(Root);
					bCreateSubfolder = false;
					this->bFolderIncludePrefix = *bDlgFolderIncPrefix;
					this->bFolderIncludeSuffix = *bDlgFolderIncSuffix;
					this->FolderCustomPrefix = *DlgFolderPrefix;
					this->FolderCustomSuffix = *DlgFolderSuffix;
					this->FolderFindStr = *DlgFolderFind;
					this->FolderReplaceStr = *DlgFolderReplace;
					this->FolderRemoveStr = *DlgFolderRemove;
					this->bFolderIncludeBatchSuffix = *bDlgFolderIncBatchSuffix;
					OrgWindow->RequestDestroyWindow();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("CancelOrg", "Cancel"))
				.OnClicked_Lambda([OrgWindow]() -> FReply
				{
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

#undef LOCTEXT_NAMESPACE
