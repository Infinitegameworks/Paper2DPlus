// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "SpriteExtractorWindow.h"
#include "SpriteExtractionUtils.h"
#include "SpriteEditorOnlyTypes.h"
#include "EditorCanvasUtils.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/Texture2D.h"
#include "PaperSprite.h"
#include "PaperFlipbook.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "ScopedTransaction.h"
#include "DesktopPlatformModule.h"
#include "EditorDirectories.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "PropertyCustomizationHelpers.h"
// UE 5.0 compat: FAppStyle/AppStyle.h doesn't exist, use FEditorStyle
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif
#include "Misc/MessageDialog.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

/** SSpriteExtractorWindow — Single-texture sprite extraction with island/grid detection, edit mode, merge, repack, and flipbook creation. */

#define LOCTEXT_NAMESPACE "SpriteExtractor"

// ============================================
// Static Member Initialization
// ============================================

TArray<FSoftObjectPath> SSpriteExtractorWindow::RecentTextures;

// ============================================
// SSpriteExtractorWindow Implementation
// ============================================

void SSpriteExtractorWindow::Construct(const FArguments& InArgs)
{
	LoadRecentTextures();

	// Populate anchor options for the uniform dimensions combo box
	UniformAnchorOptions.Add(MakeShared<ESpriteAnchor>(ESpriteAnchor::BottomCenter));
	UniformAnchorOptions.Add(MakeShared<ESpriteAnchor>(ESpriteAnchor::BottomLeft));
	UniformAnchorOptions.Add(MakeShared<ESpriteAnchor>(ESpriteAnchor::BottomRight));
	UniformAnchorOptions.Add(MakeShared<ESpriteAnchor>(ESpriteAnchor::Center));
	UniformAnchorOptions.Add(MakeShared<ESpriteAnchor>(ESpriteAnchor::TopCenter));
	UniformAnchorOptions.Add(MakeShared<ESpriteAnchor>(ESpriteAnchor::TopLeft));
	UniformAnchorOptions.Add(MakeShared<ESpriteAnchor>(ESpriteAnchor::TopRight));
	UniformAnchorOptions.Add(MakeShared<ESpriteAnchor>(ESpriteAnchor::CenterLeft));
	UniformAnchorOptions.Add(MakeShared<ESpriteAnchor>(ESpriteAnchor::CenterRight));

	ChildSlot
	[
		SNew(SVerticalBox)

		// Top toolbar with primary actions
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildMainToolbar()
		]

		// Main content area with splitter
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			// Left panel - Settings
			+ SSplitter::Slot()
			.Value(0.25f)
			[
				SNew(SVerticalBox)

				// Texture settings warning banner (initially hidden)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(TextureSettingsBanner, SBorder)
					.BorderBackgroundColor(FLinearColor(0.8f, 0.6f, 0.0f, 1.0f))
					.Visibility(EVisibility::Collapsed)
					.Padding(FMargin(8, 6))
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NeedsPaper2DSettings",
								"This texture needs Paper2D settings (nearest filter, no compression, no mips)."))
							.AutoWrapText(true)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(4, 0)
						[
							SNew(SButton)
							.Text(LOCTEXT("ApplyNow", "Apply Now"))
							.OnClicked(this, &SSpriteExtractorWindow::OnApplyTextureSettingsClicked)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
							.Text(LOCTEXT("DismissBanner", "X"))
							.OnClicked_Lambda([this]()
							{
								TextureSettingsBanner->SetVisibility(EVisibility::Collapsed);
								return FReply::Handled();
							})
						]
					]
				]

				// Scrollable settings area
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4)
						[
							BuildTextureSection()
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4)
						[
							BuildRecentTexturesSection()
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4)
						[
							BuildDetectionSection()
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4)
						[
							BuildOutputSection()
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4)
						[
							BuildCharacterAssetSection()
						]
					]
				]
			]

			// Center - Canvas (wrapped in clipping box)
			+ SSplitter::Slot()
			.Value(0.55f)
			[
				SNew(SBox)
				.Clipping(EWidgetClipping::ClipToBounds)
				[
					SAssignNew(Canvas, SSpriteExtractorCanvas)
				]
			]

			// Right panel - Sprite list
			+ SSplitter::Slot()
			.Value(0.2f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BuildSpriteListHeader()
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(4)
				[
					SAssignNew(SpriteListScrollBox, SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(SpriteListBox, SVerticalBox)
					]
				]
			]
		]
	];

	// Bind canvas delegates
	if (Canvas.IsValid())
	{
		Canvas->OnZoomChanged.BindLambda([this]() { UpdateStatusTexts(); });
		Canvas->OnSpriteSelectionToggled.BindRaw(this, &SSpriteExtractorWindow::OnCanvasSpriteSelectionToggled);

		Canvas->OnNewBoxDrawn.BindLambda([this](const FIntRect& NewBounds)
		{
			PushUndoState();

			TArray<FDetectedSprite>& Sprites = Canvas->GetDetectedSprites();

			FDetectedSprite NewSprite;
			NewSprite.OriginalBounds = NewBounds;
			NewSprite.Bounds = NewBounds;
			NewSprite.bSelected = true;
			Sprites.Add(NewSprite);

			// Iterative absorb — new sprite is last, absorb may shift indices
			FIntRect FinalBounds = AbsorbContainedSprites(NewBounds, Sprites, Sprites.Num() - 1);
			Sprites.Last().Bounds = FinalBounds;
			Sprites.Last().OriginalBounds = FinalBounds;

			// Re-index
			for (int32 i = 0; i < Sprites.Num(); i++) Sprites[i].Index = i;

			RefreshSpriteList();
			UpdateStatusTexts();
			Canvas->EnterEditMode(Sprites.Num() - 1);
		});

		Canvas->OnSpriteEdited.BindLambda([this](int32 SpriteIndex, const FIntRect& NewBounds)
		{
			PushUndoState();
			if (!Canvas.IsValid() || !Canvas->GetDetectedSprites().IsValidIndex(SpriteIndex)) return;

			TArray<FDetectedSprite>& Sprites = Canvas->GetDetectedSprites();
			const FIntRect OldBounds = Sprites[SpriteIndex].OriginalBounds;

			// Check if the drag region might contain small islands that were
			// filtered out by MinSpriteSize. Temporarily detect at size 1 to
			// find the smallest island in the dragged region.
			if (SourceTexture && NewBounds.Width() > OldBounds.Width() + 2 || NewBounds.Height() > OldBounds.Height() + 2)
			{
				TArray<FColor> Pixels;
				int32 W = 0, H = 0;
				if (FSpriteExtractionUtils::LoadTextureData(SourceTexture, Pixels, W, H))
				{
					// Quick scan: find smallest opaque island in the NEW bounds
					// that's below current MinSpriteSize
					TArray<bool> Visited;
					Visited.SetNumZeroed(W * H);
					int32 SmallestIsland = MinSpriteSize;

					for (int32 Y = NewBounds.Min.Y; Y < FMath::Min(NewBounds.Max.Y, H); Y++)
					{
						for (int32 X = NewBounds.Min.X; X < FMath::Min(NewBounds.Max.X, W); X++)
						{
							if (!Visited[Y * W + X] && X < W && Y < H)
							{
								const FColor& Pixel = Pixels[Y * W + X];
								if (Pixel.A >= AlphaThreshold)
								{
									// Flood fill to find island bounds
									FIntRect IslandBounds(X, Y, X, Y);
									TArray<FIntPoint> Stack;
									Stack.Add(FIntPoint(X, Y));
									Visited[Y * W + X] = true;
									while (Stack.Num() > 0)
									{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
										FIntPoint P = Stack.Pop(EAllowShrinking::No);
#else
										FIntPoint P = Stack.Pop(false);
#endif
										IslandBounds.Min.X = FMath::Min(IslandBounds.Min.X, P.X);
										IslandBounds.Min.Y = FMath::Min(IslandBounds.Min.Y, P.Y);
										IslandBounds.Max.X = FMath::Max(IslandBounds.Max.X, P.X + 1);
										IslandBounds.Max.Y = FMath::Max(IslandBounds.Max.Y, P.Y + 1);
										const int32 Dirs[][2] = {{-1,0},{1,0},{0,-1},{0,1}};
										for (const auto& D : Dirs)
										{
											int32 NX = P.X + D[0], NY = P.Y + D[1];
											if (NX >= 0 && NX < W && NY >= 0 && NY < H && !Visited[NY * W + NX])
											{
												const FColor& NP = Pixels[NY * W + NX];
												if (NP.A >= AlphaThreshold)
												{
													Visited[NY * W + NX] = true;
													Stack.Add(FIntPoint(NX, NY));
												}
											}
										}
									}
									int32 IslandSize = FMath::Min(IslandBounds.Width(), IslandBounds.Height());
									if (IslandSize > 0 && IslandSize < SmallestIsland)
									{
										SmallestIsland = IslandSize;
									}
								}
								else
								{
									Visited[Y * W + X] = true;
								}
							}
						}
					}
					if (SmallestIsland < MinSpriteSize)
					{
						MinSpriteSize = FMath::Max(1, SmallestIsland);
					}
				}
			}

			// Re-detect with updated settings — the merge distance and min size
			// ensure auto-detect reproduces the user's intent.
			ScheduleAutoDetect();
		});

		}
}

// ============================================
// Keyboard Handler
// ============================================

FReply SSpriteExtractorWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Don't intercept shortcuts when a text input widget has focus
	TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if (FocusedWidget.IsValid())
	{
		FName WidgetType = FocusedWidget->GetType();
		if (WidgetType == TEXT("SEditableText") || WidgetType == TEXT("SMultiLineEditableText"))
		{
			return FReply::Unhandled();
		}
	}

	FKey Key = InKeyEvent.GetKey();

	// Ctrl+Shift+Z - Redo
	if (Key == EKeys::Z && InKeyEvent.IsControlDown() && InKeyEvent.IsShiftDown())
	{
		Redo();
		return FReply::Handled();
	}

	// Ctrl+Z - Undo
	if (Key == EKeys::Z && InKeyEvent.IsControlDown() && !InKeyEvent.IsShiftDown())
	{
		Undo();
		return FReply::Handled();
	}

	// Ctrl+Y - Redo
	if (Key == EKeys::Y && InKeyEvent.IsControlDown())
	{
		Redo();
		return FReply::Handled();
	}

	// Escape - Exit edit mode (cancel)
	if (Key == EKeys::Escape)
	{
		if (Canvas.IsValid() && Canvas->IsInEditMode())
		{
			Canvas->ExitEditMode(false);
			return FReply::Handled();
		}
	}

	// M - Merge selected sprites
	if (Key == EKeys::M)
	{
		if (Canvas.IsValid())
		{
			// Try merge selection (shift+click) first
			if (Canvas->GetMergeSelectedCount() >= 2)
			{
				TArray<int32> Indices = Canvas->GetMergeSelected().Array();
				MergeSelectedSprites(Indices);
				return FReply::Handled();
			}
			// Fall back to checkbox-selected sprites
			TArray<int32> SelectedIndices;
			TArray<FDetectedSprite>& Sprites = Canvas->GetDetectedSprites();
			for (int32 i = 0; i < Sprites.Num(); i++)
			{
				if (Sprites[i].bSelected) SelectedIndices.Add(i);
			}
			if (SelectedIndices.Num() >= 2)
			{
				MergeSelectedSprites(SelectedIndices);
				return FReply::Handled();
			}
		}
	}

	// Space - Run detection (but not Ctrl+Space, which opens the content browser)
	if (Key == EKeys::SpaceBar && !InKeyEvent.IsControlDown())
	{
		OnDetectSpritesClicked();
		return FReply::Handled();
	}

	// Enter - Extract selected sprites
	if (Key == EKeys::Enter)
	{
		OnExtractSpritesClicked();
		return FReply::Handled();
	}

	// A - Select all
	if (Key == EKeys::A && !InKeyEvent.IsControlDown())
	{
		OnSelectAllClicked();
		return FReply::Handled();
	}

	// D - Deselect all
	if (Key == EKeys::D)
	{
		OnDeselectAllClicked();
		return FReply::Handled();
	}

	// R - Reset view
	if (Key == EKeys::R)
	{
		if (Canvas.IsValid())
		{
			Canvas->ResetView();
			UpdateStatusTexts();
		}
		return FReply::Handled();
	}

	// + or = - Zoom in
	if (Key == EKeys::Add || Key == EKeys::Equals)
	{
		if (Canvas.IsValid())
		{
			Canvas->SetZoom(Canvas->GetZoom() + 0.25f);
			UpdateStatusTexts();
		}
		return FReply::Handled();
	}

	// - - Zoom out
	if (Key == EKeys::Subtract || Key == EKeys::Hyphen)
	{
		if (Canvas.IsValid())
		{
			Canvas->SetZoom(Canvas->GetZoom() - 0.25f);
			UpdateStatusTexts();
		}
		return FReply::Handled();
	}

	// 1-9 - Toggle selection of sprite by index
	int32 NumKeyIndex = -1;
	if (Key == EKeys::One) NumKeyIndex = 0;
	else if (Key == EKeys::Two) NumKeyIndex = 1;
	else if (Key == EKeys::Three) NumKeyIndex = 2;
	else if (Key == EKeys::Four) NumKeyIndex = 3;
	else if (Key == EKeys::Five) NumKeyIndex = 4;
	else if (Key == EKeys::Six) NumKeyIndex = 5;
	else if (Key == EKeys::Seven) NumKeyIndex = 6;
	else if (Key == EKeys::Eight) NumKeyIndex = 7;
	else if (Key == EKeys::Nine) NumKeyIndex = 8;

	if (NumKeyIndex >= 0)
	{
		if (Canvas.IsValid())
		{
			Canvas->ToggleSpriteSelection(NumKeyIndex);
			RefreshSpriteList();
			UpdateStatusTexts();
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

// ============================================
// Main Toolbar
// ============================================

TSharedRef<SWidget> SSpriteExtractorWindow::BuildMainToolbar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(4))
		[
			SNew(SWrapBox)
			.UseAllottedSize(true)

			// Primary actions group
			+ SWrapBox::Slot()
			.Padding(0, 0, 4, 2)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("SelectTexture", "Select Texture..."))
					.ToolTipText(LOCTEXT("SelectTextureTooltip", "Choose a texture to extract sprites from"))
					.OnClicked(this, &SSpriteExtractorWindow::OnSelectTextureClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
					.Text_Lambda([this]() { return bReExtractMode ? LOCTEXT("ReExtractSelected", "Re-extract") : LOCTEXT("ExtractSelected", "Extract Selected"); })
					.ToolTipText_Lambda([this]() { return bReExtractMode ? LOCTEXT("ReExtractTooltip", "Update existing sprites in-place with new extraction bounds") : LOCTEXT("ExtractSelectedTooltip", "Extract selected sprites and create assets (Enter)"); })
					.OnClicked(this, &SSpriteExtractorWindow::OnExtractSpritesClicked)
					.IsEnabled_Lambda([this]()
					{
						if (!Canvas.IsValid()) return false;
						return GetSelectedSpriteCount() > 0;
					})
				]
			]

			// Zoom controls group
			+ SWrapBox::Slot()
			.Padding(0, 0, 4, 2)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("ZoomOut", "-"))
					.ToolTipText(LOCTEXT("ZoomOutTooltip", "Zoom out (-)"))
					.OnClicked_Lambda([this]()
					{
						if (Canvas.IsValid())
						{
							Canvas->SetZoom(Canvas->GetZoom() - 0.25f);
							UpdateStatusTexts();
						}
						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 0)
				[
					SAssignNew(ZoomText, STextBlock)
					.Text(LOCTEXT("ZoomDefault", "100%"))
					.MinDesiredWidth(40)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("ZoomIn", "+"))
					.ToolTipText(LOCTEXT("ZoomInTooltip", "Zoom in (+)"))
					.OnClicked_Lambda([this]()
					{
						if (Canvas.IsValid())
						{
							Canvas->SetZoom(Canvas->GetZoom() + 0.25f);
							UpdateStatusTexts();
						}
						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("ResetView", "Reset"))
					.ToolTipText(LOCTEXT("ResetViewTooltip", "Reset view to default zoom and position (R)"))
					.OnClicked_Lambda([this]()
					{
						if (Canvas.IsValid())
						{
							Canvas->ResetView();
							UpdateStatusTexts();
						}
						return FReply::Handled();
					})
				]
			]

			// Selection count
			+ SWrapBox::Slot()
			.Padding(4, 0, 0, 2)
			[
				SAssignNew(SelectionCountText, STextBlock)
				.Text(LOCTEXT("NoSelection", "No sprites detected"))
			]
		];
}

TSharedRef<SWidget> SSpriteExtractorWindow::BuildTextureSection()
{
	return SNew(SExpandableArea)
		.AreaTitle(LOCTEXT("SourceTextureTitle", "SOURCE TEXTURE"))
		.InitiallyCollapsed(false)
		.BodyContent()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(4)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						if (!SourceTexture)
						{
							return LOCTEXT("NoTextureSelected", "No texture selected - use toolbar to select");
						}
						return FText::Format(
							LOCTEXT("TextureInfo", "{0}\n{1} x {2}"),
							FText::FromString(SourceTexture->GetName()),
							FText::AsNumber(SourceTexture->GetSizeX()),
							FText::AsNumber(SourceTexture->GetSizeY())
						);
					})
					.AutoWrapText(true)
				]
			]
		];
}

// ============================================
// Recent Textures
// ============================================

void SSpriteExtractorWindow::AddToRecentTextures(UTexture2D* Texture)
{
	if (!Texture) return;

	FSoftObjectPath TexturePath(Texture);

	// Remove if already in list (to move to front)
	RecentTextures.RemoveAll([&TexturePath](const FSoftObjectPath& Path)
	{
		return Path == TexturePath;
	});

	// Insert at front
	RecentTextures.Insert(TexturePath, 0);

	// Trim to max
	if (RecentTextures.Num() > MaxRecentTextures)
	{
		RecentTextures.SetNum(MaxRecentTextures);
	}

	SaveRecentTextures();
}

void SSpriteExtractorWindow::LoadRecentTextures()
{
	RecentTextures.Empty();
	FString ConfigSection = TEXT("Paper2DPlus.SpriteExtractor");
	int32 Count = 0;
	GConfig->GetInt(*ConfigSection, TEXT("RecentTextureCount"), Count, GEditorPerProjectIni);
	for (int32 i = 0; i < Count && i < MaxRecentTextures; i++)
	{
		FString Path;
		FString Key = FString::Printf(TEXT("RecentTexture_%d"), i);
		if (GConfig->GetString(*ConfigSection, *Key, Path, GEditorPerProjectIni))
		{
			RecentTextures.Add(FSoftObjectPath(Path));
		}
	}
}

void SSpriteExtractorWindow::SaveRecentTextures()
{
	FString ConfigSection = TEXT("Paper2DPlus.SpriteExtractor");
	GConfig->SetInt(*ConfigSection, TEXT("RecentTextureCount"), RecentTextures.Num(), GEditorPerProjectIni);
	for (int32 i = 0; i < RecentTextures.Num(); i++)
	{
		FString Key = FString::Printf(TEXT("RecentTexture_%d"), i);
		GConfig->SetString(*ConfigSection, *Key, *RecentTextures[i].ToString(), GEditorPerProjectIni);
	}
	GConfig->Flush(false, GEditorPerProjectIni);
}

TSharedRef<SWidget> SSpriteExtractorWindow::BuildRecentTexturesSection()
{
	// Show placeholder if no recent textures
	if (RecentTextures.Num() == 0)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RecentTextures", "Recent Textures"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8, 2)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoRecentTextures", "No recent textures"))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			];
	}

	TSharedRef<SVerticalBox> RecentBox = SNew(SVerticalBox);

	RecentBox->AddSlot()
	.AutoHeight()
	.Padding(4, 2)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("RecentTextures", "Recent Textures"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
	];

	for (int32 i = 0; i < RecentTextures.Num(); i++)
	{
		FSoftObjectPath Path = RecentTextures[i];
		FString DisplayName = FPaths::GetBaseFilename(Path.GetAssetName());

		RecentBox->AddSlot()
		.AutoHeight()
		.Padding(8, 1)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.OnClicked_Lambda([this, Path]() -> FReply
			{
				UTexture2D* Texture = Cast<UTexture2D>(Path.TryLoad());
				if (Texture)
				{
					SetInitialTexture(Texture);
					// Also update internal state
					SourceTexture = Texture;
					SourceTexturePath = Path.ToString();
					if (Canvas.IsValid())
					{
						Canvas->SetTexture(Texture);
						Canvas->ResetView();
					}
					RefreshCanvas();
					AddToRecentTextures(Texture);
				}
				return FReply::Handled();
			})
			.ToolTipText(FText::FromString(Path.ToString()))
			[
				SNew(STextBlock)
				.Text(FText::FromString(DisplayName))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]
		];
	}

	return RecentBox;
}

TSharedRef<SWidget> SSpriteExtractorWindow::BuildDetectionSection()
{
	return SNew(SExpandableArea)
		.AreaTitle(LOCTEXT("DetectionTitle", "DETECTION SETTINGS"))
		.InitiallyCollapsed(false)
		.BodyContent()
		[
			SNew(SVerticalBox)

			// Mode selection with radio buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ModeLabel", "Detection Mode"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 16, 0)
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "RadioButton")
					.ToolTipText(LOCTEXT("IslandModeTooltip", "Automatically detect sprites by finding connected regions of opaque pixels using flood fill. Best for sprite sheets with irregular spacing or varying sprite sizes."))
					.IsChecked_Lambda([this]() { return DetectionMode == ESpriteDetectionMode::Island ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { if (State == ECheckBoxState::Checked) { DetectionMode = ESpriteDetectionMode::Island; ScheduleAutoDetect(); } })
					[
						SNew(STextBlock).Text(LOCTEXT("IslandMode", "Island Detection"))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "RadioButton")
					.ToolTipText(LOCTEXT("GridModeTooltip", "Split the texture into a uniform grid of equal-sized cells. Best for sprite sheets with consistent spacing and fixed cell sizes."))
					.IsChecked_Lambda([this]() { return DetectionMode == ESpriteDetectionMode::Grid ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { if (State == ECheckBoxState::Checked) { DetectionMode = ESpriteDetectionMode::Grid; ScheduleAutoDetect(); } })
					[
						SNew(STextBlock).Text(LOCTEXT("GridMode", "Grid Split"))
					]
				]
			]

			// Common settings
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 8, 4, 2)
			[
				SNew(SHorizontalBox)
				.ToolTipText(LOCTEXT("AlphaThresholdTooltip", "Minimum alpha value (0-255) for a pixel to be considered opaque. Pixels with alpha below this value are treated as transparent. Lower values detect more semi-transparent pixels, higher values are more strict. Default: 1"))

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AlphaThreshold", "Alpha Threshold:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				[
					SNew(SNumericEntryBox<int32>)
					.Value_Lambda([this]() { return AlphaThreshold; })
					.OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { AlphaThreshold = FMath::Clamp(Value, 0, 255); ScheduleAutoDetect(); })
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.ToolTipText(LOCTEXT("MinSizeTooltip", "Minimum width and height in pixels for a detected region to be considered a sprite. Regions smaller than this are ignored. Useful for filtering out noise or small artifacts. Default: 4"))

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MinSize", "Min Sprite Size:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				[
					SNew(SNumericEntryBox<int32>)
					.Value_Lambda([this]() { return MinSpriteSize; })
					.OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { MinSpriteSize = FMath::Max(1, Value); ScheduleAutoDetect(); })
				]
			]

			// Island detection specific settings
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 8, 4, 2)
			[
				SNew(STextBlock)
				.Visibility_Lambda([this]() { return DetectionMode == ESpriteDetectionMode::Island ? EVisibility::Visible : EVisibility::Collapsed; })
				.Text(LOCTEXT("IslandOptions", "Island Options"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SCheckBox)
				.Visibility_Lambda([this]() { return DetectionMode == ESpriteDetectionMode::Island ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("8DirTooltip", "When enabled, flood fill checks 8 neighboring pixels (including diagonals). When disabled, only checks 4 neighbors (up/down/left/right). Enable this to properly detect sprites with diagonally-connected parts like angled swords or limbs."))
				.IsChecked_Lambda([this]() { return bUse8DirectionalFloodFill ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bUse8DirectionalFloodFill = (State == ECheckBoxState::Checked); ScheduleAutoDetect(); })
				[
					SNew(STextBlock).Text(LOCTEXT("8Dir", "8-directional (catches diagonals)"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return DetectionMode == ESpriteDetectionMode::Island ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("MergeDistTooltip", "Maximum pixel distance between islands to merge into a single sprite. Set to 0 to disable."))
				+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(LOCTEXT("MergeDist", "Merge Distance:"))
				]
				+ SHorizontalBox::Slot().FillWidth(0.5f)
				[
					SNew(SNumericEntryBox<int32>)
					.Value_Lambda([this]() { return IslandMergeDistance; })
					.OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { IslandMergeDistance = FMath::Max(0, Value); ScheduleAutoDetect(); })
				]
			]

			// Grid-specific settings
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 8, 4, 2)
			[
				SNew(STextBlock)
				.Visibility_Lambda([this]() { return DetectionMode == ESpriteDetectionMode::Grid ? EVisibility::Visible : EVisibility::Collapsed; })
				.Text(LOCTEXT("GridOptions", "Grid Options"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return DetectionMode == ESpriteDetectionMode::Grid ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("GridColumnsTooltip", "Number of columns to divide the texture into. The texture width will be split evenly into this many columns. Each cell becomes one sprite."))

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("GridColumns", "Columns:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				[
					SNew(SNumericEntryBox<int32>)
					.Value_Lambda([this]() { return GridColumns; })
					.OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { GridColumns = FMath::Max(1, Value); ScheduleAutoDetect(); })
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return DetectionMode == ESpriteDetectionMode::Grid ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("GridRowsTooltip", "Number of rows to divide the texture into. The texture height will be split evenly into this many rows. Total sprites = Columns x Rows."))

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("GridRows", "Rows:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				[
					SNew(SNumericEntryBox<int32>)
					.Value_Lambda([this]() { return GridRows; })
					.OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { GridRows = FMath::Max(1, Value); ScheduleAutoDetect(); })
				]
			]

		];
}

TSharedRef<SWidget> SSpriteExtractorWindow::BuildOutputSection()
{
	return SNew(SExpandableArea)
		.AreaTitle(LOCTEXT("OutputTitle", "OUTPUT SETTINGS"))
		.InitiallyCollapsed(false)
		.BodyContent()
		[
			SNew(SVerticalBox)

			// Naming section header
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NamingLabel", "Naming"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]

			// Prefix field
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.ToolTipText(LOCTEXT("PrefixTooltip", "Character/entity name prefix. Auto-detected from texture name at the selected split point."))

				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Prefix", "Prefix:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.7f)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this]() { return FText::FromString(NamePrefix); })
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { NamePrefix = Text.ToString(); UpdateOutputPath(); })
				]
			]

			// Split point dropdown (only shown if separators found)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return SeparatorPositions.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("SplitPointTooltip", "Choose where to split the texture name into prefix and base name"))

				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SplitAt", "Split at:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.7f)
				[
					SAssignNew(SplitComboBox, SComboBox<TSharedPtr<int32>>)
					.OptionsSource(&SplitOptions)
					.OnSelectionChanged_Lambda([this](TSharedPtr<int32> Selection, ESelectInfo::Type)
					{
						if (!Selection.IsValid()) return;
						SplitIndex = *Selection;
						if (SplitIndex == -1)
						{
							// "No split" — move everything to NameBase, clear prefix
							FString FullName;
							if (!NamePrefix.IsEmpty())
							{
								FullName = NamePrefix + NameSeparator + NameBase;
							}
							else
							{
								FullName = NameBase;
							}
							NamePrefix.Empty();
							NameBase = FullName;
							AnimationName = FullName;
							UpdateOutputPath();
						}
						else if (SourceTexture && SeparatorPositions.IsValidIndex(SplitIndex))
						{
							FString TexName = SourceTexture->GetName();
							if (TexName.EndsWith(TEXT("_Texture"))) { TexName.LeftChopInline(8); }
							int32 CharIdx = SeparatorPositions[SplitIndex];
							NamePrefix = TexName.Left(CharIdx);
							NameSeparator = TexName.Mid(CharIdx, 1);
							NameBase = TexName.Mid(CharIdx + 1);
							AnimationName = NameBase;
							UpdateOutputPath();
						}
					})
					.OnGenerateWidget_Lambda([this](TSharedPtr<int32> Item) -> TSharedRef<SWidget>
					{
						if (!Item.IsValid() || !SourceTexture) return SNew(STextBlock).Text(LOCTEXT("Invalid", "?"));
						int32 Idx = *Item;
						if (Idx == -1)
						{
							return SNew(STextBlock).Text(LOCTEXT("NoSplitOption", "No split (use full name)"));
						}
						FString TexName = SourceTexture->GetName();
						if (TexName.EndsWith(TEXT("_Texture"))) { TexName.LeftChopInline(8); }
						if (!SeparatorPositions.IsValidIndex(Idx)) return SNew(STextBlock).Text(LOCTEXT("Invalid", "?"));
						int32 CharIdx = SeparatorPositions[Idx];
						// Show only the prefix portion that will be split off
						return SNew(STextBlock).Text(FText::FromString(TexName.Left(CharIdx)));
					})
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							if (!SourceTexture || SplitIndex == -1 || !SeparatorPositions.IsValidIndex(SplitIndex))
								return LOCTEXT("NoSplit", "No split");
							FString TexName = SourceTexture->GetName();
							if (TexName.EndsWith(TEXT("_Texture"))) { TexName.LeftChopInline(8); }
							int32 CharIdx = SeparatorPositions[SplitIndex];
							return FText::FromString(TexName.Left(CharIdx));
						})
					]
				]
			]

			// Name field
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.ToolTipText(LOCTEXT("NameTooltip", "Animation/action name. Sprites get this + index suffix. Flipbook gets just this name (with prefix)."))

				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Name", "Name:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.7f)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this]() { return FText::FromString(NameBase); })
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { NameBase = Text.ToString(); UpdateOutputPath(); })
				]
			]

			// Name preview
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 4)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					if (bCreateSubfolder)
					{
						return FText::Format(LOCTEXT("NamePreviewWithFolder", "Sprites: {0}  |  Flipbook: {1}  |  Folder: {2}/"),
							FText::FromString(GetSpriteName(0)),
							FText::FromString(GetFlipbookName()),
							FText::FromString(GetOutputFolderName()));
					}
					return FText::Format(LOCTEXT("NamePreview", "Sprites: {0}  |  Flipbook: {1}"),
						FText::FromString(GetSpriteName(0)),
						FText::FromString(GetFlipbookName()));
				})
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.7f, 0.5f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]

			// Create subfolder checkbox
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 4, 4, 2)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("CreateSubfolderTooltip", "Create a subfolder named after the asset to organize extracted sprites."))
				.IsChecked_Lambda([this]() { return bCreateSubfolder ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bCreateSubfolder = (State == ECheckBoxState::Checked); })
				[
					SNew(STextBlock).Text(LOCTEXT("CreateSubfolder", "Create Subfolder"))
				]
			]

			// Output path
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2, 4, 2)
			[
				SNew(SHorizontalBox)
				.ToolTipText(LOCTEXT("OutputPathTooltip", "Content browser path where extracted sprites will be saved. Click the folder icon to browse."))

				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OutputPath", "Output Path:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.7f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						FString BasePath = OutputPath.IsEmpty() ? TEXT("/Game/Sprites") : OutputPath;
						if (bCreateSubfolder)
						{
							return FText::FromString(BasePath / GetOutputFolderName() + TEXT("/"));
						}
						return FText::FromString(BasePath + TEXT("/"));
					})
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(LOCTEXT("BrowseOutputPath", "Browse for output folder"))
					.OnClicked_Lambda([this]() -> FReply
					{
						TSharedRef<FString> SelectedPath = MakeShared<FString>(OutputPath.IsEmpty() ? TEXT("/Game/Sprites") : OutputPath);

						FPathPickerConfig Config;
						Config.DefaultPath = *SelectedPath;
						Config.bAllowContextMenu = true;
						Config.bAddDefaultPath = true;
						Config.OnPathSelected = FOnPathSelected::CreateLambda([SelectedPath](const FString& Path)
						{
							*SelectedPath = Path;
						});

						FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
						TSharedRef<SWidget> PathPicker = CBModule.Get().CreatePathPicker(Config);

						TSharedRef<SWindow> PickerWindow = SNew(SWindow)
							.Title(LOCTEXT("ChooseOutputFolder", "Choose Output Folder"))
							.ClientSize(FVector2D(400, 500))
							.SupportsMinimize(false)
							.SupportsMaximize(false);

						PickerWindow->SetContent(
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							[
								PathPicker
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(8, 4)
							.HAlign(HAlign_Right)
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
								.Text(LOCTEXT("SelectFolder", "Select"))
								.OnClicked_Lambda([this, SelectedPath, WeakPickerWindow = TWeakPtr<SWindow>(PickerWindow)]() -> FReply
								{
									OutputPath = *SelectedPath;
									if (TSharedPtr<SWindow> PinnedWindow = WeakPickerWindow.Pin())
									{
										PinnedWindow->RequestDestroyWindow();
									}
									return FReply::Handled();
								})
							]
						);

						TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(SharedThis(this));
					FSlateApplication::Get().AddModalWindow(PickerWindow, ParentWindow);

						return FReply::Handled();
					})
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.FolderOpen"))
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(24, 0, 4, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("InvalidPathWarning", "Path should start with /Game/"))
				.ColorAndOpacity(FLinearColor(1.0f, 0.3f, 0.3f))
				.Visibility_Lambda([this]()
				{
					FString Path = OutputPath.IsEmpty() ? TEXT("/Game/Sprites") : OutputPath;
					return Path.StartsWith(TEXT("/Game/")) ? EVisibility::Collapsed : EVisibility::Visible;
				})
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 8, 4, 2)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("CreateFlipbookTooltip", "Automatically create a PaperFlipbook asset from the extracted sprites."))
				.IsChecked_Lambda([this]() { return bCreateFlipbook ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bCreateFlipbook = (State == ECheckBoxState::Checked); })
				[
					SNew(STextBlock).Text(LOCTEXT("CreateFlipbook", "Create Flipbook"))
				]
			]

			// Repack texture checkbox
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 4, 4, 2)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("RepackTextureTooltip",
					"Create a tightly packed texture containing only the extracted\n"
					"sprite regions instead of referencing the original (often padded)\n"
					"source texture. Significantly reduces texture memory for sheets\n"
					"with excess padding between or around sprites."))
				.IsChecked_Lambda([this]() { return bRepackTexture ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bRepackTexture = (State == ECheckBoxState::Checked); })
				[
					SNew(STextBlock).Text(LOCTEXT("RepackTexture", "Repack Texture (trim padding)"))
				]
			]

			// Uniform dimensions checkbox
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 4, 4, 2)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("UniformDimensionsTooltip",
					"Extract every sprite at the same (max-width, max-height) dimensions.\n"
					"Each sprite's tight-fit bounds are expanded using the Anchor setting\n"
					"to decide where the original sprite sits inside the expanded rect,\n"
					"pulling ACTUAL texture pixels from the surrounding sheet.\n\n"
					"For character animations, use Bottom Center (default) so feet stay\n"
					"at the bottom and extra space goes above the head."))
				.IsChecked_Lambda([this]() { return bUniformDimensions ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					bUniformDimensions = (State == ECheckBoxState::Checked);
					ApplyUniformBoundsPreview();
					RefreshCanvas();
					RefreshSpriteList();
				})
				[
					SNew(STextBlock).Text(LOCTEXT("UniformDimensions", "Uniform Dimensions (max W x H)"))
				]
			]

			// Uniform anchor picker
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(24, 0, 4, 2)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return bUniformDimensions ? EVisibility::Visible : EVisibility::Collapsed; })

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("UniformAnchorLabel", "Anchor:"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SComboBox<TSharedPtr<ESpriteAnchor>>)
					.OptionsSource(&UniformAnchorOptions)
					.OnSelectionChanged_Lambda([this](TSharedPtr<ESpriteAnchor> Selection, ESelectInfo::Type)
					{
						if (Selection.IsValid())
						{
							UniformAnchor = *Selection;
						}
					})
					.OnGenerateWidget_Lambda([](TSharedPtr<ESpriteAnchor> Item) -> TSharedRef<SWidget>
					{
						auto GetAnchorName = [](ESpriteAnchor A) -> FText
						{
							const UEnum* Enum = StaticEnum<ESpriteAnchor>();
							return Enum ? Enum->GetDisplayNameTextByValue(static_cast<int64>(A)) : FText::GetEmpty();
						};
						return SNew(STextBlock)
							.Text(GetAnchorName(*Item))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
					})
					.InitiallySelectedItem(UniformAnchorOptions[0])
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							const UEnum* Enum = StaticEnum<ESpriteAnchor>();
							return Enum ? Enum->GetDisplayNameTextByValue(static_cast<int64>(UniformAnchor)) : FText::GetEmpty();
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8, 0, 0, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("UniformAnchorHint", "(Bottom Center for character animations)"))
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 7))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					.Visibility_Lambda([this]() { return UniformAnchor == ESpriteAnchor::BottomCenter ? EVisibility::Collapsed : EVisibility::Visible; })
				]
			]

			// Uniform dimensions preview
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(24, 0, 4, 2)
			[
				SNew(STextBlock)
				.Visibility_Lambda([this]() { return bUniformDimensions ? EVisibility::Visible : EVisibility::Collapsed; })
				.Text_Lambda([this]()
				{
					if (!Canvas.IsValid()) return FText::GetEmpty();
					TArray<FDetectedSprite> Selected;
					for (const FDetectedSprite& S : Canvas->GetDetectedSprites())
					{
						if (S.bSelected) Selected.Add(S);
					}
					if (Selected.Num() == 0)
					{
						return LOCTEXT("UniformDimsNoSelection", "(select sprites to preview uniform size)");
					}
					if (Selected.Num() < 2)
					{
						const FIntPoint Uniform = ComputeUniformSpriteSize(Selected);
						return FText::Format(
							LOCTEXT("UniformDimsPreview1", "Uniform size: {0} x {1}  (1 sprite)"),
							FText::AsNumber(Uniform.X), FText::AsNumber(Uniform.Y));
					}
					// Compute actual extraction bounds: cell stride + max extents
					const int32 TW = SourceTexture ? SourceTexture->GetSizeX() : 0;
					const int32 TH = SourceTexture ? SourceTexture->GetSizeY() : 0;
					if (TW <= 0 || TH <= 0)
					{
						return FText::GetEmpty();
					}
					// Row grouping (mirrors extraction logic)
					int32 AvgH = 0;
					for (const FDetectedSprite& S : Selected) { AvgH += S.OriginalBounds.Height(); }
					AvgH /= Selected.Num();
					const int32 RowTol = FMath::Max(AvgH / 2, 16);
					TArray<int32> SI;
					for (int32 i = 0; i < Selected.Num(); i++) { SI.Add(i); }
					SI.Sort([&Selected](int32 A, int32 B)
					{
						int32 CYA = (Selected[A].OriginalBounds.Min.Y + Selected[A].OriginalBounds.Max.Y) / 2;
						int32 CYB = (Selected[B].OriginalBounds.Min.Y + Selected[B].OriginalBounds.Max.Y) / 2;
						if (CYA != CYB) return CYA < CYB;
						return Selected[A].OriginalBounds.Min.X < Selected[B].OriginalBounds.Min.X;
					});
					TArray<TArray<int32>> PR;
					PR.AddDefaulted();
					PR.Last().Add(SI[0]);
					for (int32 i = 1; i < SI.Num(); i++)
					{
						int32 CY = (Selected[SI[i]].OriginalBounds.Min.Y + Selected[SI[i]].OriginalBounds.Max.Y) / 2;
						int32 RY = (Selected[PR.Last()[0]].OriginalBounds.Min.Y + Selected[PR.Last()[0]].OriginalBounds.Max.Y) / 2;
						if (FMath::Abs(CY - RY) > RowTol) { PR.AddDefaulted(); }
						PR.Last().Add(SI[i]);
					}
					for (TArray<int32>& R : PR)
					{
						R.Sort([&Selected](int32 A, int32 B)
						{ return Selected[A].OriginalBounds.Min.X < Selected[B].OriginalBounds.Min.X; });
					}
					int32 MPR = 0;
					for (const TArray<int32>& R : PR) { MPR = FMath::Max(MPR, R.Num()); }
					const int32 CSX = TW / MPR;
					const int32 CSY = TH / PR.Num();
					int32 EL = 0, ER = 0, ET = 0, EB = 0;
					for (int32 RI = 0; RI < PR.Num(); RI++)
					{
						for (int32 CI = 0; CI < PR[RI].Num(); CI++)
						{
							const FIntRect& B = Selected[PR[RI][CI]].OriginalBounds;
							int32 MX = CI * CSX + CSX / 2;
							int32 MY = RI * CSY + CSY / 2;
							EL = FMath::Max(EL, MX - B.Min.X);
							ER = FMath::Max(ER, B.Max.X - MX);
							ET = FMath::Max(ET, MY - B.Min.Y);
							EB = FMath::Max(EB, B.Max.Y - MY);
						}
					}
					const int32 BoundsW = EL + ER;
					const int32 BoundsH = ET + EB;
					return FText::Format(
						LOCTEXT("UniformDimsPreview", "Extraction bounds: {0} x {1}  ({2} sprites, cell {3} x {4})"),
						FText::AsNumber(BoundsW), FText::AsNumber(BoundsH),
						FText::AsNumber(Selected.Num()),
						FText::AsNumber(CSX), FText::AsNumber(CSY));
				})
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.7f, 0.5f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return bCreateFlipbook ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("FrameRateTooltip", "Playback speed in frames per second. 8-12 for walk, 12-24 for combat."))

				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FrameRate", "Frame Rate:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.7f)
				[
					SNew(SNumericEntryBox<float>)
					.Value_Lambda([this]() { return FlipbookFrameRate; })
					.OnValueCommitted_Lambda([this](float Value, ETextCommit::Type) { FlipbookFrameRate = FMath::Max(0.1f, Value); })
				]
			]

			// Flipbook duration preview
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(STextBlock)
				.Visibility_Lambda([this]() { return bCreateFlipbook ? EVisibility::Visible : EVisibility::Collapsed; })
				.Text_Lambda([this]()
				{
					int32 FrameCount = GetSelectedSpriteCount();
					if (FrameCount > 0 && FlipbookFrameRate > 0)
					{
						float Duration = FrameCount / FlipbookFrameRate;
						return FText::Format(
							LOCTEXT("FlipbookDuration", "Duration: {0} frames @ {1} FPS = {2}s"),
							FText::AsNumber(FrameCount),
							FText::AsNumber(FlipbookFrameRate),
							FText::AsNumber(Duration)
						);
					}
					return LOCTEXT("NoDuration", "No sprites selected");
				})
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]
		];
}

TSharedRef<SWidget> SSpriteExtractorWindow::BuildCharacterAssetSection()
{
	return SNew(SExpandableArea)
		.AreaTitle(LOCTEXT("CharacterAssetTitle", "CHARACTER PROFILE ASSET"))
		.InitiallyCollapsed(true)
		.BodyContent()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("AddToAssetTooltip", "When enabled, the created flipbook will be automatically added to a Paper2DPlus Character Profile Asset as a new animation. Requires a target asset and animation name."))
				.IsChecked_Lambda([this]() { return bAddToCharacterAsset ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bAddToCharacterAsset = (State == ECheckBoxState::Checked); })
				[
					SNew(STextBlock).Text(LOCTEXT("AddToAsset", "Add to Character Profile Asset"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return bAddToCharacterAsset ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("TargetAssetTooltip", "The Paper2DPlus Character Profile Asset to add the animation to. This asset stores all animations for a character, including flipbooks, hitboxes, and frame events."))

				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TargetAsset", "Target Asset:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.7f)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UPaper2DPlusCharacterProfileAsset::StaticClass())
					.ObjectPath_Lambda([this]() { return TargetCharacterAsset ? TargetCharacterAsset->GetPathName() : FString(); })
					.OnObjectChanged_Lambda([this](const FAssetData& AssetData) {
						TargetCharacterAsset = Cast<UPaper2DPlusCharacterProfileAsset>(AssetData.GetAsset());
					})
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4, 2)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return bAddToCharacterAsset ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText(LOCTEXT("AnimationNameTooltip", "Name for this animation in the Character Profile Asset. Use descriptive names like 'Idle', 'Walk', 'Attack1', 'Jump_Start'. This name is used to look up animations at runtime."))

				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AnimationName", "Animation Name:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.7f)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this]() { return FText::FromString(AnimationName); })
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { AnimationName = Text.ToString(); })
				]
			]
		];
}

TSharedRef<SWidget> SSpriteExtractorWindow::BuildSpriteListHeader()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(4)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DetectedSpritesTitle", "DETECTED SPRITES"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("All", "All"))
					.ToolTipText(LOCTEXT("SelectAllTooltip", "Select all sprites (A)"))
					.OnClicked(this, &SSpriteExtractorWindow::OnSelectAllClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("None", "None"))
					.ToolTipText(LOCTEXT("DeselectAllTooltip", "Deselect all sprites (D)"))
					.OnClicked(this, &SSpriteExtractorWindow::OnDeselectAllClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("Invert", "Invert"))
					.ToolTipText(LOCTEXT("InvertTooltip", "Invert selection"))
					.OnClicked(this, &SSpriteExtractorWindow::OnInvertSelectionClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8, 2, 2, 2)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.Text(LOCTEXT("Help", "?"))
					.ToolTipText(LOCTEXT("HelpTooltip", "Show extractor help"))
					.OnClicked_Lambda([this]() -> FReply
					{
						FText HelpText = LOCTEXT("ExtractorHelpText",
							"Paper2D+ Sprite Extractor\n"
							"===========================\n\n"
							"Detection runs automatically when a texture is loaded or settings change.\n"
							"Paper2D texture settings (nearest filter, no compression, no mips) are\n"
							"applied automatically — no manual step needed.\n\n"
							"CANVAS CONTROLS\n"
							"  Click — select/deselect sprite\n"
							"  Click + drag handle — resize sprite bounds\n"
							"  Ctrl + drag — draw a new sprite box\n"
							"  Middle mouse / Right drag — pan\n"
							"  Scroll wheel — zoom\n\n"
							"MERGE BY DRAGGING\n"
							"  Drag a sprite's resize handle until it overlaps another sprite.\n"
							"  On release, the Merge Distance setting is raised automatically\n"
							"  so auto-detect will merge them. Small islands inside the dragged\n"
							"  region also lower the Min Sprite Size setting to include them.\n"
							"  Settings auto-update so your merges survive re-detection.\n\n"
							"UNIFORM DIMENSIONS\n"
							"  Enabled by default. Derives cell size from the texture dimensions\n"
							"  and sprite count, then centers tight bounds on each cell midpoint.\n"
							"  Eliminates jitter in flipbook playback.\n\n"
							"REPACK TEXTURE\n"
							"  Enabled by default. Creates a tightly packed texture containing\n"
							"  only the extracted sprite regions. Reduces memory for sheets with\n"
							"  excess padding.\n\n"
							"COMBINE TEXTURES\n"
							"  Select multiple textures in Content Browser, right-click >\n"
							"  Paper2D+ Actions > Combine into Spritesheet. Creates a single\n"
							"  horizontal strip (sorted alphabetically, bottom-aligned) and\n"
							"  opens it in the extractor.\n\n"
							"KEYBOARD SHORTCUTS\n"
							"  Space — re-run detection\n"
							"  Enter — extract selected sprites\n"
							"  A — select all\n"
							"  D — deselect all\n"
							"  Ctrl+Z / Ctrl+Y — undo / redo\n"
							"  Delete — remove selected sprite boxes\n"
						);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 4
						FMessageDialog::Open(EAppMsgType::Ok, HelpText);
#else
						FMessageDialog::Open(EAppMsgType::Ok, HelpText,
							LOCTEXT("ExtractorHelpTitle", "Sprite Extractor Help"));
#endif
						return FReply::Handled();
					})
				]
			]
		];
}

// ============================================
// Helper Functions
// ============================================

void SSpriteExtractorWindow::CheckTextureSettings()
{
	// Auto-apply Paper2D pixel-art settings if needed — no manual button required.
	// Settings: TC_EditorIcon compression, TF_Nearest filter, no mipmaps,
	// Pixels2D LOD group, NeverStream, SRGB. These match Paper2D's own defaults
	// but also set Filter/MipGen/NeverStream which Paper2D leaves to project config.
	if (SourceTexture && FSpriteExtractionUtils::NeedsPaper2DSettings(SourceTexture))
	{
		FSpriteExtractionUtils::ApplyPaper2DSettings(SourceTexture);
		if (Canvas.IsValid())
		{
			Canvas->SetTexture(SourceTexture);
		}
	}

	// Hide the legacy banner if it still exists in the widget tree
	if (TextureSettingsBanner.IsValid())
	{
		TextureSettingsBanner->SetVisibility(EVisibility::Collapsed);
	}
}

FReply SSpriteExtractorWindow::OnApplyTextureSettingsClicked()
{
	if (SourceTexture)
	{
		FSpriteExtractionUtils::ApplyPaper2DSettings(SourceTexture);
		CheckTextureSettings();

		// Refresh canvas to show texture with new settings
		if (Canvas.IsValid())
		{
			Canvas->SetTexture(SourceTexture);
		}

		// Re-run detection since SetTexture clears sprites
		ScheduleAutoDetect();
	}
	return FReply::Handled();
}

void SSpriteExtractorWindow::UpdateStatusTexts()
{
	if (ZoomText.IsValid() && Canvas.IsValid())
	{
		int32 ZoomPercent = FMath::RoundToInt(Canvas->GetZoom() * 100.0f);
		ZoomText->SetText(FText::Format(LOCTEXT("ZoomPercent", "{0}%"), FText::AsNumber(ZoomPercent)));
	}

	if (SelectionCountText.IsValid() && Canvas.IsValid())
	{
		TArray<FDetectedSprite>& Sprites = Canvas->GetDetectedSprites();
		int32 TotalCount = Sprites.Num();
		int32 SelectedCount = GetSelectedSpriteCount();

		if (TotalCount == 0)
		{
			SelectionCountText->SetText(LOCTEXT("NoSpritesDetected", "No sprites detected"));
		}
		else
		{
			SelectionCountText->SetText(FText::Format(
				LOCTEXT("SelectionCount", "Selected: {0} of {1}"),
				FText::AsNumber(SelectedCount),
				FText::AsNumber(TotalCount)
			));
		}
	}
}

int32 SSpriteExtractorWindow::GetSelectedSpriteCount() const
{
	if (!Canvas.IsValid()) return 0;

	int32 Count = 0;
	TArray<FDetectedSprite>& Sprites = const_cast<SSpriteExtractorCanvas*>(Canvas.Get())->GetDetectedSprites();
	for (const FDetectedSprite& S : Sprites)
	{
		if (S.bSelected) Count++;
	}
	return Count;
}

FReply SSpriteExtractorWindow::OnInvertSelectionClicked()
{
	if (Canvas.IsValid())
	{
		TArray<FDetectedSprite>& Sprites = Canvas->GetDetectedSprites();
		for (FDetectedSprite& S : Sprites)
		{
			S.bSelected = !S.bSelected;
		}
		RefreshSpriteList();
		UpdateStatusTexts();
	}
	return FReply::Handled();
}

void SSpriteExtractorWindow::SetReExtractMode(UPaperFlipbook* Flipbook, int32 FlipbookIndex, UPaper2DPlusCharacterProfileAsset* ProfileAsset)
{
	bReExtractMode = true;
	ReExtractFlipbook = Flipbook;
	ReExtractFlipbookIndex = FlipbookIndex;
	ReExtractProfileAsset = ProfileAsset;
}

void SSpriteExtractorWindow::SetInitialTexture(UTexture2D* Texture)
{
	if (Texture)
	{
		SourceTexture = Texture;
		SourceTexturePath = Texture->GetPathName();

		if (Canvas.IsValid())
		{
			Canvas->SetTexture(SourceTexture);
		}

		DetectedSprites.Empty();
		RefreshSpriteList();

		// Auto-detect name parts from texture name
		AutoDetectNameParts(SourceTexture->GetName());
		UpdateOutputPath();

		CheckTextureSettings();

		// Run detection immediately with current settings
		ScheduleAutoDetect();
	}
}

FReply SSpriteExtractorWindow::OnSelectTextureClicked()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	FOpenAssetDialogConfig Config;
	Config.DialogTitleOverride = LOCTEXT("SelectTexture", "Select Texture");
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
	Config.AssetClassNames.Add(UTexture2D::StaticClass()->GetFName());
#else
	Config.AssetClassNames.Add(UTexture2D::StaticClass()->GetClassPathName());
#endif
	Config.bAllowMultipleSelection = false;

	TArray<FAssetData> SelectedAssets = ContentBrowserModule.Get().CreateModalOpenAssetDialog(Config);
	if (SelectedAssets.Num() > 0)
	{
		SourceTexture = Cast<UTexture2D>(SelectedAssets[0].GetAsset());
		if (SourceTexture)
		{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
			SourceTexturePath = SelectedAssets[0].ObjectPath.ToString();
#else
			SourceTexturePath = SelectedAssets[0].GetObjectPathString();
#endif
			Canvas->SetTexture(SourceTexture);
			DetectedSprites.Empty();
			RefreshSpriteList();

			// Auto-detect name parts from texture name
			AutoDetectNameParts(SourceTexture->GetName());
			UpdateOutputPath();

			CheckTextureSettings();

			// Run detection immediately with current settings
			ScheduleAutoDetect();

			// Add to recent textures list
			AddToRecentTextures(SourceTexture);
		}
	}

	return FReply::Handled();
}

FReply SSpriteExtractorWindow::OnSelectAllClicked()
{
	if (Canvas.IsValid())
	{
		Canvas->SelectAll(true);
		RefreshSpriteList();
		UpdateStatusTexts();
	}
	return FReply::Handled();
}

FReply SSpriteExtractorWindow::OnDeselectAllClicked()
{
	if (Canvas.IsValid())
	{
		Canvas->SelectAll(false);
		RefreshSpriteList();
		UpdateStatusTexts();
	}
	return FReply::Handled();
}

FReply SSpriteExtractorWindow::OnDetectSpritesClicked()
{
	if (!SourceTexture) return FReply::Handled();

	// Pre-flight pixel data — hoisted so DetectIslands can reuse it
	TArray<FColor> TestPixels;
	int32 TestW = 0, TestH = 0;

	// Pre-flight: verify texture data is accessible (Island mode loads pixels)
	if (DetectionMode == ESpriteDetectionMode::Island)
	{
		if (!FSpriteExtractionUtils::LoadTextureData(SourceTexture, TestPixels, TestW, TestH))
		{
			if (!FSpriteExtractionUtils::NeedsPaper2DSettings(SourceTexture))
			{
				// Settings look correct — likely a CPU access issue
				EAppReturnType::Type Result = FMessageDialog::Open(
					EAppMsgType::YesNo,
					LOCTEXT("ForceCPUAccess",
						"Texture data not accessible. This may be a CPU access issue.\n\n"
						"Force CPU access rebuild? This will modify the texture asset."));

				if (Result == EAppReturnType::Yes)
				{
					if (FSpriteExtractionUtils::ForceCPUAccess(SourceTexture))
					{
						if (Canvas.IsValid())
						{
							Canvas->SetTexture(SourceTexture);
						}
						// Retry load
						if (!FSpriteExtractionUtils::LoadTextureData(SourceTexture, TestPixels, TestW, TestH))
						{
							FNotificationInfo Info(LOCTEXT("LoadFailed",
								"Failed to load texture data. Try re-importing the texture from source."));
							Info.ExpireDuration = 5.0f;
							FSlateNotificationManager::Get().AddNotification(Info);
							return FReply::Handled();
						}
						// Success — fall through to detection
					}
					else
					{
						FNotificationInfo Info(LOCTEXT("ForceCPUFailed",
							"Could not force CPU access. The texture may need to be reimported from source."));
						Info.ExpireDuration = 5.0f;
						FSlateNotificationManager::Get().AddNotification(Info);
						return FReply::Handled();
					}
				}
				else
				{
					return FReply::Handled();
				}
			}
			else
			{
				// Settings are wrong — point user to banner
				FNotificationInfo Info(LOCTEXT("ApplySettingsFirst",
					"Apply Paper2D texture settings first (see banner above)."));
				Info.ExpireDuration = 5.0f;
				FSlateNotificationManager::Get().AddNotification(Info);
				return FReply::Handled();
			}
		}
	}

	DetectedSprites.Empty();
	if (Canvas.IsValid())
	{
		Canvas->ClearMergeSelection();
		Canvas->ExitEditMode(false);
	}

	if (DetectionMode == ESpriteDetectionMode::Island)
	{
		// Reuse preflight pixel data if available to avoid redundant texture load
		if (TestW > 0 && TestH > 0 && TestPixels.Num() > 0)
		{
			DetectIslands(&TestPixels, TestW, TestH);
		}
		else
		{
			DetectIslands();
		}
	}
	else
	{
		DetectGrid();
	}

	// Apply uniform bounds preview so canvas shows extraction size
	ApplyUniformBoundsPreview();

	// Warn if sprite count jumped significantly (noise from low MinSpriteSize)
	const int32 NewCount = DetectedSprites.Num();
	if (!bDismissedSpriteCountWarning
		&& bUniformDimensions
		&& PreviousDetectedSpriteCount > 0
		&& NewCount > PreviousDetectedSpriteCount * 1.5
		&& NewCount - PreviousDetectedSpriteCount > 3)
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("SpriteCountJump",
				"Sprite count jumped from {0} to {1}. Small noise islands may distort uniform bounds.\n\n"
				"Try increasing Island Merge Distance to absorb nearby noise into sprites."),
			FText::AsNumber(PreviousDetectedSpriteCount),
			FText::AsNumber(NewCount)));
		Info.bFireAndForget = false;
		Info.bAllowThrottleWhenFrameRateIsLow = false;

		TWeakPtr<SSpriteExtractorWindow> WeakWindow = SharedThis(this);

		// Indirection so lambdas can capture the notification before it's created
		auto WeakNotif = MakeShared<TWeakPtr<SNotificationItem>>();

		Info.ButtonDetails.Add(FNotificationButtonInfo(
			LOCTEXT("SpriteCountJumpOK", "OK"),
			LOCTEXT("SpriteCountJumpOKTooltip", "Dismiss"),
			FSimpleDelegate::CreateLambda([WeakNotif]()
			{
				if (TSharedPtr<SNotificationItem> Notif = WeakNotif->Pin())
				{
					Notif->SetCompletionState(SNotificationItem::CS_None);
					Notif->ExpireAndFadeout();
				}
			})));

		Info.ButtonDetails.Add(FNotificationButtonInfo(
			LOCTEXT("SpriteCountJumpDismiss", "Don't show again"),
			LOCTEXT("SpriteCountJumpDismissTooltip", "Dismiss and stop showing this warning for this session"),
			FSimpleDelegate::CreateLambda([WeakNotif, WeakWindow]()
			{
				if (TSharedPtr<SSpriteExtractorWindow> Window = WeakWindow.Pin())
				{
					Window->bDismissedSpriteCountWarning = true;
				}
				if (TSharedPtr<SNotificationItem> Notif = WeakNotif->Pin())
				{
					Notif->SetCompletionState(SNotificationItem::CS_None);
					Notif->ExpireAndFadeout();
				}
			})));

		TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
		if (NotificationPtr.IsValid())
		{
			NotificationPtr->SetCompletionState(SNotificationItem::CS_Pending);
			*WeakNotif = NotificationPtr;
		}
	}
	PreviousDetectedSpriteCount = NewCount;

	// Copy sprites to canvas
	Canvas->SetDetectedSprites(DetectedSprites);

	RefreshSpriteList();
	UpdateStatusTexts();

	UE_LOG(LogTemp, Log, TEXT("SpriteExtractor: Detected %d sprites"), DetectedSprites.Num());

	return FReply::Handled();
}

FReply SSpriteExtractorWindow::OnExtractSpritesClicked()
{
	if (bReExtractMode)
	{
		int32 Result = ReExtractSprites();
		if (Result > 0)
		{
			FNotificationInfo Info(FText::Format(
				LOCTEXT("ReExtractSuccess", "Re-extracted {0} sprites in-place."),
				FText::AsNumber(Result)));
			Info.bFireAndForget = true;
			Info.ExpireDuration = 5.0f;
			Info.bUseSuccessFailIcons = true;
			TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
			if (Notification.IsValid()) Notification->SetCompletionState(SNotificationItem::CS_Success);
		}
		return FReply::Handled();
	}

	// Validate character asset selection if checkbox is checked
	if (bAddToCharacterAsset && !TargetCharacterAsset)
	{
		FNotificationInfo Info(LOCTEXT("NoTargetAsset",
			"No Character Profile Asset selected. Please select a target asset or uncheck 'Add to Character Profile Asset'."));
		Info.ExpireDuration = 5.0f;
		Info.bFireAndForget = true;
		Info.bUseSuccessFailIcons = true;
		TSharedPtr<SNotificationItem> Notif = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notif.IsValid()) Notif->SetCompletionState(SNotificationItem::CS_Fail);
		return FReply::Handled();
	}

	// Returns positive count on full success, negative on cancellation, 0 on nothing
	int32 Result = ExtractSprites();

	if (Result > 0)
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("ExtractionSuccess", "Successfully extracted {0} sprites to {1}"),
			FText::AsNumber(Result), FText::FromString(GetOutputFolderName())));
		Info.bFireAndForget = true;
		Info.ExpireDuration = 5.0f;
		Info.bUseSuccessFailIcons = true;
		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(SNotificationItem::CS_Success);
		}

		TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
		if (ParentWindow.IsValid())
		{
			ParentWindow->RequestDestroyWindow();
		}
	}
	return FReply::Handled();
}

void SSpriteExtractorWindow::DetectIslands(const TArray<FColor>* /*PreloadedPixels*/, int32 /*PreloadedWidth*/, int32 /*PreloadedHeight*/)
{
	FSpriteDetectionParams Params;
	Params.AlphaThreshold = AlphaThreshold;
	Params.MinSpriteSize = MinSpriteSize;
	Params.bUse8DirectionalFloodFill = bUse8DirectionalFloodFill;
	Params.IslandMergeDistance = IslandMergeDistance;

	UE_LOG(LogTemp, Log, TEXT("SpriteExtractor: DetectIslands called — MinSize=%d, Alpha=%d, MergeDist=%d, 8Dir=%d"),
		MinSpriteSize, AlphaThreshold, IslandMergeDistance, bUse8DirectionalFloodFill ? 1 : 0);

	DetectedSprites = FSpriteExtractionUtils::DetectSpriteBounds(SourceTexture, Params);

	if (DetectedSprites.Num() == 0 && SourceTexture)
	{
		FNotificationInfo Info(LOCTEXT("TextureLoadFailed",
			"Failed to load texture data. Ensure texture has CPU access enabled (Compression Settings)."));
		Info.ExpireDuration = 8.0f;
		Info.bFireAndForget = true;
		Info.bUseSuccessFailIcons = true;
		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}
}

void SSpriteExtractorWindow::DetectGrid()
{
	if (!SourceTexture || GridColumns < 1 || GridRows < 1) return;

	int32 Width = SourceTexture->GetSizeX();
	int32 Height = SourceTexture->GetSizeY();
	if (Width <= 0 || Height <= 0) return;

	// Clamp grid dimensions to texture size to prevent zero-width cells
	GridColumns = FMath::Clamp(GridColumns, 1, Width);
	GridRows = FMath::Clamp(GridRows, 1, Height);

	int32 CellWidth = Width / GridColumns;
	int32 CellHeight = Height / GridRows;

	int32 Index = 0;
	for (int32 Row = 0; Row < GridRows; Row++)
	{
		for (int32 Col = 0; Col < GridColumns; Col++)
		{
			FDetectedSprite Sprite;
			Sprite.Bounds = FIntRect(
				Col * CellWidth,
				Row * CellHeight,
				(Col + 1) * CellWidth,
				(Row + 1) * CellHeight
			);
			Sprite.OriginalBounds = Sprite.Bounds;
			Sprite.bSelected = true;
			Sprite.Index = Index++;
			DetectedSprites.Add(Sprite);
		}
	}
}

FIntPoint SSpriteExtractorWindow::ComputeUniformSpriteSize(const TArray<FDetectedSprite>& Sprites) const
{
	FIntPoint Max(0, 0);
	for (const FDetectedSprite& S : Sprites)
	{
		const FIntPoint Size = S.GetOriginalSize();
		Max.X = FMath::Max(Max.X, Size.X);
		Max.Y = FMath::Max(Max.Y, Size.Y);
	}
	return Max;
}

void SSpriteExtractorWindow::ApplyUniformBoundsPreview()
{
	// Always reset to tight-fit first
	for (FDetectedSprite& S : DetectedSprites) { S.Bounds = S.OriginalBounds; }

	UE_LOG(LogTemp, Log, TEXT("SpriteExtractor: ApplyUniformBoundsPreview — bUniform=%d, %d sprites"),
		bUniformDimensions ? 1 : 0, DetectedSprites.Num());

	if (!bUniformDimensions || !SourceTexture || DetectedSprites.Num() < 2) return;

	const FIntPoint TexDims = FSpriteExtractionUtils::GetDetectionDimensions(SourceTexture);
	UE_LOG(LogTemp, Log, TEXT("SpriteExtractor: TexDims from GetDetectionDimensions: %dx%d (Source), GetSizeX/Y: %dx%d (Platform)"),
		TexDims.X, TexDims.Y, SourceTexture->GetSizeX(), SourceTexture->GetSizeY());

	if (TexDims.X <= 0 || TexDims.Y <= 0) return;

	FSpriteExtractionUtils::ComputeUniformBounds(DetectedSprites, TexDims.X, TexDims.Y);
}

FIntRect SSpriteExtractorWindow::ExpandBoundsToUniform(const FIntRect& OriginalBounds, FIntPoint UniformSize, ESpriteAnchor Anchor, int32 TexW, int32 TexH) const
{
	return FSpriteExtractionUtils::ExpandBoundsToUniform(OriginalBounds, UniformSize, Anchor, TexW, TexH);
}

int32 SSpriteExtractorWindow::ExtractSprites()
{
	if (!SourceTexture || !Canvas.IsValid()) return 0;

	// Collect selected sprites from canvas (which has the current selection state)
	TArray<FDetectedSprite> SelectedSprites;
	TArray<FDetectedSprite>& CanvasSprites = Canvas->GetDetectedSprites();
	for (const FDetectedSprite& Sprite : CanvasSprites)
	{
		if (Sprite.bSelected)
		{
			SelectedSprites.Add(Sprite);
		}
	}

	if (SelectedSprites.Num() == 0)
	{
		FNotificationInfo Info(LOCTEXT("NoSpritesSelected", "No sprites selected for extraction."));
		Info.ExpireDuration = 4.0f;
		Info.bFireAndForget = true;
		FSlateNotificationManager::Get().AddNotification(Info);
		return 0;
	}

	// Uniform dimensions: use the shared ComputeUniformBounds algorithm which
	// computes per-row cell strides and max-extent uniform bounds.
	const bool bApplyUniform = bUniformDimensions && SelectedSprites.Num() > 0;
	const FIntPoint TexDims = FSpriteExtractionUtils::GetDetectionDimensions(SourceTexture);
	if (bApplyUniform && SelectedSprites.Num() >= 2)
	{
		FSpriteExtractionUtils::ComputeUniformBounds(SelectedSprites, TexDims.X, TexDims.Y);
	}
	else if (bApplyUniform)
	{
		FIntPoint UniformSize = ComputeUniformSpriteSize(SelectedSprites);
		for (FDetectedSprite& Sprite : SelectedSprites)
		{
			Sprite.Bounds = ExpandBoundsToUniform(Sprite.OriginalBounds, UniformSize, UniformAnchor, TexDims.X, TexDims.Y);
		}
	}

	// Resolve output path using naming system
	FString ResolvedOutputPath = bCreateSubfolder ? (OutputPath / GetOutputFolderName()) : OutputPath;

	// Repack texture: create a tight packed texture instead of using the padded original
	UTexture2D* SpriteTexture = SourceTexture;
	TArray<FIntRect> PackedBounds; // bounds remapped to packed texture coordinates

	if (bRepackTexture && SelectedSprites.Num() > 0)
	{
		// Collect extraction regions
		TArray<FIntRect> Regions;
		for (const FDetectedSprite& S : SelectedSprites) { Regions.Add(S.Bounds); }

		FString PackedTexName = GetOutputFolderName();
		if (!PackedTexName.EndsWith(TEXT("_Texture")))
		{
			PackedTexName += TEXT("_Texture");
		}

		UTexture2D* PackedTex = FSpriteExtractionUtils::CreatePackedTexture(
			SourceTexture, Regions, PackedTexName, ResolvedOutputPath);

		if (PackedTex)
		{
			SpriteTexture = PackedTex;

			// Compute packed bounds — each region maps to a cell in the strip
			int32 CellW = 0, CellH = 0;
			for (const FIntRect& R : Regions)
			{
				CellW = FMath::Max(CellW, R.Width());
				CellH = FMath::Max(CellH, R.Height());
			}
			for (int32 i = 0; i < Regions.Num(); i++)
			{
				const int32 PadX = (CellW - Regions[i].Width()) / 2;
				const int32 PadY = (CellH - Regions[i].Height()) / 2;
				PackedBounds.Add(FIntRect(
					i * CellW + PadX, PadY,
					i * CellW + PadX + Regions[i].Width(),
					PadY + Regions[i].Height()));
			}
		}
	}

	if (PackedBounds.Num() == 0)
	{
		// Not repacking — move source texture into output folder
		FString TextureName = SourceTexture->GetName();
		if (!TextureName.EndsWith(TEXT("_Texture")))
		{
			TextureName += TEXT("_Texture");
		}
		FString DestPackageName = ResolvedOutputPath / TextureName;
		if (!FPackageName::DoesPackageExist(DestPackageName))
		{
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			TArray<FAssetRenameData> RenameData;
			RenameData.Emplace(SourceTexture, ResolvedOutputPath, TextureName);
			AssetTools.RenameAssets(RenameData);
		}
	}

	FScopedSlowTask Progress(SelectedSprites.Num(), LOCTEXT("ExtractingSprites", "Extracting sprites..."));
	Progress.MakeDialog(true);

	TArray<UPaperSprite*> CreatedSprites;
	bool bCancelled = false;

	for (int32 i = 0; i < SelectedSprites.Num(); i++)
	{
		if (Progress.ShouldCancel())
		{
			bCancelled = true;
			break;
		}

		Progress.EnterProgressFrame(1, FText::Format(
			LOCTEXT("CreatingSprite", "Creating sprite {0}/{1}"),
			FText::AsNumber(i + 1), FText::AsNumber(SelectedSprites.Num())));

		// Use packed bounds if available, otherwise original bounds
		const FIntRect& SpriteBounds = PackedBounds.IsValidIndex(i) ? PackedBounds[i] : SelectedSprites[i].Bounds;
		FString SpriteName = GetSpriteName(i);
		UPaperSprite* NewSprite = FSpriteExtractionUtils::CreateSpriteFromBounds(SpriteTexture, SpriteBounds, SpriteName, ResolvedOutputPath);
		if (NewSprite)
		{
			CreatedSprites.Add(NewSprite);
		}
	}

	// Create flipbook if requested
	UPaperFlipbook* Flipbook = nullptr;
	if (bCreateFlipbook && CreatedSprites.Num() > 0)
	{
		Flipbook = CreateFlipbook(CreatedSprites);
	}

	// Add to character asset if requested
	if (bAddToCharacterAsset && TargetCharacterAsset && Flipbook)
	{
		FScopedTransaction Transaction(LOCTEXT("AddFlipbook", "Add Flipbook to Character Asset"));
		TargetCharacterAsset->Modify();

		FString ResolvedAnimationName = AnimationName.TrimStartAndEnd();
		if (ResolvedAnimationName.IsEmpty())
		{
			ResolvedAnimationName = NameBase;
		}

		FString UniqueAnimationName = ResolvedAnimationName;
		int32 NameSuffix = 1;
		while (TargetCharacterAsset->FindFlipbookDataPtr(UniqueAnimationName) != nullptr)
		{
			UniqueAnimationName = FString::Printf(TEXT("%s_%d"), *ResolvedAnimationName, NameSuffix++);
		}

		FFlipbookProfileEntry NewAnimation;
		NewAnimation.Identity.FlipbookName = UniqueAnimationName;
		NewAnimation.Identity.Flipbook = Flipbook;
		NewAnimation.SourceTexture = SourceTexture;
		NewAnimation.SpritesOutputPath = ResolvedOutputPath;

		// Create frame data for each sprite
		for (int32 i = 0; i < SelectedSprites.Num(); i++)
		{
			FFrameHitboxData FrameData;
			FrameData.FrameName = FString::Printf(TEXT("%s_%02d"), *UniqueAnimationName, i);
			NewAnimation.CombatData.Frames.Add(FrameData);

			// Store extraction info
			FSpriteExtractionInfo ExtractionInfo;
			ExtractionInfo.SourceOffset = SelectedSprites[i].OriginalBounds.Min;
			ExtractionInfo.AlphaThreshold = AlphaThreshold;
			ExtractionInfo.ExtractionTime = FDateTime::Now();
			NewAnimation.CombatData.FrameExtractionInfo.Add(ExtractionInfo);
		}

		TargetCharacterAsset->Flipbooks.Add(NewAnimation);
		const int32 NewFlipbookIndex = TargetCharacterAsset->Flipbooks.Num() - 1;
		TargetCharacterAsset->SyncFramesToFlipbook(NewFlipbookIndex);
		TargetCharacterAsset->MarkPackageDirty();
	}

	// Delete original source texture after repack — the packed texture replaces it
	if (bRepackTexture && SpriteTexture != SourceTexture && SpriteTexture != nullptr)
	{
		FSpriteExtractionUtils::DeleteTextureAsset(SourceTexture);
	}

	// Show completion notification
	if (CreatedSprites.Num() > 0)
	{
		FString NotifText = bCancelled
			? FString::Printf(TEXT("Extraction cancelled. %d/%d sprites created."), CreatedSprites.Num(), SelectedSprites.Num())
			: FString::Printf(TEXT("Extracted %d sprites to %s"), CreatedSprites.Num(), *ResolvedOutputPath);

		FNotificationInfo Info(FText::FromString(NotifText));
		Info.bFireAndForget = true;
		Info.ExpireDuration = 8.0f;
		FString CapturedOutputPath = ResolvedOutputPath;
		Info.Hyperlink = FSimpleDelegate::CreateLambda([CapturedOutputPath]()
		{
			FString DiskPath;
			FPackageName::TryConvertLongPackageNameToFilename(CapturedOutputPath, DiskPath);
			FPlatformProcess::ExploreFolder(*DiskPath);
		});
		Info.HyperlinkText = LOCTEXT("OpenOutputFolder", "Open Output Folder");
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	return bCancelled ? -CreatedSprites.Num() : CreatedSprites.Num();
}

int32 SSpriteExtractorWindow::ReExtractSprites()
{
	if (!SourceTexture || !Canvas.IsValid() || !ReExtractFlipbook || !ReExtractProfileAsset) return 0;
	if (!ReExtractProfileAsset->Flipbooks.IsValidIndex(ReExtractFlipbookIndex)) return 0;

	// Collect selected sprites from canvas
	TArray<FDetectedSprite> SelectedSprites;
	TArray<FDetectedSprite>& CanvasSprites = Canvas->GetDetectedSprites();
	for (const FDetectedSprite& Sprite : CanvasSprites)
	{
		if (Sprite.bSelected)
		{
			SelectedSprites.Add(Sprite);
		}
	}

	if (SelectedSprites.Num() == 0) return 0;

	const FIntPoint TexDims = FSpriteExtractionUtils::GetDetectionDimensions(SourceTexture);

	// Apply uniform bounds (same as ExtractSprites)
	if (bUniformDimensions && SelectedSprites.Num() >= 2)
	{
		FSpriteExtractionUtils::ComputeUniformBounds(SelectedSprites, TexDims.X, TexDims.Y);
	}
	else if (bUniformDimensions)
	{
		FIntPoint UniformSize = ComputeUniformSpriteSize(SelectedSprites);
		for (FDetectedSprite& S : SelectedSprites)
		{
			S.Bounds = ExpandBoundsToUniform(S.OriginalBounds, UniformSize, UniformAnchor, TexDims.X, TexDims.Y);
		}
	}

	const int32 NumFrames = ReExtractFlipbook->GetNumKeyFrames();
	const int32 NumToUpdate = FMath::Min(NumFrames, SelectedSprites.Num());
	FFlipbookProfileEntry& Entry = ReExtractProfileAsset->Flipbooks[ReExtractFlipbookIndex];

	// Repack texture if requested
	UTexture2D* SpriteTexture = SourceTexture;
	TArray<FIntRect> PackedBounds;
	UTexture2D* OldSpriteTexture = nullptr;

	{
		const FPaperFlipbookKeyFrame& FirstFrame = ReExtractFlipbook->GetKeyFrameChecked(0);
		if (FirstFrame.Sprite) { OldSpriteTexture = FirstFrame.Sprite->GetSourceTexture(); }
	}

	if (bRepackTexture && NumToUpdate > 0)
	{
		TArray<FIntRect> Regions;
		for (int32 i = 0; i < NumToUpdate; i++) { Regions.Add(SelectedSprites[i].Bounds); }

		FString PackedTexName = Entry.Identity.FlipbookName + TEXT("_Texture");

		// Always derive output path from the first sprite's current location
		FString ResolvedOutputPath;
		{
			const FPaperFlipbookKeyFrame& FirstFrame = ReExtractFlipbook->GetKeyFrameChecked(0);
			if (FirstFrame.Sprite) { ResolvedOutputPath = FPackageName::GetLongPackagePath(FirstFrame.Sprite->GetPackage()->GetName()); }
		}
		if (ResolvedOutputPath.IsEmpty())
		{
			ResolvedOutputPath = Entry.SpritesOutputPath;
		}

		UTexture2D* PackedTex = FSpriteExtractionUtils::CreatePackedTexture(SourceTexture, Regions, PackedTexName, ResolvedOutputPath);
		if (PackedTex)
		{
			SpriteTexture = PackedTex;

			int32 CellW = 0, CellH = 0;
			for (const FIntRect& R : Regions) { CellW = FMath::Max(CellW, R.Width()); CellH = FMath::Max(CellH, R.Height()); }
			for (int32 i = 0; i < Regions.Num(); i++)
			{
				const int32 PadX = (CellW - Regions[i].Width()) / 2;
				const int32 PadY = (CellH - Regions[i].Height()) / 2;
				PackedBounds.Add(FIntRect(
					i * CellW + PadX, PadY,
					i * CellW + PadX + Regions[i].Width(),
					PadY + Regions[i].Height()));
			}
		}
	}

	// Update sprites in-place
	FScopedTransaction Transaction(FText::Format(
		LOCTEXT("ReExtractTxn", "Re-extract Sprites: {0}"),
		FText::FromString(Entry.Identity.FlipbookName)));
	ReExtractProfileAsset->Modify();

	UE_LOG(LogTemp, Log, TEXT("ReExtract: %d frames, repack=%d, srcTex=%s, spriteTex=%s"),
		NumToUpdate, bRepackTexture ? 1 : 0,
		*SourceTexture->GetName(),
		SpriteTexture ? *SpriteTexture->GetName() : TEXT("null"));

	int32 UpdatedCount = 0;
	for (int32 i = 0; i < NumToUpdate; i++)
	{
		const FPaperFlipbookKeyFrame& KeyFrame = ReExtractFlipbook->GetKeyFrameChecked(i);
		UPaperSprite* Sprite = KeyFrame.Sprite;
		if (!Sprite) continue;

		const FVector2D OldSourceUV = Sprite->GetSourceUV();
		const FVector2D OldSourceDim = Sprite->GetSourceSize();
		const bool bOldIsSourceTexture = (Sprite->GetSourceTexture() == SourceTexture);

		// Compute art-shift delta before updating the sprite
		FIntPoint ArtInNewSprite = SelectedSprites[i].OriginalBounds.Min - SelectedSprites[i].Bounds.Min;
		FIntPoint ArtInOldSprite = FIntPoint::ZeroValue;

		if (bOldIsSourceTexture && Entry.CombatData.FrameExtractionInfo.IsValidIndex(i))
		{
			const FIntPoint StoredOffset = Entry.CombatData.FrameExtractionInfo[i].SourceOffset;
			const FIntPoint OldExtractionMin(FMath::RoundToInt(OldSourceUV.X), FMath::RoundToInt(OldSourceUV.Y));

			// If SourceOffset was never initialized (zero but sprite isn't at origin),
			// the flipbook was extracted with tight-fit bounds before the SourceOffset
			// system existed. Art fills the entire sprite → ArtInOldSprite = (0,0).
			if (StoredOffset != FIntPoint::ZeroValue || OldExtractionMin == FIntPoint::ZeroValue)
			{
				ArtInOldSprite = StoredOffset - OldExtractionMin;
			}
		}

		const FIntPoint HitboxDelta = ArtInNewSprite - ArtInOldSprite;

		UE_LOG(LogTemp, Log, TEXT("ReExtract: [%d] OldSourceUV=(%.0f,%.0f) OldDim=(%.0f,%.0f) OldTex=%s bSameTex=%d"),
			i, OldSourceUV.X, OldSourceUV.Y, OldSourceDim.X, OldSourceDim.Y,
			Sprite->GetSourceTexture() ? *Sprite->GetSourceTexture()->GetName() : TEXT("null"),
			bOldIsSourceTexture ? 1 : 0);
		UE_LOG(LogTemp, Log, TEXT("ReExtract: [%d] OrigBounds=(%d,%d)-(%d,%d) NewBounds=(%d,%d)-(%d,%d)"),
			i, SelectedSprites[i].OriginalBounds.Min.X, SelectedSprites[i].OriginalBounds.Min.Y,
			SelectedSprites[i].OriginalBounds.Max.X, SelectedSprites[i].OriginalBounds.Max.Y,
			SelectedSprites[i].Bounds.Min.X, SelectedSprites[i].Bounds.Min.Y,
			SelectedSprites[i].Bounds.Max.X, SelectedSprites[i].Bounds.Max.Y);
		if (Entry.CombatData.FrameExtractionInfo.IsValidIndex(i))
		{
			const FSpriteExtractionInfo& EI = Entry.CombatData.FrameExtractionInfo[i];
			UE_LOG(LogTemp, Log, TEXT("ReExtract: [%d] StoredSourceOffset=(%d,%d) SpriteOffset=(%d,%d)"),
				i, EI.SourceOffset.X, EI.SourceOffset.Y, EI.SpriteOffset.X, EI.SpriteOffset.Y);
		}
		UE_LOG(LogTemp, Log, TEXT("ReExtract: [%d] ArtInNew=(%d,%d) ArtInOld=(%d,%d) → HitboxDelta=(%d,%d)"),
			i, ArtInNewSprite.X, ArtInNewSprite.Y, ArtInOldSprite.X, ArtInOldSprite.Y,
			HitboxDelta.X, HitboxDelta.Y);
		if (PackedBounds.IsValidIndex(i))
		{
			UE_LOG(LogTemp, Log, TEXT("ReExtract: [%d] PackedBounds=(%d,%d)-(%d,%d)"),
				i, PackedBounds[i].Min.X, PackedBounds[i].Min.Y,
				PackedBounds[i].Max.X, PackedBounds[i].Max.Y);
		}

		Sprite->Modify();

		const FIntRect& NewBounds = PackedBounds.IsValidIndex(i) ? PackedBounds[i] : SelectedSprites[i].Bounds;
		FSpriteExtractionUtils::UpdateSpriteSourceRegion(Sprite, SpriteTexture, NewBounds, HitboxDelta);
		Sprite->MarkPackageDirty();

		// Adjust hitboxes and sockets by the same delta
		if ((HitboxDelta.X != 0 || HitboxDelta.Y != 0) && Entry.CombatData.Frames.IsValidIndex(i))
		{
			FFrameHitboxData& FrameData = Entry.CombatData.Frames[i];
			for (FHitboxData& Hitbox : FrameData.Hitboxes)
			{
				UE_LOG(LogTemp, Log, TEXT("ReExtract: [%d] Hitbox %s (%d,%d) → (%d,%d)"),
					i, *UEnum::GetValueAsString(Hitbox.Type), Hitbox.X, Hitbox.Y,
					Hitbox.X + HitboxDelta.X, Hitbox.Y + HitboxDelta.Y);
				Hitbox.X += HitboxDelta.X;
				Hitbox.Y += HitboxDelta.Y;
			}
			for (FSocketData& Socket : FrameData.Sockets)
			{
				UE_LOG(LogTemp, Log, TEXT("ReExtract: [%d] Socket (%d,%d) → (%d,%d)"),
					i, Socket.X, Socket.Y,
					Socket.X + HitboxDelta.X, Socket.Y + HitboxDelta.Y);
				Socket.X += HitboxDelta.X;
				Socket.Y += HitboxDelta.Y;
			}
		}
		else if (Entry.CombatData.Frames.IsValidIndex(i))
		{
			const FFrameHitboxData& FrameData = Entry.CombatData.Frames[i];
			UE_LOG(LogTemp, Log, TEXT("ReExtract: [%d] HitboxDelta=(0,0), %d hitboxes %d sockets unchanged"),
				i, FrameData.Hitboxes.Num(), FrameData.Sockets.Num());
		}

		if (Entry.CombatData.FrameExtractionInfo.IsValidIndex(i))
		{
			FSpriteExtractionInfo& Info = Entry.CombatData.FrameExtractionInfo[i];
			Info.SourceOffset = SelectedSprites[i].OriginalBounds.Min;
			Info.ExtractionTime = FDateTime::Now();
			Info.SpriteOffset = FIntPoint::ZeroValue;
			Info.bHasCustomAlignment = false;
		}

		UpdatedCount++;
	}

	// Update stored output path to match where sprites actually live
	if (ReExtractFlipbook->GetNumKeyFrames() > 0)
	{
		const FPaperFlipbookKeyFrame& FirstFrame = ReExtractFlipbook->GetKeyFrameChecked(0);
		if (FirstFrame.Sprite)
		{
			Entry.SpritesOutputPath = FPackageName::GetLongPackagePath(FirstFrame.Sprite->GetPackage()->GetName());
		}
	}

	ReExtractProfileAsset->MarkPackageDirty();

	// Delete old texture if it's now orphaned (different from both source and current sprite texture)
	if (OldSpriteTexture != nullptr && OldSpriteTexture != SourceTexture && OldSpriteTexture != SpriteTexture)
	{
		FSpriteExtractionUtils::DeleteTextureAsset(OldSpriteTexture);
	}

	return UpdatedCount;
}

UPaperFlipbook* SSpriteExtractorWindow::CreateFlipbook(const TArray<UPaperSprite*>& Sprites)
{
	if (Sprites.Num() == 0) return nullptr;

	FString ResolvedOutputPath = bCreateSubfolder ? (OutputPath / GetOutputFolderName()) : OutputPath;
	FString ResolvedFlipbookName = GetFlipbookName();
	FString PackageName = ResolvedOutputPath / ResolvedFlipbookName;

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package) return nullptr;

	// Check for existing object to avoid fatal crash on name collision
	UPaperFlipbook* Flipbook = FindObject<UPaperFlipbook>(Package, *ResolvedFlipbookName);
	if (!Flipbook)
	{
		UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *ResolvedFlipbookName);
		if (Existing)
		{
			Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
		}
		Flipbook = NewObject<UPaperFlipbook>(Package, *ResolvedFlipbookName, RF_Public | RF_Standalone);
	}
	if (!Flipbook) return nullptr;

	// Use FScopedFlipbookMutator to modify the flipbook's protected members
	{
		FScopedFlipbookMutator Mutator(Flipbook);
		Mutator.FramesPerSecond = FlipbookFrameRate;
		Mutator.KeyFrames.Empty();

		for (UPaperSprite* Sprite : Sprites)
		{
			FPaperFlipbookKeyFrame KeyFrame;
			KeyFrame.Sprite = Sprite;
			KeyFrame.FrameRun = 1;
			Mutator.KeyFrames.Add(KeyFrame);
		}
	}

	// Mark package dirty and register
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Flipbook);

	UE_LOG(LogTemp, Log, TEXT("SpriteExtractor: Created flipbook '%s' with %d frames at %.1f FPS"),
		*ResolvedFlipbookName, Sprites.Num(), FlipbookFrameRate);

	return Flipbook;
}

void SSpriteExtractorWindow::RefreshSpriteList()
{
	if (!SpriteListBox.IsValid() || !Canvas.IsValid()) return;

	SpriteListBox->ClearChildren();
	SpriteListRows.Empty();

	TArray<FDetectedSprite>& Sprites = Canvas->GetDetectedSprites();
	for (int32 i = 0; i < Sprites.Num(); i++)
	{
		const FDetectedSprite& Sprite = Sprites[i];

		TSharedPtr<SBorder> RowWidget;

		SpriteListBox->AddSlot()
		.AutoHeight()
		.Padding(2)
		[
			SAssignNew(RowWidget, SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(4)
			.BorderBackgroundColor(Sprite.bSelected ? FLinearColor(0.1f, 0.3f, 0.1f, 1.0f) : FLinearColor(0.15f, 0.15f, 0.15f, 1.0f))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2)
				[
					SNew(SCheckBox)
					.IsChecked(Sprite.bSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda([this, i](ECheckBoxState State)
					{
						Canvas->ToggleSpriteSelection(i);
						RefreshSpriteList();
						UpdateStatusTexts();
					})
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(4, 0)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("SpriteIndex", "#{0}  {1}x{2}"),
							FText::AsNumber(Sprite.Index),
							FText::AsNumber(Sprite.Bounds.Width()),
							FText::AsNumber(Sprite.Bounds.Height())))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("SpritePos", "@ ({0}, {1})"),
							FText::AsNumber(Sprite.Bounds.Min.X),
							FText::AsNumber(Sprite.Bounds.Min.Y)))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
					]
				]
			]
		];

		SpriteListRows.Add(RowWidget);
	}
}

void SSpriteExtractorWindow::OnCanvasSpriteSelectionToggled(int32 SpriteIndex)
{
	RefreshSpriteList();
	UpdateStatusTexts();

	// Scroll the toggled sprite's row into view (deferred via active timer to let layout settle)
	const int32 TargetIndex = SpriteIndex;
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
		[this, TargetIndex](double, float) -> EActiveTimerReturnType
		{
			if (SpriteListScrollBox.IsValid() && SpriteListRows.IsValidIndex(TargetIndex))
			{
				SpriteListScrollBox->ScrollDescendantIntoView(SpriteListRows[TargetIndex]);
			}
			return EActiveTimerReturnType::Stop;
		}
	));
}

void SSpriteExtractorWindow::RefreshCanvas()
{
	if (Canvas.IsValid())
	{
		Canvas->SetDetectedSprites(DetectedSprites);
	}
}

// ============================================
// Undo/Redo
// ============================================

void SSpriteExtractorWindow::PushUndoState()
{
	FExtractorStateSnapshot Snapshot;
	if (Canvas.IsValid())
	{
		Snapshot.Sprites = Canvas->GetDetectedSprites();
	}

	UndoStack.Add(Snapshot);
	if (UndoStack.Num() > MaxUndoHistory)
	{
		UndoStack.RemoveAt(0);
	}

	// Clear redo stack on new action
	RedoStack.Empty();
}

void SSpriteExtractorWindow::Undo()
{
	if (UndoStack.Num() == 0) return;

	// Save current state to redo
	FExtractorStateSnapshot Current;
	if (Canvas.IsValid())
	{
		Current.Sprites = Canvas->GetDetectedSprites();
	}
	RedoStack.Add(Current);

	// Restore previous state
	FExtractorStateSnapshot Previous = UndoStack.Pop();
	RestoreState(Previous);
}

void SSpriteExtractorWindow::Redo()
{
	if (RedoStack.Num() == 0) return;

	// Save current state to undo
	FExtractorStateSnapshot Current;
	if (Canvas.IsValid())
	{
		Current.Sprites = Canvas->GetDetectedSprites();
	}
	UndoStack.Add(Current);

	// Restore next state
	FExtractorStateSnapshot Next = RedoStack.Pop();
	RestoreState(Next);
}

void SSpriteExtractorWindow::RestoreState(const FExtractorStateSnapshot& State)
{
	if (Canvas.IsValid())
	{
		Canvas->ExitEditMode(false);
		Canvas->ClearMergeSelection();
		Canvas->SetDetectedSprites(State.Sprites);
	}

	DetectedSprites = State.Sprites;
	RefreshSpriteList();
	UpdateStatusTexts();
}

// ============================================
// Merge & Absorb
// ============================================

void SSpriteExtractorWindow::MergeSelectedSprites(const TArray<int32>& IndicesToMerge)
{
	if (IndicesToMerge.Num() < 2 || !Canvas.IsValid()) return;

	PushUndoState();

	TArray<FDetectedSprite>& Sprites = Canvas->GetDetectedSprites();

	// Compute merged bounds from all selected indices
	FIntRect MergedBounds = Sprites[IndicesToMerge[0]].OriginalBounds;
	for (int32 i = 1; i < IndicesToMerge.Num(); i++)
	{
		const FIntRect& Other = Sprites[IndicesToMerge[i]].OriginalBounds;
		MergedBounds.Min.X = FMath::Min(MergedBounds.Min.X, Other.Min.X);
		MergedBounds.Min.Y = FMath::Min(MergedBounds.Min.Y, Other.Min.Y);
		MergedBounds.Max.X = FMath::Max(MergedBounds.Max.X, Other.Max.X);
		MergedBounds.Max.Y = FMath::Max(MergedBounds.Max.Y, Other.Max.Y);
	}

	// Remove all merged sprites (reverse order for safe removal)
	TArray<int32> SortedIndices = IndicesToMerge;
	SortedIndices.Sort([](const int32& A, const int32& B) { return A > B; });
	for (int32 Index : SortedIndices)
	{
		if (Sprites.IsValidIndex(Index))
		{
			Sprites.RemoveAt(Index);
		}
	}

	// Create new merged sprite
	FDetectedSprite MergedSprite;
	MergedSprite.OriginalBounds = MergedBounds;
	MergedSprite.Bounds = MergedBounds;
	MergedSprite.bSelected = true;
	Sprites.Add(MergedSprite);

	// Absorb any sprites fully contained in the merged bounds
	MergedSprite.Bounds = AbsorbContainedSprites(MergedBounds, Sprites, Sprites.Num() - 1);
	Sprites.Last().Bounds = MergedSprite.Bounds;
	Sprites.Last().OriginalBounds = MergedSprite.Bounds;

	// Re-index and sort — preserve row-major ordering for grid mode, left-to-right for island mode.
	if (DetectionMode == ESpriteDetectionMode::Grid)
	{
		Sprites.Sort([](const FDetectedSprite& A, const FDetectedSprite& B)
		{
			if (A.Bounds.Min.Y != B.Bounds.Min.Y)
			{
				return A.Bounds.Min.Y < B.Bounds.Min.Y;
			}
			return A.Bounds.Min.X < B.Bounds.Min.X;
		});
	}
	else
	{
		Sprites.Sort([](const FDetectedSprite& A, const FDetectedSprite& B)
		{
			if (A.Bounds.Min.X != B.Bounds.Min.X)
			{
				return A.Bounds.Min.X < B.Bounds.Min.X;
			}
			return A.Bounds.Min.Y < B.Bounds.Min.Y;
		});
	}
	for (int32 i = 0; i < Sprites.Num(); i++)
	{
		Sprites[i].Index = i;
	}

	Canvas->ClearMergeSelection();
	DetectedSprites = Sprites;
	RefreshSpriteList();
	UpdateStatusTexts();
}

FIntRect SSpriteExtractorWindow::AbsorbContainedSprites(FIntRect Bounds, TArray<FDetectedSprite>& Sprites, int32 SkipIndex)
{
	constexpr int32 MaxIterations = 100;
	for (int32 Iteration = 0; Iteration < MaxIterations; Iteration++)
	{
		bool bAbsorbed = false;
		for (int32 i = Sprites.Num() - 1; i >= 0; i--)
		{
			if (i == SkipIndex) continue;

			const FIntRect& Other = Sprites[i].OriginalBounds;
			// Check if Other is fully contained in Bounds
			if (Other.Min.X >= Bounds.Min.X && Other.Min.Y >= Bounds.Min.Y &&
				Other.Max.X <= Bounds.Max.X && Other.Max.Y <= Bounds.Max.Y)
			{
				Sprites.RemoveAt(i);
				if (i < SkipIndex)
				{
					SkipIndex--;
				}
				bAbsorbed = true;
			}
		}

		if (!bAbsorbed) break;
	}
	return Bounds;
}

// ============================================
// Auto-Update Detection
// ============================================

void SSpriteExtractorWindow::ScheduleAutoDetect()
{
	// Don't auto-detect during edit mode
	if (Canvas.IsValid() && Canvas->IsInEditMode()) return;

	// Cancel existing timer if active
	TSharedPtr<FActiveTimerHandle> ExistingTimer = ActiveDebounceTimerHandle.Pin();
	if (ExistingTimer.IsValid())
	{
		UnRegisterActiveTimer(ExistingTimer.ToSharedRef());
	}

	// Schedule new debounced detection
	ActiveDebounceTimerHandle = RegisterActiveTimer(AutoDetectDebounceSeconds,
		FWidgetActiveTimerDelegate::CreateSP(this, &SSpriteExtractorWindow::OnAutoDetectTimer));
}

EActiveTimerReturnType SSpriteExtractorWindow::OnAutoDetectTimer(double InCurrentTime, float InDeltaTime)
{
	if (SourceTexture)
	{
		OnDetectSpritesClicked();
	}
	return EActiveTimerReturnType::Stop;
}

// ============================================
// Naming System
// ============================================

void SSpriteExtractorWindow::AutoDetectNameParts(const FString& TextureName)
{
	// Strip _Texture suffix from previous extractions for backwards compatibility
	FString CleanName = TextureName;
	if (CleanName.EndsWith(TEXT("_Texture")))
	{
		CleanName.LeftChopInline(8);
	}

	// Find all separator positions (underscore and hyphen)
	SeparatorPositions.Empty();
	for (int32 i = 0; i < CleanName.Len(); i++)
	{
		TCHAR Ch = CleanName[i];
		if (Ch == TEXT('_') || Ch == TEXT('-'))
		{
			SeparatorPositions.Add(i);
		}
	}

	// Build split options for SComboBox
	SplitOptions.Empty();
	SplitOptions.Add(MakeShared<int32>(-1)); // "No split" option
	for (int32 i = 0; i < SeparatorPositions.Num(); i++)
	{
		SplitOptions.Add(MakeShared<int32>(i));
	}

	if (SeparatorPositions.Num() > 0)
	{
		// Default to last separator (most common: "CharName_AnimName")
		SplitIndex = SeparatorPositions.Num() - 1;

		int32 SepPos = SeparatorPositions[SplitIndex];
		NamePrefix = CleanName.Left(SepPos);
		NameSeparator = CleanName.Mid(SepPos, 1);
		NameBase = CleanName.Mid(SepPos + 1);
	}
	else
	{
		// No separators found — entire name is the base
		SplitIndex = -1;
		NamePrefix.Empty();
		NameSeparator = TEXT("_");
		NameBase = CleanName;
	}

	// Auto-populate animation name with the base name (without prefix)
	AnimationName = NameBase;

	// Refresh the split combo box if it exists
	if (SplitComboBox.IsValid())
	{
		SplitComboBox->RefreshOptions();

		// Restore selection by value — RefreshOptions invalidates old pointers
		for (const auto& Option : SplitOptions)
		{
			if (*Option == SplitIndex)
			{
				SplitComboBox->SetSelectedItem(Option);
				break;
			}
		}
	}
}

FString SSpriteExtractorWindow::GetSpriteName(int32 Index) const
{
	FString FullName;
	if (!NamePrefix.IsEmpty())
	{
		FullName = NamePrefix + NameSeparator;
	}
	FullName += NameBase + FString::Printf(TEXT("_%02d"), Index);
	return FullName;
}

FString SSpriteExtractorWindow::GetFlipbookName() const
{
	FString FullName;
	if (!NamePrefix.IsEmpty())
	{
		FullName = NamePrefix + NameSeparator;
	}
	FullName += NameBase;
	return FullName;
}

FString SSpriteExtractorWindow::GetOutputFolderName() const
{
	return NameBase;
}

void SSpriteExtractorWindow::UpdateOutputPath()
{
	if (!SourceTexture) return;
	FString TexturePath = FPackageName::GetLongPackagePath(SourceTexturePath);
	OutputPath = TexturePath;
}

#undef LOCTEXT_NAMESPACE
