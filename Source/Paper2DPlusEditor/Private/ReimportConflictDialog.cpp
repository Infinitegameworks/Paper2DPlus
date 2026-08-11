// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "ReimportConflictDialog.h"
#include "AsepriteReimporter.h"

#include "Widgets/SWindow.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Framework/Application/SlateApplication.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "ReimportConflictDialog"

bool SReimportConflictDialog::bDialogOpen = false;

// =============================================================================
// Static helpers
// =============================================================================

static FText GetConflictTypeLabel(EReimportConflictType Type)
{
	switch (Type)
	{
	case EReimportConflictType::LayerDeleted:             return LOCTEXT("TypeLayerDeleted", "Layer Deleted");
	case EReimportConflictType::LayerRenamed:             return LOCTEXT("TypeLayerRenamed", "Layer Renamed");
	case EReimportConflictType::TagRenamed:               return LOCTEXT("TypeTagRenamed", "Tag Renamed");
	case EReimportConflictType::UniformBoundsInstability: return LOCTEXT("TypeBoundsInstability", "Bounds Instability");
	default:                                              return LOCTEXT("TypeUnknown", "Unknown");
	}
}

static FLinearColor GetConflictTypeColor(EReimportConflictType Type)
{
	switch (Type)
	{
	case EReimportConflictType::LayerDeleted:             return FLinearColor(0.9f, 0.3f, 0.3f); // red
	case EReimportConflictType::LayerRenamed:             return FLinearColor(0.9f, 0.7f, 0.2f); // orange
	case EReimportConflictType::TagRenamed:               return FLinearColor(0.9f, 0.7f, 0.2f); // orange
	case EReimportConflictType::UniformBoundsInstability: return FLinearColor(0.9f, 0.7f, 0.2f); // orange
	default:                                              return FLinearColor(0.6f, 0.6f, 0.6f);
	}
}

/** Resolution options per conflict type. Each entry is {Resolution, ButtonLabel}. */
struct FResolutionOption
{
	EReimportConflictResolution Resolution;
	FText Label;
};

static TArray<FResolutionOption> GetResolutionOptions(EReimportConflictType Type)
{
	TArray<FResolutionOption> Options;
	switch (Type)
	{
	case EReimportConflictType::LayerDeleted:
		Options.Add({ EReimportConflictResolution::RemoveOld,     LOCTEXT("BtnRemove", "Remove") });
		Options.Add({ EReimportConflictResolution::KeepOrphaned,  LOCTEXT("BtnKeepOrphaned", "Keep Orphaned") });
		break;
	case EReimportConflictType::LayerRenamed:
		Options.Add({ EReimportConflictResolution::AcceptRename,  LOCTEXT("BtnAcceptRename", "Accept Rename") });
		Options.Add({ EReimportConflictResolution::KeepBoth,      LOCTEXT("BtnKeepBoth", "Keep Both") });
		Options.Add({ EReimportConflictResolution::RemoveOld,     LOCTEXT("BtnRemoveOld", "Remove Old") });
		break;
	case EReimportConflictType::TagRenamed:
		Options.Add({ EReimportConflictResolution::AcceptRename,  LOCTEXT("BtnAcceptTagRename", "Accept Rename") });
		Options.Add({ EReimportConflictResolution::KeepBoth,      LOCTEXT("BtnKeepBothTags", "Keep Both") });
		break;
	case EReimportConflictType::UniformBoundsInstability:
		Options.Add({ EReimportConflictResolution::AcceptNew,     LOCTEXT("BtnAcceptNew", "Accept New") });
		Options.Add({ EReimportConflictResolution::KeepCurrent,   LOCTEXT("BtnKeepCurrent", "Keep Current") });
		break;
	}
	return Options;
}

// =============================================================================
// ShowConflictDialog / IsDialogOpen
// =============================================================================

bool SReimportConflictDialog::ShowConflictDialog(TArray<FReimportConflict>& Conflicts)
{
	if (bDialogOpen)
	{
		return false;
	}

	bDialogOpen = true;

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("DialogTitle", "Reimport Conflicts"))
		.ClientSize(FVector2D(600, 400))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.IsTopmostWindow(true);

	TSharedPtr<SReimportConflictDialog> Dialog;
	Window->SetContent(
		SAssignNew(Dialog, SReimportConflictDialog)
			.Conflicts(&Conflicts)
			.ParentWindow(Window)
	);

	FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().GetActiveTopLevelWindow());

	bDialogOpen = false;
	return Dialog->WasConfirmed();
}

bool SReimportConflictDialog::IsDialogOpen()
{
	return bDialogOpen;
}

// =============================================================================
// Construct
// =============================================================================

void SReimportConflictDialog::Construct(const FArguments& InArgs)
{
	Conflicts = InArgs._Conflicts;
	ParentWindow = InArgs._ParentWindow;

	ChildSlot
	[
		SNew(SVerticalBox)

		// ---- Header text ----
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12, 12, 12, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("HeaderText",
				"The following conflicts were detected during auto-reimport.\nChoose how to resolve each one:"))
			.AutoWrapText(true)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
		]

		// ---- Separator ----
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12, 4, 12, 4)
		[
			SNew(SSeparator)
		]

		// ---- Scrollable conflict list ----
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(12, 0)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(ConflictListBox, SVerticalBox)
			]
		]

		// ---- Separator ----
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12, 4, 12, 0)
		[
			SNew(SSeparator)
		]

		// ---- Bottom bar ----
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12, 8, 12, 12)
		[
			SNew(SHorizontalBox)

			// Spacer
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)

			// Cancel
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("CancelBtn", "Cancel"))
				.OnClicked_Lambda([this]()
				{
					CloseDialog(false);
					return FReply::Handled();
				})
			]

			// Apply Resolutions
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0)
			[
				SAssignNew(ApplyButton, SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
				.IsEnabled_Lambda([this]() { return AreAllConflictsResolved(); })
				.OnClicked_Lambda([this]()
				{
					CloseDialog(true);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ApplyBtn", "Apply Resolutions"))
				]
			]
		]
	];

	RebuildConflictList();
}

// =============================================================================
// CloseDialog
// =============================================================================

void SReimportConflictDialog::CloseDialog(bool bAccept)
{
	if (bAccept)
	{
		bConfirmed = true;
	}
	else
	{
		// Reset all resolutions to Unresolved on cancel
		if (Conflicts)
		{
			for (FReimportConflict& C : *Conflicts)
			{
				C.Resolution = EReimportConflictResolution::Unresolved;
			}
		}
		bConfirmed = false;
	}

	if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}
}

// =============================================================================
// AreAllConflictsResolved
// =============================================================================

bool SReimportConflictDialog::AreAllConflictsResolved() const
{
	if (!Conflicts || Conflicts->Num() == 0)
	{
		return false;
	}

	for (const FReimportConflict& C : *Conflicts)
	{
		if (C.Resolution == EReimportConflictResolution::Unresolved)
		{
			return false;
		}
	}
	return true;
}

// =============================================================================
// RebuildConflictList
// =============================================================================

void SReimportConflictDialog::RebuildConflictList()
{
	if (!ConflictListBox.IsValid() || !Conflicts)
	{
		return;
	}

	ConflictListBox->ClearChildren();

	for (int32 Idx = 0; Idx < Conflicts->Num(); Idx++)
	{
		FReimportConflict& Conflict = (*Conflicts)[Idx];

		// Row container
		ConflictListBox->AddSlot()
		.AutoHeight()
		.Padding(0, 3)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(8, 6))
			[
				SNew(SVerticalBox)

				// Top line: type label + old/new names
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 2)
				[
					SNew(SHorizontalBox)

					// Type label
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 8, 0)
					[
						SNew(STextBlock)
						.Text(GetConflictTypeLabel(Conflict.Type))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(FSlateColor(GetConflictTypeColor(Conflict.Type)))
					]

					// Names (OldName -> NewName if applicable)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([this, Idx]() -> FText
						{
							if (!Conflicts || !Conflicts->IsValidIndex(Idx))
							{
								return FText::GetEmpty();
							}
							const FReimportConflict& C = (*Conflicts)[Idx];
							if (!C.NewName.IsEmpty() && C.NewName != C.OldName)
							{
								return FText::Format(LOCTEXT("NameTransition", "{0}  ->  {1}"),
									FText::FromString(C.OldName),
									FText::FromString(C.NewName));
							}
							return FText::FromString(C.OldName);
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]
				]

				// Description
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Conflict.Description))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
					.AutoWrapText(true)
				]

				// Resolution buttons
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						this->MakeResolutionButtons(Idx)
					]
				]
			]
		];
	}
}

// =============================================================================
// MakeResolutionButtons — build a horizontal row of toggle-style buttons
// =============================================================================

TSharedRef<SWidget> SReimportConflictDialog::MakeResolutionButtons(int32 ConflictIndex)
{
	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

	if (!Conflicts || !Conflicts->IsValidIndex(ConflictIndex))
	{
		return Box;
	}

	const TArray<FResolutionOption> Options = GetResolutionOptions((*Conflicts)[ConflictIndex].Type);

	for (const FResolutionOption& Opt : Options)
	{
		const EReimportConflictResolution ThisResolution = Opt.Resolution;

		Box->AddSlot()
		.AutoWidth()
		.Padding(0, 0, 4, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.OnClicked_Lambda([this, ConflictIndex, ThisResolution]()
			{
				if (Conflicts && Conflicts->IsValidIndex(ConflictIndex))
				{
					FReimportConflict& C = (*Conflicts)[ConflictIndex];
					// Toggle: clicking the already-selected resolution deselects it
					if (C.Resolution == ThisResolution)
					{
						C.Resolution = EReimportConflictResolution::Unresolved;
					}
					else
					{
						C.Resolution = ThisResolution;
					}
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(Opt.Label)
				.Font_Lambda([this, ConflictIndex, ThisResolution]() -> FSlateFontInfo
				{
					bool bSelected = false;
					if (Conflicts && Conflicts->IsValidIndex(ConflictIndex))
					{
						bSelected = ((*Conflicts)[ConflictIndex].Resolution == ThisResolution);
					}
					return bSelected
						? FCoreStyle::GetDefaultFontStyle("Bold", 9)
						: FCoreStyle::GetDefaultFontStyle("Regular", 9);
				})
				.ColorAndOpacity_Lambda([this, ConflictIndex, ThisResolution]() -> FSlateColor
				{
					bool bSelected = false;
					if (Conflicts && Conflicts->IsValidIndex(ConflictIndex))
					{
						bSelected = ((*Conflicts)[ConflictIndex].Resolution == ThisResolution);
					}
					return bSelected
						? FSlateColor(FLinearColor(0.2f, 0.8f, 0.4f))
						: FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f));
				})
			]
		];
	}

	return Box;
}

#undef LOCTEXT_NAMESPACE
