// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "AsepriteImporter.h" // EHitboxApplyPolicy

/**
 * Modal 3-way prompt shown when a layered-import would overwrite hitboxes/sockets that already exist on the
 * target CharacterProfile frames. Lets the user pick how the imported `.ase` boxes are delivered:
 *   Merge     — append the imported boxes/sockets to the existing ones (dedupe exact duplicates).
 *   Overwrite — replace the conflicting frames' boxes/sockets with the imported set.
 *   Apply     — least-destructive: write only on frames that currently have none of that kind.
 *
 * Mirrors SReimportConflictDialog's minimal-SWindow + FSlateApplication::AddModalWindow pattern with a static
 * re-entrancy guard reset via RAII (ON_SCOPE_EXIT) so an early/exceptional return can't strand the flag. The
 * CALLER decides whether to show this (it only prompts on real conflicts when the editor is attended and not
 * running automation); ShowDialog returns the chosen policy and is safe to call when Slate is uninitialized
 * (it logs and returns the default).
 */
class SHitboxConflictDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHitboxConflictDialog) {}
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
		SLATE_ARGUMENT(int32, ConflictCount)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Show the conflict prompt modally and return the chosen policy. ConflictCount is purely informational
	 * (shown in the body). DefaultPolicy is returned when the dialog is closed via the title-bar X or when a
	 * dialog is already open (re-entrancy). Never call when running unattended/commandlet — the caller guards.
	 */
	static EHitboxApplyPolicy ShowDialog(int32 ConflictCount, EHitboxApplyPolicy DefaultPolicy = EHitboxApplyPolicy::Apply);

	/** Re-entrancy guard — true if a hitbox conflict dialog is currently open. */
	static bool IsDialogOpen();

private:
	void CloseDialog(EHitboxApplyPolicy Choice);

	TWeakPtr<SWindow> ParentWindow;
	int32 ConflictCount = 0;
	EHitboxApplyPolicy ChosenPolicy = EHitboxApplyPolicy::Apply;

	static bool bDialogOpen;
};
