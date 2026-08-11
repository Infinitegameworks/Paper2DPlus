// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#include "HitboxConflictDialog.h"

#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ScopeExit.h"
#include "Editor.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 1
#include "EditorStyleSet.h"
#ifndef FAppStyle
#define FAppStyle FEditorStyle
#define GetAppStyleSetName GetStyleSetName
#endif
#else
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "HitboxConflictDialog"

bool SHitboxConflictDialog::bDialogOpen = false;

// =============================================================================
// ShowDialog / IsDialogOpen
// =============================================================================

EHitboxApplyPolicy SHitboxConflictDialog::ShowDialog(int32 ConflictCount, EHitboxApplyPolicy DefaultPolicy)
{
	if (bDialogOpen)
	{
		// Re-entrancy: never stack a second prompt — fall back to the default policy.
		return DefaultPolicy;
	}

	bDialogOpen = true;
	// RAII reset: any early/exceptional return below still clears the flag, so a one-off failure can't strand it
	// and permanently suppress the prompt for the rest of the session.
	ON_SCOPE_EXIT { bDialogOpen = false; };

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("DialogTitle", "Existing Hitboxes Found"))
		.ClientSize(FVector2D(460, 230))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.IsTopmostWindow(true);

	TSharedPtr<SHitboxConflictDialog> Dialog;
	Window->SetContent(
		SAssignNew(Dialog, SHitboxConflictDialog)
			.ParentWindow(Window)
			.ConflictCount(ConflictCount)
	);

	// Seed with the default so a title-bar close (or a suppressed prompt below) returns the least-destructive choice.
	Dialog->ChosenPolicy = DefaultPolicy;

	if (FSlateApplication::IsInitialized())
	{
		// Match SReimportConflictDialog: parent to the active top-level window via FSlateApplication.
		FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().GetActiveTopLevelWindow());
	}
	else
	{
		// No Slate (e.g. -nullrhi / headless): can't prompt. Returning the safe default silently would hide that a
		// conflict went unprompted — log it so a suppressed prompt isn't invisible.
		UE_LOG(LogTemp, Warning,
			TEXT("SHitboxConflictDialog: Slate is not initialized — skipping the hitbox conflict prompt and returning the default policy (%d conflicting frame(s))."),
			ConflictCount);
	}

	return Dialog->ChosenPolicy;
}

bool SHitboxConflictDialog::IsDialogOpen()
{
	return bDialogOpen;
}

// =============================================================================
// Construct
// =============================================================================

void SHitboxConflictDialog::Construct(const FArguments& InArgs)
{
	ParentWindow = InArgs._ParentWindow;
	ConflictCount = InArgs._ConflictCount;

	// A labeled choice button: title + one-line explanation, closes the dialog with the given policy.
	auto MakeChoiceButton = [this](const FText& Title, const FText& Desc, EHitboxApplyPolicy Policy) -> TSharedRef<SWidget>
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
			.HAlign(HAlign_Fill)
			.OnClicked_Lambda([this, Policy]()
			{
				CloseDialog(Policy);
				return FReply::Handled();
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(Title)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 1, 0, 0)
				[
					SNew(STextBlock)
					.Text(Desc)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
					.AutoWrapText(true)
				]
			];
	};

	ChildSlot
	[
		SNew(SVerticalBox)

		// ---- Header text ----
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12, 12, 12, 4)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("HeaderText",
				"{0} animation frame(s) being imported already have hitboxes or sockets.\nChoose how to deliver the imported boxes:"),
				FText::AsNumber(ConflictCount)))
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

		// ---- Choice buttons ----
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12, 2)
		[
			MakeChoiceButton(
				LOCTEXT("MergeTitle", "Merge (append)"),
				LOCTEXT("MergeDesc", "Append imported boxes/sockets that aren't byte-identical to existing ones. Re-importing a NUDGED box adds a near-duplicate (it isn't byte-identical) — use Overwrite to replace boxes."),
				EHitboxApplyPolicy::Merge)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12, 2)
		[
			MakeChoiceButton(
				LOCTEXT("OverwriteTitle", "Overwrite"),
				LOCTEXT("OverwriteDesc", "Replace the conflicting frames' boxes/sockets with the imported set."),
				EHitboxApplyPolicy::Overwrite)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12, 2, 12, 8)
		[
			MakeChoiceButton(
				LOCTEXT("ApplyTitle", "Apply to empty only"),
				LOCTEXT("ApplyDesc", "Keep existing boxes; only fill frames that currently have none (least destructive)."),
				EHitboxApplyPolicy::Apply)
		]
	];
}

// =============================================================================
// CloseDialog
// =============================================================================

void SHitboxConflictDialog::CloseDialog(EHitboxApplyPolicy Choice)
{
	ChosenPolicy = Choice;
	if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}
}

#undef LOCTEXT_NAMESPACE
