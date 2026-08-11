// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "FlipbookDrawToolsPanel.h"
#include "FlipbookDrawModel.h"
#include "FlipbookPixelEdit.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Styling/CoreStyle.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "FlipbookDrawTools"

void SFlipbookDrawToolsPanel::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;
	OnFrameTransform = InArgs._OnFrameTransform;
	CanFrameTransform = InArgs._CanFrameTransform;

	if (Model.IsValid())
	{
		ToolStateHandle = Model->OnToolStateChanged.AddSP(this, &SFlipbookDrawToolsPanel::HandleToolStateChanged);

		// Seed a small default palette the first time.
		if (Model->GetSwatches().Num() == 0)
		{
			TArray<FLinearColor>& S = Model->GetSwatches();
			S.Add(FLinearColor::Black);
			S.Add(FLinearColor::White);
			S.Add(FLinearColor::Red);
			S.Add(FLinearColor::Green);
			S.Add(FLinearColor::Blue);
			S.Add(FLinearColor::Yellow);
		}
	}

	TWeakPtr<FFlipbookDrawModel> MW = Model;
	const FSlateFontInfo HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);

	SwatchBox = SNew(SWrapBox).UseAllottedSize(true);
	RebuildSwatches();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(6.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)

			// ── Drawing tools ──
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
			[ SNew(STextBlock).Text(LOCTEXT("ToolsHdr", "Drawing Tools")).Font(HeaderFont) ]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).Padding(1) [ BuildToolButton(EFlipbookDrawTool::Pencil,     LOCTEXT("Pencil", "Pencil (B)"),  LOCTEXT("PencilTip", "Pencil — draw with the primary color")) ]
				+ SHorizontalBox::Slot().FillWidth(1).Padding(1) [ BuildToolButton(EFlipbookDrawTool::Eraser,     LOCTEXT("Eraser", "Eraser (E)"),  LOCTEXT("EraserTip", "Eraser — paint transparency")) ]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).Padding(1) [ BuildToolButton(EFlipbookDrawTool::Eyedropper, LOCTEXT("Dropper", "Dropper (I)"), LOCTEXT("DropperTip", "Eyedropper — pick a color from the frame")) ]
				+ SHorizontalBox::Slot().FillWidth(1).Padding(1) [ BuildToolButton(EFlipbookDrawTool::Fill,       LOCTEXT("Fill", "Fill (G)"),      LOCTEXT("FillTip", "Bucket fill — flood the contiguous region")) ]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).Padding(1) [ BuildToolButton(EFlipbookDrawTool::Line,      LOCTEXT("Line", "Line (L)"),      LOCTEXT("LineTip", "Line — drag a straight, pixel-perfect stroke")) ]
				+ SHorizontalBox::Slot().FillWidth(1).Padding(1) [ BuildToolButton(EFlipbookDrawTool::Rectangle, LOCTEXT("Rectangle", "Rect (R)"), LOCTEXT("RectangleTip", "Rectangle — drag an outlined rectangle")) ]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6) [ SNew(SSeparator) ]

			// ── Playback ──
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
			[ SNew(STextBlock).Text(LOCTEXT("PlaybackHdr", "Playback")).Font(HeaderFont) ]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.ToolTipText(LOCTEXT("PlayTip", "Play / stop the animation (Space)"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([MW]() { if (TSharedPtr<FFlipbookDrawModel> M = MW.Pin()) { M->TogglePlaying(); } return FReply::Handled(); })
				[
					SNew(STextBlock)
					.Text_Lambda([MW]() { TSharedPtr<FFlipbookDrawModel> M = MW.Pin(); return (M.IsValid() && M->IsPlaying()) ? LOCTEXT("Stop", "Stop") : LOCTEXT("Play", "Play"); })
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6) [ SNew(SSeparator) ]

			// ── Brush ──
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
			[ SNew(STextBlock).Text(LOCTEXT("BrushHdr", "Brush Size")).Font(HeaderFont) ]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SSpinBox<int32>)
				.MinValue(1).MaxValue(64).MinSliderValue(1).MaxSliderValue(32)
				.Value_Lambda([MW]() { TSharedPtr<FFlipbookDrawModel> M = MW.Pin(); return M.IsValid() ? M->GetBrushSize() : 1; })
				.OnValueChanged_Lambda([MW](int32 V) { if (TSharedPtr<FFlipbookDrawModel> M = MW.Pin()) { M->SetBrushSize(V); } })
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6) [ SNew(SSeparator) ]

			// ── Color ──
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
			[ SNew(STextBlock).Text(LOCTEXT("ColorHdr", "Color")).Font(HeaderFont) ]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(LOCTEXT("PickColorTip", "Open the color picker"))
					.OnClicked(this, &SFlipbookDrawToolsPanel::OnOpenColorPicker)
					[
						SNew(SBox).WidthOverride(56).HeightOverride(24)
						[
							SNew(SColorBlock)
							.Color_Lambda([MW]() { TSharedPtr<FFlipbookDrawModel> M = MW.Pin(); return M.IsValid() ? M->GetPrimaryColor() : FLinearColor::White; })
							.ShowBackgroundForAlpha(true)
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
					.ToolTipText(LOCTEXT("AddSwatchTip", "Add the current color as a swatch"))
					.OnClicked(this, &SFlipbookDrawToolsPanel::OnAddSwatch)
					[ SNew(STextBlock).Text(LOCTEXT("AddSwatch", "+")) ]
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
			[ SwatchBox.ToSharedRef() ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6) [ SNew(SSeparator) ]

			// ── Drawing symmetry ──
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
			[ SNew(STextBlock).Text(LOCTEXT("MirrorHdr", "Drawing Symmetry")).Font(HeaderFont) ]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 6, 0)
				[
					SNew(SCheckBox)
					.ToolTipText(LOCTEXT("MirrorXTip", "Reflect each new brush stroke or shape left-to-right. Existing frame pixels are not flipped."))
					.IsChecked_Lambda([MW]() { TSharedPtr<FFlipbookDrawModel> M = MW.Pin(); return (M.IsValid() && EnumHasAnyFlags(M->GetMirror(), EFlipbookMirror::Horizontal)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([MW](ECheckBoxState State)
					{
						TSharedPtr<FFlipbookDrawModel> M = MW.Pin(); if (!M.IsValid()) { return; }
						EFlipbookMirror Cur = M->GetMirror();
						if (State == ECheckBoxState::Checked) { EnumAddFlags(Cur, EFlipbookMirror::Horizontal); } else { EnumRemoveFlags(Cur, EFlipbookMirror::Horizontal); }
						M->SetMirror(Cur);
					})
					[ SNew(STextBlock).Text(LOCTEXT("MirrorX", "Left ↔ Right")) ]
				]
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					SNew(SCheckBox)
					.ToolTipText(LOCTEXT("MirrorYTip", "Reflect each new brush stroke or shape top-to-bottom. Existing frame pixels are not flipped."))
					.IsChecked_Lambda([MW]() { TSharedPtr<FFlipbookDrawModel> M = MW.Pin(); return (M.IsValid() && EnumHasAnyFlags(M->GetMirror(), EFlipbookMirror::Vertical)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([MW](ECheckBoxState State)
					{
						TSharedPtr<FFlipbookDrawModel> M = MW.Pin(); if (!M.IsValid()) { return; }
						EFlipbookMirror Cur = M->GetMirror();
						if (State == ECheckBoxState::Checked) { EnumAddFlags(Cur, EFlipbookMirror::Vertical); } else { EnumRemoveFlags(Cur, EFlipbookMirror::Vertical); }
						M->SetMirror(Cur);
					})
					[ SNew(STextBlock).Text(LOCTEXT("MirrorY", "Top ↔ Bottom")) ]
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6) [ SNew(SSeparator) ]

			// ── Whole-frame transforms ──
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
			[ SNew(STextBlock).Text(LOCTEXT("TransformHdr", "Frame Transform")).Font(HeaderFont) ]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).Padding(1)
				[ BuildTransformButton(EFlipbookPixelTransform::FlipHorizontal, LOCTEXT("FlipHorizontal", "Flip L ↔ R"), LOCTEXT("FlipHorizontalTip", "Mirror all existing pixels left-to-right on the current frame (undoable)")) ]
				+ SHorizontalBox::Slot().FillWidth(1).Padding(1)
				[ BuildTransformButton(EFlipbookPixelTransform::FlipVertical, LOCTEXT("FlipVertical", "Flip T ↔ B"), LOCTEXT("FlipVerticalTip", "Mirror all existing pixels top-to-bottom on the current frame (undoable)")) ]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).Padding(1)
				[ BuildTransformButton(EFlipbookPixelTransform::Rotate180, LOCTEXT("Rotate180", "Rotate 180°"), LOCTEXT("Rotate180Tip", "Rotate the current frame 180 degrees without resizing it (undoable)")) ]
				+ SHorizontalBox::Slot().FillWidth(1).Padding(1)
				[ BuildTransformButton(EFlipbookPixelTransform::Clear, LOCTEXT("ClearFrame", "Clear"), LOCTEXT("ClearFrameTip", "Clear the current frame to transparency (undoable)")) ]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 1)
			[ SNew(STextBlock).Text(LOCTEXT("NudgeHdr", "Nudge 1 px")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)) ]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).Padding(1)
				[ BuildTransformButton(EFlipbookPixelTransform::ShiftLeft, LOCTEXT("NudgeLeft", "←"), LOCTEXT("NudgeLeftTip", "Move all frame pixels left by one; the new right edge is transparent")) ]
				+ SHorizontalBox::Slot().FillWidth(1).Padding(1)
				[ BuildTransformButton(EFlipbookPixelTransform::ShiftUp, LOCTEXT("NudgeUp", "↑"), LOCTEXT("NudgeUpTip", "Move all frame pixels up by one; the new bottom edge is transparent")) ]
				+ SHorizontalBox::Slot().FillWidth(1).Padding(1)
				[ BuildTransformButton(EFlipbookPixelTransform::ShiftDown, LOCTEXT("NudgeDown", "↓"), LOCTEXT("NudgeDownTip", "Move all frame pixels down by one; the new top edge is transparent")) ]
				+ SHorizontalBox::Slot().FillWidth(1).Padding(1)
				[ BuildTransformButton(EFlipbookPixelTransform::ShiftRight, LOCTEXT("NudgeRight", "→"), LOCTEXT("NudgeRightTip", "Move all frame pixels right by one; the new left edge is transparent")) ]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6) [ SNew(SSeparator) ]

			// ── View ──
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
			[ SNew(STextBlock).Text(LOCTEXT("ViewHdr", "View")).Font(HeaderFont) ]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("OnionTip", "Ghost the previous/next frame to align motion"))
				.IsChecked_Lambda([MW]() { TSharedPtr<FFlipbookDrawModel> M = MW.Pin(); return (M.IsValid() && M->IsOnionSkinEnabled()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([MW](ECheckBoxState State) { if (TSharedPtr<FFlipbookDrawModel> M = MW.Pin()) { M->SetOnionSkinEnabled(State == ECheckBoxState::Checked); } })
				[ SNew(STextBlock).Text(LOCTEXT("OnionSkin", "Onion Skin")) ]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("GridTip", "Show a 1px grid at high zoom"))
				.IsChecked_Lambda([MW]() { TSharedPtr<FFlipbookDrawModel> M = MW.Pin(); return (M.IsValid() && M->IsPixelGridEnabled()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([MW](ECheckBoxState State) { if (TSharedPtr<FFlipbookDrawModel> M = MW.Pin()) { M->SetPixelGridEnabled(State == ECheckBoxState::Checked); } })
				[ SNew(STextBlock).Text(LOCTEXT("PixelGrid", "Pixel Grid")) ]
			]
			]
		]
	];
}

SFlipbookDrawToolsPanel::~SFlipbookDrawToolsPanel()
{
	if (Model.IsValid() && ToolStateHandle.IsValid())
	{
		Model->OnToolStateChanged.Remove(ToolStateHandle);
	}
}

TSharedRef<SWidget> SFlipbookDrawToolsPanel::BuildToolButton(EFlipbookDrawTool Tool, const FText& Label, const FText& Tip)
{
	TWeakPtr<FFlipbookDrawModel> MW = Model;
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
		.ToolTipText(Tip)
		.HAlign(HAlign_Center)
		.OnClicked_Lambda([MW, Tool]() { if (TSharedPtr<FFlipbookDrawModel> M = MW.Pin()) { M->SetActiveTool(Tool); } return FReply::Handled(); })
		.ButtonColorAndOpacity_Lambda([MW, Tool]()
		{
			TSharedPtr<FFlipbookDrawModel> M = MW.Pin();
			const bool bActive = M.IsValid() && M->GetActiveTool() == Tool;
			return bActive ? FLinearColor(0.20f, 0.50f, 0.90f) : FLinearColor(0.28f, 0.28f, 0.28f);
		})
		[ SNew(STextBlock).Text(Label).Justification(ETextJustify::Center) ];
}

TSharedRef<SWidget> SFlipbookDrawToolsPanel::BuildTransformButton(
	EFlipbookPixelTransform Transform, const FText& Label, const FText& Tip)
{
	const FOnFlipbookFrameTransform TransformAction = OnFrameTransform;
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
		.ToolTipText(Tip)
		.HAlign(HAlign_Center)
		.IsEnabled(CanFrameTransform)
		.OnClicked_Lambda([TransformAction, Transform]() mutable
		{
			TransformAction.ExecuteIfBound(Transform);
			return FReply::Handled();
		})
		[ SNew(STextBlock).Text(Label).Justification(ETextJustify::Center) ];
}

void SFlipbookDrawToolsPanel::RebuildSwatches()
{
	if (!SwatchBox.IsValid() || !Model.IsValid())
	{
		return;
	}
	SwatchBox->ClearChildren();
	TWeakPtr<FFlipbookDrawModel> MW = Model;
	const TArray<FLinearColor>& Swatches = Model->GetSwatches();
	for (int32 i = 0; i < Swatches.Num(); ++i)
	{
		const FLinearColor C = Swatches[i];
		SwatchBox->AddSlot().Padding(2)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.OnClicked_Lambda([MW, C]() { if (TSharedPtr<FFlipbookDrawModel> M = MW.Pin()) { M->SetPrimaryColor(C); } return FReply::Handled(); })
			[
				SNew(SBox).WidthOverride(20).HeightOverride(20)
				[ SNew(SColorBlock).Color(C).ShowBackgroundForAlpha(true) ]
			]
		];
	}
}

FReply SFlipbookDrawToolsPanel::OnOpenColorPicker()
{
	if (!Model.IsValid())
	{
		return FReply::Handled();
	}
	TWeakPtr<FFlipbookDrawModel> MW = Model;
	FOnLinearColorValueChanged OnCommitted = FOnLinearColorValueChanged::CreateLambda([MW](FLinearColor NewColor)
	{
		if (TSharedPtr<FFlipbookDrawModel> M = MW.Pin()) { M->SetPrimaryColor(NewColor); }
	});
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2)
	FColorPickerArgs Args(Model->GetPrimaryColor(), OnCommitted);
#else
	// UE <5.2 has no (initial color, on-committed) FColorPickerArgs constructor; use the default ctor + fields.
	FColorPickerArgs Args;
	Args.InitialColorOverride = Model->GetPrimaryColor();
	Args.OnColorCommitted = OnCommitted;
#endif
	Args.bUseAlpha = true;
	Args.ParentWidget = SharedThis(this);
	OpenColorPicker(Args);
	return FReply::Handled();
}

FReply SFlipbookDrawToolsPanel::OnAddSwatch()
{
	if (Model.IsValid())
	{
		Model->GetSwatches().AddUnique(Model->GetPrimaryColor());
		RebuildSwatches();
	}
	return FReply::Handled();
}

void SFlipbookDrawToolsPanel::HandleToolStateChanged()
{
	// Lambda bindings re-read on paint; a repaint is enough to reflect tool/color/brush changes.
	Invalidate(EInvalidateWidgetReason::Paint);
}

#undef LOCTEXT_NAMESPACE
