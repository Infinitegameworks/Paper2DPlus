// Copyright 2026 Infinite Gameworks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CharacterCatalogEditorModel.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class SButton;

/** Docked Unreal-style Details surface for the selected Character Catalog row. */
class PAPER2DPLUSEDITOR_API SCharacterCatalogDetailsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCharacterCatalogDetailsPanel) {}
		SLATE_ARGUMENT(TSharedPtr<FCharacterCatalogEditorModel>, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SCharacterCatalogDetailsPanel() override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Moves keyboard focus to the selected Character's status row after warning navigation. */
	bool FocusSelectedCharacter();

	TSharedPtr<SWidget> GetStatusFocusTargetForTests() const { return StatusFocusTarget; }
	bool HasDetailsSurfaceForTests() const { return DetailsBox.IsValid(); }
	TSharedPtr<SButton> GetRemoveCharacterButtonForTests() const { return RemoveCharacterButton; }
	void SetRemoveCharacterConfirmationForTests(TFunction<bool(const FText&)> Confirmation)
	{
		RemoveCharacterConfirmation = MoveTemp(Confirmation);
	}
	bool RequestSelectedCharacterRemovalForTests();
	/** Drive the Suggest Companions action headlessly; the outcome message routes through ShowMessage. */
	bool RequestCompanionSuggestionForTests(const FSoftObjectPath& CharacterPath)
	{
		return RequestCompanionSuggestion(CharacterPath);
	}
	/**
	 * Capture outcome messages instead of opening a modal. Without this an interactive host would block
	 * automation on a human dismissing the dialog, so the seam exists for the same reason the removal
	 * confirmation one does — and it lets a test assert what the designer is actually told.
	 */
	void SetMessageSinkForTests(TFunction<void(const FText&)> Sink)
	{
		MessageSink = MoveTemp(Sink);
	}
	void QueueEntryTagsCommitForTests(
		const FSoftObjectPath& CharacterPath,
		const FGameplayTagContainer& Tags);
	bool HasPendingEntryTagsCommitForTests() const { return PendingEntryTagsCommit.IsSet(); }
	void FlushEntryTagsCommitForTests();

private:
	struct FPendingEntryTagsCommit
	{
		FSoftObjectPath CharacterPath;
		FGameplayTagContainer Tags;
	};

	void HandleModelChanged();
	void RebuildDetails();
	TSharedRef<SWidget> BuildCategory(
		FName CategoryId,
		const FText& Label,
		const FText& Summary,
		const TSharedRef<SWidget>& Body);
	TSharedRef<SWidget> BuildCharacterSection(
		const FPaper2DPlusCharacterCatalogEditorRow& Row);
	TSharedRef<SWidget> BuildCompanionSection(
		const FPaper2DPlusCharacterCatalogEditorRow& Row,
		EPaper2DPlusCatalogCompanion Companion);
	TSharedRef<SWidget> BuildEntryTagsControl(
		const FPaper2DPlusCharacterCatalogEditorRow& Row);
	bool RequestCharacterRemoval(const FSoftObjectPath& CharacterPath, const FText& DisplayName);
	bool RequestCompanionSuggestion(const FSoftObjectPath& CharacterPath);
	void QueueEntryTagsCommit(
		const FSoftObjectPath& CharacterPath,
		const FGameplayTagContainer& Tags);
	EActiveTimerReturnType HandleDeferredEntryTagsCommit(double CurrentTime, float DeltaTime);
	void ShowMessage(const FText& Message) const;

	TSharedPtr<FCharacterCatalogEditorModel> Model;
	TSharedPtr<SVerticalBox> DetailsBox;
	TSharedPtr<SWidget> StatusFocusTarget;
	TSharedPtr<SButton> RemoveCharacterButton;
	TFunction<bool(const FText&)> RemoveCharacterConfirmation;
	TFunction<void(const FText&)> MessageSink;
	TOptional<FPendingEntryTagsCommit> PendingEntryTagsCommit;
	TWeakPtr<FActiveTimerHandle> EntryTagsCommitTimerHandle;
	TMap<FName, bool> CategoryExpansion;
	FDelegateHandle ModelChangedHandle;
};
