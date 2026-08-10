// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FReimportConflict;

/**
 * Modal dialog that presents batched conflicts from an auto-reimport operation
 * and collects per-item resolutions from the user.
 *
 * Each conflict row shows a type label, description, and resolution buttons
 * appropriate for the conflict type. The Apply button is only enabled when
 * every conflict has a resolution selected.
 */
class SReimportConflictDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SReimportConflictDialog) {}
		SLATE_ARGUMENT(TArray<FReimportConflict>*, Conflicts)
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	bool WasConfirmed() const { return bConfirmed; }

	/** Show the conflict dialog modally. Returns true if user confirmed. Mutates Conflicts in-place with resolutions. */
	static bool ShowConflictDialog(TArray<FReimportConflict>& Conflicts);

	/** Re-entrancy guard -- true if a conflict dialog is currently open. */
	static bool IsDialogOpen();

private:
	void CloseDialog(bool bAccept);
	void RebuildConflictList();
	bool AreAllConflictsResolved() const;
	TSharedRef<SWidget> MakeResolutionButtons(int32 ConflictIndex);

	TArray<FReimportConflict>* Conflicts = nullptr;
	TWeakPtr<SWindow> ParentWindow;
	bool bConfirmed = false;

	TSharedPtr<SVerticalBox> ConflictListBox;
	TSharedPtr<SButton> ApplyButton;

	static bool bDialogOpen;
};
