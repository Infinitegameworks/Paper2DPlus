// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EffectProfileEditorModel.h"
#include "Widgets/SCompoundWidget.h"

class SProfileNavigatorPanel;

/** Virtualized Effect library, intake, descriptor filtering, and Content Browser drop target. */
class SEffectProfileLibraryPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEffectProfileLibraryPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FEffectProfileEditorModel>, Model)
		SLATE_EVENT(FSimpleDelegate, OnOpenValidation)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SEffectProfileLibraryPanel() override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

	int32 GetResultCountForTests() const;
	int32 GenerateRowsForViewportForTests(const FVector2D& ViewportSize);
	int32 GetRefreshCountForTests() const { return RefreshCount; }
	bool IsShowingEmptyStateForTests() const;
	const FText& GetLastIntakeMessageForTests() const { return LastIntakeMessage; }

private:
	FReply HandleAddExistingClicked();
	FReply HandleValidationClicked();
	void OpenAssetPicker();
	void ApplyIntakeResult(const FEffectProfileIntakeResult& Result);
	void HandleModelChanged();
	void HandleItemDoubleClicked(const FProfileItemIdentity& Identity);
	TSharedRef<SWidget> BuildDescriptorFilterControl();
	TSharedPtr<SWidget> BuildItemPreview(const FProfilePickerItem& Item);
	TSharedRef<SWidget> BuildItemContent(
		const FProfileItemIdentity& Identity,
		const TSharedRef<SWidget>& DefaultContent);
	TSharedRef<SWidget> BuildRowExtension(const FProfileItemIdentity& Identity);
	EVisibility GetEmptyLibraryVisibility() const;
	FText GetDragStateText() const;

	TSharedPtr<FEffectProfileEditorModel> Model;
	TSharedPtr<SProfileNavigatorPanel> Navigator;
	FSimpleDelegate OnOpenValidation;
	FDelegateHandle SourceChangedHandle;
	FText LastIntakeMessage;
	bool bDragOver = false;
	bool bHasIntakeResult = false;
	int32 RefreshCount = 0;
};
