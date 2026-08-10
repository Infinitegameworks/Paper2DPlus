// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FCharacterProfileEditorModel;

class SOverviewPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOverviewPanel)
		: _ShowDetails(true)
		{}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		/** When false, render only the flipbook browser (no 70/30 sub-splitter / details column). The
		 *  merged Animations tab (TASK-96 P3) sets this false so the ONE shared SProfileDetailsPanel on
		 *  the right of SAnimationsPanel is the single details surface across List/Card/Map. */
		SLATE_ARGUMENT(bool, ShowDetails)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedPtr<FCharacterProfileEditorModel> Model;

	void ShowFlipbookContextMenu(int32 FlipbookIndex);
};
