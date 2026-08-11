// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EffectProfileEditorModel.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SVerticalBox;

/** Focused active-contract editor: label, type, descriptors, ordering, membership, and source. */
class SEffectProfileDetailsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEffectProfileDetailsPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FEffectProfileEditorModel>, Model)
		SLATE_EVENT(FSimpleDelegate, OnOpenAdvancedDetails)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SEffectProfileDetailsPanel() override;

	/** Validation navigation seam. Unknown/compatibility fields route to Advanced Details. */
	void FocusField(FName FieldName);
	bool HasSelectionForTests() const;
	FName GetLastFocusedFieldForTests() const { return LastFocusedField; }
	void FlushPendingRefreshForTests();

private:
	void RequestRefresh();
	EActiveTimerReturnType HandleDeferredRefresh(double CurrentTime, float DeltaTime);
	void Refresh();
	void ScheduleFieldFocus(FName FieldName);
	void HandleLabelCommitted(const FText& NewText, ETextCommit::Type CommitType);
	TSharedRef<SWidget> BuildTypeControl(const FPaper2DPlusEffectProfileEntry& Entry);
	TSharedRef<SWidget> BuildDescriptorControl(const FPaper2DPlusEffectProfileEntry& Entry);
	TSharedRef<SWidget> BuildActionsMenu();
	FReply HandleMoveUp();
	FReply HandleMoveDown();
	FReply HandleRemove();
	FReply HandleOpenSource();
	FReply HandleOpenAdvanced();

	TSharedPtr<FEffectProfileEditorModel> Model;
	TSharedPtr<SVerticalBox> ContentBox;
	TSharedPtr<SEditableTextBox> LabelEditor;
	TSharedPtr<SWidget> TypeFocusWidget;
	TSharedPtr<SWidget> DescriptorFocusWidget;
	TSharedPtr<SWidget> OpenSourceFocusWidget;
	TSharedPtr<SWidget> AdvancedFocusWidget;
	FSimpleDelegate OnOpenAdvancedDetails;
	FDelegateHandle SourceChangedHandle;
	TWeakPtr<FActiveTimerHandle> RefreshTimerHandle;
	TWeakPtr<FActiveTimerHandle> FocusTimerHandle;
	FName LastFocusedField = NAME_None;
	FName PendingFocusField = NAME_None;
};
