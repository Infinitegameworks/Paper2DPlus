// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "Paper2DPlusAuthoringProgressTags.h"
#include "Widgets/SCompoundWidget.h"

enum class EProfileCompletionKind : uint8
{
	Layer,
	Effect,
	Combat
};

/**
 * Shared asset-level Completion surface for Layer, Effect, and Combat profiles.
 * Character keeps its selected-animation Completion panel because its progress granularity is different.
 */
class PAPER2DPLUSEDITOR_API SProfileCompletionPanel final
	: public SCompoundWidget
	, public FEditorUndoClient
{
public:
	/**
	 * Derived from the runtime tag contract so the checkbox list and the exported CompletionTotal tag
	 * cannot disagree: adding a criterion means bumping ProfileChecklistCriteriaCount, which updates
	 * this mask, the exported total, and the Catalog's denominator together.
	 */
	static constexpr int32 AllCriteriaMask = Paper2DPlusAuthoringProgress::ProfileChecklistCriteriaMask;

	SLATE_BEGIN_ARGS(SProfileCompletionPanel)
		: _Asset(nullptr)
		, _ProfileKind(EProfileCompletionKind::Layer)
	{}
		SLATE_ARGUMENT(UObject*, Asset)
		SLATE_ARGUMENT(EProfileCompletionKind, ProfileKind)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SProfileCompletionPanel() override;

	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	void Shutdown();

	/** Headless seams for the shared profile-workspace contract. */
	int32 GetDisplayedCriterionCountForTests() const;
	FText GetCriterionLabelForTests(int32 CriterionBit) const;
	int32 GetCompletionMaskForTests() const;
	bool SetCriterionCompletedForTests(int32 CriterionBit, bool bCompleted);
	bool SetAllCompletedForTests(bool bCompleted);

private:
	struct FCriterion
	{
		int32 Bit = INDEX_NONE;
		FText Label;
		FText ToolTip;
	};

	const TArray<FCriterion>& GetCriteria() const;
	int32* ResolveCompletionFlags() const;
	int32 GetCompletionFlags() const;
	int32 GetCompletedCriterionCount() const;
	FText GetHeaderText() const;
	FText GetProgressText() const;
	TOptional<float> GetProgressFraction() const;
	ECheckBoxState GetCriterionCheckState(int32 CriterionBit) const;
	void HandleCriterionCheckStateChanged(ECheckBoxState NewState, int32 CriterionBit);
	FReply HandleSetAllClicked(bool bCompleted);
	bool MutateCompletionFlags(int32 NewFlags, const FText& TransactionText);

	TWeakObjectPtr<UObject> Asset;
	EProfileCompletionKind ProfileKind = EProfileCompletionKind::Layer;
	bool bShutdown = false;
};
