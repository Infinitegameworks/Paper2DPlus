// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "AsepriteLayerImportDialog.h"
#include "AsepriteImporter.h"
#include "EditorCanvasUtils.h"
#include "Paper2DPlusCharacterProfileAsset.h"
#include "SpriteExtractionUtils.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "PropertyCustomizationHelpers.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "AsepriteLayerImportDialog"

// ==========================================
// SLayerImportPreviewCanvas
// ==========================================

void SLayerImportPreviewCanvas::Construct(const FArguments& InArgs)
{
	ParsedData = InArgs._ParsedData;
	PerLayerBuffers = InArgs._PerLayerBuffers;
	SetClipping(EWidgetClipping::ClipToBounds);

	// Initialize all visual layers as visible
	if (PerLayerBuffers)
	{
		for (const auto& Pair : *PerLayerBuffers)
		{
			LayerVisibility.Add(Pair.Key, true);
		}
	}

	// Create persistent preview texture
	if (ParsedData && ParsedData->bIsValid && ParsedData->Width > 0 && ParsedData->Height > 0)
	{
		UTexture2D* Tex = UTexture2D::CreateTransient(ParsedData->Width, ParsedData->Height, PF_B8G8R8A8);
		if (Tex)
		{
			Tex->SetFlags(RF_Transient);
			Tex->MipGenSettings = TMGS_NoMipmaps;
			Tex->Filter = TF_Nearest;
			Tex->NeverStream = true;
			Tex->SRGB = true;
			PreviewTexture.Reset(Tex);

			PreviewBrush.SetResourceObject(Tex);
			PreviewBrush.ImageSize = FVector2D(ParsedData->Width, ParsedData->Height);
			PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
			PreviewBrush.Tiling = ESlateBrushTileType::NoTile;
		}
	}

	bNeedsRecomposite = true;
}

SLayerImportPreviewCanvas::~SLayerImportPreviewCanvas()
{
	PreviewTexture.Reset();
}

FVector2D SLayerImportPreviewCanvas::ComputeDesiredSize(float) const
{
	return FVector2D(256, 256);
}

int32 SLayerImportPreviewCanvas::GetFrameCount() const
{
	if (!ParsedData) return 0;
	return ParsedData->Frames.Num();
}

void SLayerImportPreviewCanvas::SetFrameIndex(int32 NewIndex)
{
	if (NewIndex != FrameIndex)
	{
		FrameIndex = NewIndex;
		bNeedsRecomposite = true;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SLayerImportPreviewCanvas::SetLayerVisibility(int32 LayerIndex, bool bVisible)
{
	if (bool* Existing = LayerVisibility.Find(LayerIndex))
	{
		if (*Existing != bVisible)
		{
			*Existing = bVisible;
			bNeedsRecomposite = true;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
	}
}

bool SLayerImportPreviewCanvas::IsLayerVisible(int32 LayerIndex) const
{
	if (const bool* Existing = LayerVisibility.Find(LayerIndex))
	{
		return *Existing;
	}
	return false;
}

void SLayerImportPreviewCanvas::RecompositeFrame() const
{
	if (!PreviewTexture.IsValid() || !ParsedData || !PerLayerBuffers)
	{
		bNeedsRecomposite = false;
		return;
	}

	const int32 W = ParsedData->Width;
	const int32 H = ParsedData->Height;
	const int32 PixelCount = W * H;
	const int32 ClampedFrame = FMath::Clamp(FrameIndex, 0, FMath::Max(0, ParsedData->Frames.Num() - 1));

	// Build the visible Layer list in global back-to-front order.
	TArray<TPair<int32, int32>> SortedLayers; // (LayerIndex, global order)
	for (const auto& Pair : LayerVisibility)
	{
		if (!Pair.Value) continue; // not visible
		if (!PerLayerBuffers->Contains(Pair.Key)) continue;

		// Use Aseprite's layer order (index = bottom-to-top).
		SortedLayers.Add(TPair<int32, int32>(Pair.Key, Pair.Key));
	}
	SortedLayers.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
	{
		return A.Value < B.Value;
	});

	// Composite visible layers into a single buffer
	TArray<FColor> Composite;
	Composite.SetNumZeroed(PixelCount);

	for (const auto& Entry : SortedLayers)
	{
		const int32 LayerIdx = Entry.Key;
		const TArray<TArray<FColor>>& LayerFrames = (*PerLayerBuffers)[LayerIdx];
		if (!LayerFrames.IsValidIndex(ClampedFrame)) continue;

		const TArray<FColor>& SrcBuffer = LayerFrames[ClampedFrame];
		if (SrcBuffer.Num() != PixelCount) continue;

		// Alpha composite: src over dst
		for (int32 i = 0; i < PixelCount; i++)
		{
			const FColor& Src = SrcBuffer[i];
			if (Src.A == 0) continue;

			if (Src.A == 255 || Composite[i].A == 0)
			{
				Composite[i] = Src;
			}
			else
			{
				float SrcA = Src.A / 255.0f;
				float DstA = Composite[i].A / 255.0f;
				float OutA = SrcA + DstA * (1.0f - SrcA);
				if (OutA > 0.0f)
				{
					Composite[i].R = (uint8)FMath::Clamp((Src.R * SrcA + Composite[i].R * DstA * (1.0f - SrcA)) / OutA, 0.0f, 255.0f);
					Composite[i].G = (uint8)FMath::Clamp((Src.G * SrcA + Composite[i].G * DstA * (1.0f - SrcA)) / OutA, 0.0f, 255.0f);
					Composite[i].B = (uint8)FMath::Clamp((Src.B * SrcA + Composite[i].B * DstA * (1.0f - SrcA)) / OutA, 0.0f, 255.0f);
					Composite[i].A = (uint8)FMath::Clamp(OutA * 255.0f, 0.0f, 255.0f);
				}
			}
		}
	}

	// Write composite into the persistent texture
	UTexture2D* Tex = PreviewTexture.Get();
	if (Tex)
	{
		// UE pixel format is BGRA — FColor is already BGRA-ordered
		void* MipData = Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		if (MipData)
		{
			FMemory::Memcpy(MipData, Composite.GetData(), PixelCount * sizeof(FColor));
			Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
			Tex->UpdateResource();
		}
	}

	bNeedsRecomposite = false;
}

int32 SLayerImportPreviewCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Background
	FEditorCanvasUtils::DrawCheckerboard(OutDrawElements, LayerId, AllottedGeometry);
	LayerId++;

	if (!PreviewTexture.IsValid() || !ParsedData || !PerLayerBuffers)
	{
		return LayerId;
	}

	// Recomposite if dirty
	if (bNeedsRecomposite)
	{
		RecompositeFrame();
	}

	// Scale sprite to fit canvas (80% fit factor, centered)
	const FVector2D CanvasSize = AllottedGeometry.GetLocalSize();
	const FVector2D SpriteSize(ParsedData->Width, ParsedData->Height);

	if (SpriteSize.X <= 0 || SpriteSize.Y <= 0) return LayerId;

	float Scale = FMath::Min(CanvasSize.X / SpriteSize.X, CanvasSize.Y / SpriteSize.Y) * 0.8f;
	FVector2D DrawSize = SpriteSize * Scale;
	FVector2D DrawPos = (CanvasSize - DrawSize) * 0.5f;

	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		MakePaintGeometry(AllottedGeometry, DrawSize, FSlateLayoutTransform(DrawPos)),
		&PreviewBrush,
		ESlateDrawEffect::None,
		FLinearColor::White);

	return LayerId + 1;
}

// ==========================================
// SAsepiteLayerImportDialog
// ==========================================

void SAsepiteLayerImportDialog::Construct(const FArguments& InArgs)
{
	ParsedData = InArgs._ParsedData;
	PerLayerBuffers = InArgs._PerLayerBuffers;
	ParentWindow = InArgs._ParentWindow;

	// Initialize import settings
	ImportSettings.OutputPath = InArgs._DefaultOutputPath;
	ImportSettings.AssetPrefix = InArgs._DefaultAssetPrefix;
	ImportSettings.ImportMode = EAsepriteImportMode::LayerAssetNewProfile;

	// Initialize layer states from parsed data
	if (ParsedData)
	{
		int32 LayerOrderCounter = 0;
		for (int32 i = 0; i < ParsedData->Layers.Num(); i++)
		{
			const FAsepriteLayer& Layer = ParsedData->Layers[i];

			// Skip group layers
			if (Layer.LayerType == 1)
			{
				GroupCollapsed.Add(i, false);
				continue;
			}

			// Skip hitbox/socket layers
			bool bIsHitbox = false;
			for (const FAsepriteHitboxLayer& HL : ParsedData->HitboxLayers)
			{
				if (HL.LayerIndex == i)
				{
					bIsHitbox = true;
					break;
				}
			}
			if (bIsHitbox) continue;

			// Visual layer — enable by default
			ImportSettings.LayerImportEnabled.Add(i, true);
			ImportSettings.LayerOrder.Add(i, LayerOrderCounter++);
		}
	}

	// Import mode combo options
	ProfileModeOptions.Add(MakeShared<FString>(TEXT("Layer Asset + New Profile")));
	ProfileModeOptions.Add(MakeShared<FString>(TEXT("Layer Asset + Existing Profile")));
	ProfileModeOptions.Add(MakeShared<FString>(TEXT("Separate Assets per Layer")));
	SelectedProfileMode = ProfileModeOptions[0];

	// Build the dialog layout
	ChildSlot
	[
		SNew(SVerticalBox)

		// ---- Top bar: Profile mode + Output path + Asset prefix ----
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 8, 8, 4)
		[
			SNew(SVerticalBox)

			// Profile mode row
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(0.25f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ImportModeLabel", "Import Mode:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.35f)
				[
					SNew(SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&ProfileModeOptions)
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewSelection, ESelectInfo::Type)
					{
						SelectedProfileMode = NewSelection;
						if (NewSelection == ProfileModeOptions[0])
						{
							ImportSettings.ImportMode = EAsepriteImportMode::LayerAssetNewProfile;
						}
						else if (NewSelection == ProfileModeOptions[1])
						{
							ImportSettings.ImportMode = EAsepriteImportMode::LayerAssetExistingProfile;
						}
						else
						{
							ImportSettings.ImportMode = EAsepriteImportMode::SeparateAssetsPerLayer;
						}
						if (ProfilePickerBox.IsValid())
						{
							ProfilePickerBox->SetVisibility(
								ImportSettings.ImportMode == EAsepriteImportMode::LayerAssetExistingProfile
									? EVisibility::Visible : EVisibility::Collapsed);
						}
						UpdateImportButtonState();
					})
					.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
					{
						return SNew(STextBlock).Text(FText::FromString(*Item));
					})
					.InitiallySelectedItem(SelectedProfileMode)
					[
						SNew(STextBlock)
						.Text_Lambda([this]() -> FText
						{
							return SelectedProfileMode.IsValid() ? FText::FromString(*SelectedProfileMode) : FText::GetEmpty();
						})
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.40f)
				.Padding(4, 0, 0, 0)
				[
					SAssignNew(ProfilePickerBox, SBox)
					.Visibility(EVisibility::Collapsed) // hidden by default (Create New)
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(UPaper2DPlusCharacterProfileAsset::StaticClass())
						.ObjectPath_Lambda([this]() -> FString
						{
							UPaper2DPlusCharacterProfileAsset* Profile = ImportSettings.ExistingProfile.Get();
							return Profile ? Profile->GetPathName() : FString();
						})
						.OnObjectChanged_Lambda([this](const FAssetData& AssetData)
						{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
							ImportSettings.ExistingProfile = TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>(AssetData.ToSoftObjectPath());
#else
							ImportSettings.ExistingProfile = TSoftObjectPtr<UPaper2DPlusCharacterProfileAsset>(AssetData.GetSoftObjectPath());
#endif
							UpdateImportButtonState();
						})
					]
				]
			]

			// Output path row
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 2)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(0.25f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OutputPathLabel", "Output Path:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.75f)
				[
					// Content-browser path picker with a typed-entry fallback; both write ImportSettings.OutputPath.
					FSpriteExtractionUtils::MakeContentPathPicker(
						TAttribute<FString>::CreateLambda([this]() { return ImportSettings.OutputPath; }),
						[this](const FString& Path) { ImportSettings.OutputPath = Path; })
				]
			]

			// Asset prefix row
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 2)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(0.25f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AssetPrefixLabel", "Asset Prefix:"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.75f)
				[
					SNew(SEditableTextBox)
					.Text(FText::FromString(ImportSettings.AssetPrefix))
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
					{
						ImportSettings.AssetPrefix = Text.ToString();
					})
				]
			]
		]

		// ---- Separator ----
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 0)
		[
			SNew(SSeparator)
		]

		// ---- Main content: Left panel (layer tree) + Right panel (preview) ----
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8, 4)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			// Left panel — Layer tree (40%)
			+ SSplitter::Slot()
			.Value(0.40f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(LayerListBox, SVerticalBox)
				]
			]

			// Right panel — Preview + frame controls (60%)
			+ SSplitter::Slot()
			.Value(0.60f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(4)
				[
					SNew(SBox)
					.Clipping(EWidgetClipping::ClipToBounds)
					[
						SAssignNew(PreviewCanvas, SLayerImportPreviewCanvas)
						.ParsedData(ParsedData)
						.PerLayerBuffers(PerLayerBuffers)
					]
				]

				// Frame scrubber
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(4, 2, 4, 4)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.Text(LOCTEXT("PrevFrame", "<"))
						.OnClicked_Lambda([this]()
						{
							if (PreviewCanvas.IsValid())
							{
								int32 Total = PreviewCanvas->GetFrameCount();
								if (Total > 0)
								{
									int32 NewIdx = (PreviewCanvas->GetFrameIndex() - 1 + Total) % Total;
									PreviewCanvas->SetFrameIndex(NewIdx);
									if (FrameCounterText.IsValid())
									{
										FrameCounterText->Invalidate(EInvalidateWidgetReason::Paint);
									}
								}
							}
							return FReply::Handled();
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8, 0)
					[
						SAssignNew(FrameCounterText, STextBlock)
						.Text_Lambda([this]() -> FText
						{
							if (!PreviewCanvas.IsValid()) return FText::GetEmpty();
							int32 Total = PreviewCanvas->GetFrameCount();
							if (Total <= 0) return LOCTEXT("NoFrames", "No frames");
							return FText::Format(LOCTEXT("FrameCountFmt", "Frame {0} / {1}"),
								FText::AsNumber(PreviewCanvas->GetFrameIndex() + 1),
								FText::AsNumber(Total));
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
						.Text(LOCTEXT("NextFrame", ">"))
						.OnClicked_Lambda([this]()
						{
							if (PreviewCanvas.IsValid())
							{
								int32 Total = PreviewCanvas->GetFrameCount();
								if (Total > 0)
								{
									int32 NewIdx = (PreviewCanvas->GetFrameIndex() + 1) % Total;
									PreviewCanvas->SetFrameIndex(NewIdx);
									if (FrameCounterText.IsValid())
									{
										FrameCounterText->Invalidate(EInvalidateWidgetReason::Paint);
									}
								}
							}
							return FReply::Handled();
						})
					]
				]
			]
		]

		// ---- Separator ----
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 0)
		[
			SNew(SSeparator)
		]

		// ---- Bottom bar: Cancel + Import ----
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 4, 8, 8)
		[
			SNew(SHorizontalBox)

			// Summary text
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() -> FText
				{
					if (!ParsedData) return FText::GetEmpty();
					int32 VisualCount = CountCheckedVisualLayers();
					int32 HitboxCount = ParsedData->HitboxLayers.Num();
					if (VisualCount > 0 && HitboxCount > 0)
					{
						return FText::Format(LOCTEXT("SummaryBoth", "{0} visual layers, {1} data layers selected"),
							FText::AsNumber(VisualCount), FText::AsNumber(HitboxCount));
					}
					if (VisualCount > 0)
					{
						return FText::Format(LOCTEXT("SummaryVisual", "{0} visual layers selected"),
							FText::AsNumber(VisualCount));
					}
					if (HitboxCount > 0)
					{
						return FText::Format(LOCTEXT("SummaryHitbox", "{0} data layers available"),
							FText::AsNumber(HitboxCount));
					}
					return LOCTEXT("SummaryNone", "No layers selected");
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("CancelButton", "Cancel"))
				.OnClicked_Lambda([this]()
				{
					CloseDialog(false);
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0)
			[
				SAssignNew(ImportButton, SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
				.OnClicked_Lambda([this]()
				{
					CloseDialog(true);
					return FReply::Handled();
				})
				[
					SAssignNew(ImportButtonText, STextBlock)
					.Text(LOCTEXT("ImportButton", "Import"))
				]
			]
		]
	];

	// Build the layer list
	RebuildLayerList();
	UpdateImportButtonState();
}

void SAsepiteLayerImportDialog::CloseDialog(bool bConfirmed)
{
	ImportSettings.bUserConfirmed = bConfirmed;

	if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}
}

int32 SAsepiteLayerImportDialog::CountCheckedVisualLayers() const
{
	int32 Count = 0;
	for (const auto& Pair : ImportSettings.LayerImportEnabled)
	{
		if (Pair.Value) Count++;
	}
	return Count;
}

void SAsepiteLayerImportDialog::UpdateImportButtonState()
{
	if (!ImportButton.IsValid() || !ImportButtonText.IsValid()) return;

	int32 VisualCount = CountCheckedVisualLayers();
	int32 HitboxCount = ParsedData ? ParsedData->HitboxLayers.Num() : 0;

	if (VisualCount == 0 && HitboxCount == 0)
	{
		// Nothing to import — disable
		ImportButton->SetEnabled(false);
		ImportButtonText->SetText(LOCTEXT("ImportButton", "Import"));
	}
	else if (VisualCount == 0 && HitboxCount > 0)
	{
		// Hitbox only
		ImportButton->SetEnabled(true);
		ImportButtonText->SetText(LOCTEXT("ImportHitboxOnly", "Import Hitbox Data Only"));
	}
	else
	{
		// Normal import
		ImportButton->SetEnabled(true);
		ImportButtonText->SetText(LOCTEXT("ImportButton", "Import"));
	}
}

void SAsepiteLayerImportDialog::RebuildLayerList()
{
	if (!LayerListBox.IsValid() || !ParsedData) return;
	LayerListBox->ClearChildren();

	// Build layer rows from hierarchy
	// We iterate the hierarchy and build rows with indentation based on ChildLevel
	for (int32 LayerIdx = 0; LayerIdx < ParsedData->Layers.Num(); LayerIdx++)
	{
		const FAsepriteLayer& Layer = ParsedData->Layers[LayerIdx];
		float Indent = Layer.ChildLevel * 16.0f;

		// Check if this layer is inside a collapsed group
		bool bInsideCollapsedGroup = false;
		if (Layer.ChildLevel > 0)
		{
			// Walk backward to find parent group(s) and check if any are collapsed
			for (int32 PrevIdx = LayerIdx - 1; PrevIdx >= 0; PrevIdx--)
			{
				const FAsepriteLayer& PrevLayer = ParsedData->Layers[PrevIdx];
				if (PrevLayer.ChildLevel < Layer.ChildLevel && PrevLayer.LayerType == 1)
				{
					if (bool* bCollapsed = GroupCollapsed.Find(PrevIdx))
					{
						if (*bCollapsed)
						{
							bInsideCollapsedGroup = true;
							break;
						}
					}
					// Check further up for parent groups
					if (PrevLayer.ChildLevel == 0) break;
				}
				else if (PrevLayer.ChildLevel < Layer.ChildLevel - 1)
				{
					break;
				}
			}
		}

		if (bInsideCollapsedGroup) continue;

		// Check if this is a hitbox/socket layer
		const FAsepriteHitboxLayer* HitboxInfo = nullptr;
		for (const FAsepriteHitboxLayer& HL : ParsedData->HitboxLayers)
		{
			if (HL.LayerIndex == LayerIdx)
			{
				HitboxInfo = &HL;
				break;
			}
		}

		if (Layer.LayerType == 1)
		{
			// ---- Group layer ----
			bool bIsCollapsed = false;
			if (bool* Val = GroupCollapsed.Find(LayerIdx))
			{
				bIsCollapsed = *Val;
			}

			LayerListBox->AddSlot()
			.AutoHeight()
			.Padding(Indent, 2, 4, 2)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(FMargin(4, 3))
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 4, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "NoBorder")
						.OnClicked_Lambda([this, LayerIdx]()
						{
							if (bool* Val = GroupCollapsed.Find(LayerIdx))
							{
								*Val = !(*Val);
							}
							RebuildLayerList();
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text_Lambda([this, LayerIdx]() -> FText
							{
								bool bCollapsed = false;
								if (bool* Val = GroupCollapsed.Find(LayerIdx))
								{
									bCollapsed = *Val;
								}
								return bCollapsed ? LOCTEXT("Expand", "▸") : LOCTEXT("Collapse", "▾");
							})
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						]
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Layer.Name))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]
				]
			];
		}
		else if (HitboxInfo)
		{
			// ---- Hitbox/Socket layer (no import checkbox) ----
			FString TypeLabel;
			if (HitboxInfo->bIsSocket)
			{
				TypeLabel = FString::Printf(TEXT("[Socket: %s]"), *HitboxInfo->SocketName);
			}
			else
			{
				TypeLabel = HitboxInfo->HitboxType == EHitboxType::Attack
					? TEXT("[Attack]") : TEXT("[Hurtbox]");
			}

			LayerListBox->AddSlot()
			.AutoHeight()
			.Padding(Indent, 1, 4, 1)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TypeLabel))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.5f, 0.2f)))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Layer.Name))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]
			];
		}
		else
		{
			// ---- Visual layer ----
			bool* bEnabled = ImportSettings.LayerImportEnabled.Find(LayerIdx);
			int32* LayerOrder = ImportSettings.LayerOrder.Find(LayerIdx);

			LayerListBox->AddSlot()
			.AutoHeight()
			.Padding(Indent, 1, 4, 1)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor_Lambda([this, LayerIdx]() -> FSlateColor
				{
					// Highlight if visible in preview
					if (PreviewCanvas.IsValid() && PreviewCanvas->IsLayerVisible(LayerIdx))
					{
						return FSlateColor(FLinearColor(0.08f, 0.12f, 0.18f, 1.0f));
					}
					return FSlateColor(FLinearColor(0.03f, 0.03f, 0.03f, 1.0f));
				})
				.Padding(FMargin(4, 2))
				[
					SNew(SHorizontalBox)

					// Import checkbox
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 4, 0)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([bEnabled]()
						{
							return (bEnabled && *bEnabled) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([this, bEnabled](ECheckBoxState NewState)
						{
							if (bEnabled)
							{
								*bEnabled = (NewState == ECheckBoxState::Checked);
								UpdateImportButtonState();
							}
						})
					]

					// Layer name (clickable — toggles preview visibility)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
						.Cursor(EMouseCursor::Hand)
						.OnMouseButtonDown_Lambda([this, LayerIdx](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
						{
							if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && PreviewCanvas.IsValid())
							{
								PreviewCanvas->SetLayerVisibility(LayerIdx, !PreviewCanvas->IsLayerVisible(LayerIdx));
								// Refresh to update highlight
								if (LayerListBox.IsValid()) LayerListBox->Invalidate(EInvalidateWidgetReason::Paint);
								return FReply::Handled();
							}
							return FReply::Unhandled();
						})
						[
							SNew(STextBlock)
							.Text(FText::FromString(Layer.Name))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						]
					]

					// Global-order label
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 0)
					[
						SNew(STextBlock)
						.Text_Lambda([LayerOrder]() -> FText
						{
							if (LayerOrder)
							{
								return FText::Format(LOCTEXT("LayerOrderFmt", "order:{0}"), FText::AsNumber(*LayerOrder));
							}
							return FText::GetEmpty();
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
						.MinDesiredWidth(24)
					]

					// Visibility eye icon
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(2, 0, 0, 0)
					[
						SNew(STextBlock)
						.Text_Lambda([this, LayerIdx]() -> FText
						{
							if (PreviewCanvas.IsValid() && PreviewCanvas->IsLayerVisible(LayerIdx))
							{
								return LOCTEXT("EyeOpen", "◉");
							}
							return LOCTEXT("EyeClosed", "○");
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity_Lambda([this, LayerIdx]() -> FSlateColor
						{
							if (PreviewCanvas.IsValid() && PreviewCanvas->IsLayerVisible(LayerIdx))
							{
								return FSlateColor(FLinearColor(0.3f, 0.7f, 0.9f));
							}
							return FSlateColor(FLinearColor(0.3f, 0.3f, 0.3f));
						})
					]
				]
			];
		}
	}

	// ---- Data Layers summary section ----
	if (ParsedData->HitboxLayers.Num() > 0)
	{
		int32 AttackCount = 0;
		int32 HurtboxCount = 0;
		int32 SocketCount = 0;

		for (const FAsepriteHitboxLayer& HL : ParsedData->HitboxLayers)
		{
			if (HL.bIsSocket)
			{
				SocketCount++;
			}
			else if (HL.HitboxType == EHitboxType::Attack)
			{
				AttackCount++;
			}
			else
			{
				HurtboxCount++;
			}
		}

		LayerListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 8, 0, 2)
		[
			SNew(SSeparator)
		];

		LayerListBox->AddSlot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DataLayersHeader", "Data Layers"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.5f, 0.2f)))
		];

		TArray<FString> SummaryParts;
		if (AttackCount > 0) SummaryParts.Add(FString::Printf(TEXT("%d attack"), AttackCount));
		if (HurtboxCount > 0) SummaryParts.Add(FString::Printf(TEXT("%d hurtbox"), HurtboxCount));
		if (SocketCount > 0) SummaryParts.Add(FString::Printf(TEXT("%d socket"), SocketCount));

		FString SummaryText = FString::Join(SummaryParts, TEXT(", "));

		LayerListBox->AddSlot()
		.AutoHeight()
		.Padding(8, 1, 4, 4)
		[
			SNew(STextBlock)
			.Text(FText::FromString(SummaryText))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
		];
	}
}

#undef LOCTEXT_NAMESPACE
