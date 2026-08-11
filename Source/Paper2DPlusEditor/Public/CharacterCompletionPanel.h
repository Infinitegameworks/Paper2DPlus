// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "ProfileToolPanelProvider.h"
#include "Widgets/SCompoundWidget.h"

class FCharacterProfileEditorModel;

/**
 * Persistent, selected-animation Completion surface for the Character workspace.
 *
 * Completion is intentionally small and fixed: Hitboxes, Alignment, Timing, Motion, and Tags. The
 * panel follows the shared model selection and resolves a stable animation identity at every write,
 * so an undo/reimport/reorder cannot redirect a delayed click to a neighboring Flipbooks[] row.
 */
class PAPER2DPLUSEDITOR_API SCharacterCompletionPanel final
	: public SCompoundWidget
	, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SCharacterCompletionPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FCharacterProfileEditorModel>, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SCharacterCompletionPanel() override;

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	/** Idempotent early disconnect used by the toolkit before its shared model is released. */
	void Shutdown();

	/** Headless seams used by the workspace acceptance tests. */
	int32 GetDisplayedTaskCountForTests() const;
	int32 GetSelectedCompletionMaskForTests() const;
	bool SetTaskCompletedForTests(int32 TaskBit, bool bCompleted);
	bool SetAllCompletedForTests(bool bCompleted);
	int32 GetModelSubscriptionCountForTests() const;

private:
	struct FTaskDescriptor
	{
		int32 Bit = INDEX_NONE;
		FText Label;
		FText ToolTip;
	};

	static const TArray<FTaskDescriptor>& GetTasks();
	FProfileAnimationIdentity GetSelectedAnimationIdentity() const;
	/** Paint-time O(1) lookup; delayed mutation callbacks use GetSelectedAnimationIdentity instead. */
	int32 GetLiveSelectedAnimationIndex() const;
	bool HasSelectedAnimation() const;
	int32 GetSelectedCompletionFlags() const;
	int32 GetCompletedTaskCount() const;
	FText GetHeaderText() const;
	FText GetProgressText() const;
	TOptional<float> GetProgressFraction() const;
	ECheckBoxState GetTaskCheckState(int32 TaskBit) const;
	void HandleTaskCheckStateChanged(ECheckBoxState NewState, int32 TaskBit);
	FReply HandleSetAllClicked(bool bCompleted);
	bool MutateSelectedCompletionFlags(int32 NewFlags, const FText& TransactionText);
	void HandleSelectionChanged(int32 NewIndex);
	void HandleModelRefresh();

	TSharedPtr<FCharacterProfileEditorModel> Model;
	FDelegateHandle SelectionChangedHandle;
	FDelegateHandle AssetDataChangedHandle;
	FDelegateHandle ExternalModifiedHandle;
	bool bShutdown = false;
};
