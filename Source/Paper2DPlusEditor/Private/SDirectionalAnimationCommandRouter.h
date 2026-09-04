// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_RetVal_OneParam(
	bool,
	FOnDirectionalAnimationPreviewKeyDown,
	const FKeyEvent&)

/**
 * Gives the remappable directional-wheel command first refusal before a focused authoring child.
 * The handler deliberately returns a bool so an ignored text-entry chord remains unhandled and
 * continues down the normal Slate route.
 */
class SDirectionalAnimationCommandRouter final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDirectionalAnimationCommandRouter) {}
		SLATE_EVENT(
			FOnDirectionalAnimationPreviewKeyDown,
			OnDirectionalAnimationPreviewKeyDown)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		PreviewKeyDownHandler = InArgs._OnDirectionalAnimationPreviewKeyDown;
		ChildSlot
		[
			InArgs._Content.Widget
		];
	}

	virtual FReply OnPreviewKeyDown(
		const FGeometry& MyGeometry,
		const FKeyEvent& InKeyEvent) override
	{
		(void)MyGeometry;
		return PreviewKeyDownHandler.IsBound()
			&& PreviewKeyDownHandler.Execute(InKeyEvent)
			? FReply::Handled()
			: FReply::Unhandled();
	}

private:
	FOnDirectionalAnimationPreviewKeyDown PreviewKeyDownHandler;
};
