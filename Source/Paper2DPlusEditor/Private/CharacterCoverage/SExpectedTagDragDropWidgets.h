// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CharacterCoverage/ExpectedTagAssignment.h"

#include "Input/DragAndDrop.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 0
#include "EditorStyleSet.h"
using FExpectedTagDragDropAppStyle = FEditorStyle;
#else
#include "Styling/AppStyle.h"
using FExpectedTagDragDropAppStyle = FAppStyle;
#endif
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

class UPaper2DPlusCharacterProfileAsset;

/** Drag payload for one exact Catalog-declared expected animation tag. */
class FExpectedTagDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FExpectedTagDragDropOp, FDragDropOperation)

	FGameplayTag ExpectedTag;
	TWeakObjectPtr<UPaper2DPlusCharacterProfileAsset> SourceAsset;

	static TSharedRef<FExpectedTagDragDropOp> New(
		const FGameplayTag& InExpectedTag,
		UPaper2DPlusCharacterProfileAsset* InSourceAsset)
	{
		TSharedRef<FExpectedTagDragDropOp> Op =
			MakeShareable(new FExpectedTagDragDropOp());
		Op->ExpectedTag = InExpectedTag;
		Op->SourceAsset = InSourceAsset;
		Op->Construct();
		return Op;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return SNew(SBorder)
			.BorderImage(FExpectedTagDragDropAppStyle::Get().GetBrush(
				"ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6.0f, 2.0f))
			[
				SNew(STextBlock)
				.Text(FText::FromName(ExpectedTag.GetTagName()))
			];
	}
};

/**
 * Assignment drop target shared by browser cards/rows and future animation targets.
 *
 * Every recognized expected-tag drop is consumed, including null/foreign/no-op payloads, so stale
 * app-wide drag data can never bubble into an ancestor accept site.
 */
class SExpectedTagAnimationDropTarget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SExpectedTagAnimationDropTarget) {}
		SLATE_ARGUMENT(FExpectedTagAnimationTarget, Target)
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Target = InArgs._Target;
		Model = InArgs._Model;
		ChildSlot[InArgs._Content.Widget];
	}

	virtual void OnDragEnter(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override
	{
		if (TSharedPtr<FExpectedTagDragDropOp> Op =
			DragDropEvent.GetOperationAs<FExpectedTagDragDropOp>())
		{
			UpdateCursorFeedback(Op);
		}
	}

	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override
	{
		if (TSharedPtr<FExpectedTagDragDropOp> Op =
			DragDropEvent.GetOperationAs<FExpectedTagDragDropOp>())
		{
			Op->SetCursorOverride(TOptional<EMouseCursor::Type>());
		}
	}

	virtual FReply OnDragOver(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override
	{
		if (TSharedPtr<FExpectedTagDragDropOp> Op =
			DragDropEvent.GetOperationAs<FExpectedTagDragDropOp>())
		{
			UpdateCursorFeedback(Op);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnDrop(
		const FGeometry& MyGeometry,
		const FDragDropEvent& DragDropEvent) override
	{
		TSharedPtr<FExpectedTagDragDropOp> Op =
			DragDropEvent.GetOperationAs<FExpectedTagDragDropOp>();
		if (!Op.IsValid())
		{
			return FReply::Unhandled();
		}

		Op->SetCursorOverride(TOptional<EMouseCursor::Type>());
		const FExpectedTagAnimationTarget DropTarget = Target;
		const TSharedPtr<FCharacterProfileEditorModel> DropModel = Model;
		FExpectedTagAssignment::Assign(
			Op->ExpectedTag,
			Op->SourceAsset,
			DropTarget,
			DropModel);
		return FReply::Handled();
	}

private:
	void UpdateCursorFeedback(const TSharedPtr<FExpectedTagDragDropOp>& Op) const
	{
		const EExpectedTagAssignmentDisposition Disposition =
			FExpectedTagAssignment::Evaluate(
				Op->ExpectedTag,
				Op->SourceAsset,
				Target,
				Model);
		Op->SetCursorOverride(
			Disposition == EExpectedTagAssignmentDisposition::Assignable
				? TOptional<EMouseCursor::Type>()
				: TOptional<EMouseCursor::Type>(EMouseCursor::SlashedCircle));
	}

	FExpectedTagAnimationTarget Target;
	TSharedPtr<FCharacterProfileEditorModel> Model;
};
